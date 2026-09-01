#include "momo_play_ball.h"

#include <stddef.h>
#include <string.h>

#define PLAYBALL_Q8_ONE (256L)
#define PLAYBALL_MAX_SPEED_Q8 (520L * PLAYBALL_Q8_ONE)
#define PLAYBALL_STOP_SPEED_Q8 (20L * PLAYBALL_Q8_ONE)
#define PLAYBALL_MOMO_SPEED_PX_S (100L)
#define PLAYBALL_MIN_SAMPLE_MS (8U)
#define PLAYBALL_MAX_SAMPLE_MS (200U)
#define PLAYBALL_MAX_DELTA_MS (100U)
#define PLAYBALL_ROUND_TIMEOUT_MS (5000U)
#define PLAYBALL_FEEDBACK_MS (700U)

_Static_assert(sizeof(MOMO_PLAY_BALL) < 256U, "Momo play ball state too large");

/* 将坐标限制在闭区间内。 */
static int16_t PlayBall_Clamp16(int16_t sValue, int16_t sMin, int16_t sMax)
{
    if (sValue < sMin)
    {
        return sMin;
    }
    if (sMax < sValue)
    {
        return sMax;
    }
    return sValue;
}

/* 返回物理模块有界输入的绝对值。 */
static int32_t PlayBall_Abs32(int32_t lValue)
{
    return (0L > lValue) ? -lValue : lValue;
}

/* 将Q8速度限制到安全上限后收窄为32位。 */
static int32_t PlayBall_ClampVelocity(int64_t dVelocityQ8)
{
    if ((int64_t)PLAYBALL_MAX_SPEED_Q8 < dVelocityQ8)
    {
        return PLAYBALL_MAX_SPEED_Q8;
    }
    if (dVelocityQ8 < -(int64_t)PLAYBALL_MAX_SPEED_Q8)
    {
        return -PLAYBALL_MAX_SPEED_Q8;
    }
    return (int32_t)dVelocityQ8;
}

/* 清除已失效的拖拽速度，禁止松手复用旧样本。 */
static void PlayBall_ClearVelocitySample(MOMO_PLAY_BALL *pGame)
{
    pGame->lReleaseVelocityXQ8 = 0L;
    pGame->lReleaseVelocityYQ8 = 0L;
    pGame->ulVelocitySampleTickMs = 0U;
    pGame->bHasVelocitySample = false;
}

/* 校验所有起点、活动区和碰撞参数。 */
static bool PlayBall_BoundsValid(const MOMO_PLAY_BALL_BOUNDS *pBounds)
{
    if (NULL == pBounds)
    {
        return false;
    }
    if ((pBounds->sBallMaxX <= pBounds->sBallMinX) ||
        (pBounds->sBallMaxY <= pBounds->sBallMinY) ||
        (pBounds->sMomoMaxX < pBounds->sMomoMinX) ||
        (pBounds->sMomoMaxY < pBounds->sMomoMinY))
    {
        return false;
    }
    if ((pBounds->sBallStartX < pBounds->sBallMinX) ||
        (pBounds->sBallMaxX < pBounds->sBallStartX) ||
        (pBounds->sBallStartY < pBounds->sBallMinY) ||
        (pBounds->sBallMaxY < pBounds->sBallStartY) ||
        (pBounds->sMomoStartX < pBounds->sMomoMinX) ||
        (pBounds->sMomoMaxX < pBounds->sMomoStartX) ||
        (pBounds->sMomoStartY < pBounds->sMomoMinY) ||
        (pBounds->sMomoMaxY < pBounds->sMomoStartY))
    {
        return false;
    }
    return (0U < pBounds->ucHitRadius) && (64U >= pBounds->ucHitRadius) &&
           (0U < pBounds->ucCollisionX) && (127U >= pBounds->ucCollisionX) &&
           (0U < pBounds->ucCollisionY) && (127U >= pBounds->ucCollisionY);
}

/* 清除本轮瞬态并恢复确定性起点。 */
static void PlayBall_Reset(MOMO_PLAY_BALL *pGame)
{
    pGame->lBallXQ8 = (int32_t)pGame->tBounds.sBallStartX * PLAYBALL_Q8_ONE;
    pGame->lBallYQ8 = (int32_t)pGame->tBounds.sBallStartY * PLAYBALL_Q8_ONE;
    pGame->sBallX = pGame->tBounds.sBallStartX;
    pGame->sBallY = pGame->tBounds.sBallStartY;
    pGame->sMomoX = pGame->tBounds.sMomoStartX;
    pGame->sMomoY = pGame->tBounds.sMomoStartY;
    pGame->lVelocityXQ8 = 0L;
    pGame->lVelocityYQ8 = 0L;
    pGame->lReleaseVelocityXQ8 = 0L;
    pGame->lReleaseVelocityYQ8 = 0L;
    pGame->sLastTouchX = pGame->sBallX;
    pGame->sLastTouchY = pGame->sBallY;
    pGame->ulLastTouchTickMs = 0U;
    pGame->ulVelocitySampleTickMs = 0U;
    pGame->usRoundElapsedMs = 0U;
    pGame->usFeedbackElapsedMs = 0U;
    pGame->bHasVelocitySample = false;
    pGame->bCollisionLatched = false;
    pGame->eState = MOMO_PLAY_BALL_STATE_IDLE;
}

/* 初始化静态状态机；无效参数使功能保持禁用。 */
void MOMOPLAYBALL_Init(MOMO_PLAY_BALL *pGame,
                       const MOMO_PLAY_BALL_BOUNDS *pBounds)
{
    if (NULL == pGame)
    {
        return;
    }
    (void)memset(pGame, 0, sizeof(*pGame));
    pGame->eState = MOMO_PLAY_BALL_STATE_DISABLED;
    if (PlayBall_BoundsValid(pBounds))
    {
        pGame->tBounds = *pBounds;
        PlayBall_Reset(pGame);
    }
}

/* 处理球命中区域内的按下，并开始拖拽采样。 */
bool MOMOPLAYBALL_Press(MOMO_PLAY_BALL *pGame, int16_t sTouchX,
                        int16_t sTouchY, uint32_t ulTickMs)
{
    int32_t lDeltaX;
    int32_t lDeltaY;

    if ((NULL == pGame) || (MOMO_PLAY_BALL_STATE_IDLE != pGame->eState))
    {
        return false;
    }
    lDeltaX = (int32_t)sTouchX - pGame->sBallX;
    lDeltaY = (int32_t)sTouchY - pGame->sBallY;
    if ((pGame->tBounds.ucHitRadius < PlayBall_Abs32(lDeltaX)) ||
        (pGame->tBounds.ucHitRadius < PlayBall_Abs32(lDeltaY)))
    {
        return false;
    }
    pGame->eState = MOMO_PLAY_BALL_STATE_DRAGGING;
    pGame->sLastTouchX = sTouchX;
    pGame->sLastTouchY = sTouchY;
    pGame->ulLastTouchTickMs = ulTickMs;
    PlayBall_ClearVelocitySample(pGame);
    pGame->bCollisionLatched = false;
    return true;
}

/* 更新有界球坐标，并从有效采样窗口计算释放速度。 */
bool MOMOPLAYBALL_Drag(MOMO_PLAY_BALL *pGame, int16_t sTouchX,
                       int16_t sTouchY, uint32_t ulTickMs)
{
    uint32_t ulDeltaMs;
    int16_t sClampedX;
    int16_t sClampedY;
    int32_t lDeltaX;
    int32_t lDeltaY;

    if ((NULL == pGame) || (MOMO_PLAY_BALL_STATE_DRAGGING != pGame->eState))
    {
        return false;
    }
    sClampedX = PlayBall_Clamp16(sTouchX, pGame->tBounds.sBallMinX,
                                 pGame->tBounds.sBallMaxX);
    sClampedY = PlayBall_Clamp16(sTouchY, pGame->tBounds.sBallMinY,
                                 pGame->tBounds.sBallMaxY);
    ulDeltaMs = (uint32_t)(ulTickMs - pGame->ulLastTouchTickMs);
    lDeltaX = (int32_t)sClampedX - pGame->sLastTouchX;
    lDeltaY = (int32_t)sClampedY - pGame->sLastTouchY;
    if ((PLAYBALL_MIN_SAMPLE_MS <= ulDeltaMs) &&
        (PLAYBALL_MAX_SAMPLE_MS >= ulDeltaMs) &&
        ((0L != lDeltaX) || (0L != lDeltaY)))
    {
        pGame->lReleaseVelocityXQ8 = PlayBall_ClampVelocity(
            ((int64_t)lDeltaX * 1000LL * PLAYBALL_Q8_ONE) / ulDeltaMs);
        pGame->lReleaseVelocityYQ8 = PlayBall_ClampVelocity(
            ((int64_t)lDeltaY * 1000LL * PLAYBALL_Q8_ONE) / ulDeltaMs);
        pGame->ulVelocitySampleTickMs = ulTickMs;
        pGame->bHasVelocitySample = true;
    }
    else if ((PLAYBALL_MAX_SAMPLE_MS < ulDeltaMs) ||
             ((PLAYBALL_MIN_SAMPLE_MS <= ulDeltaMs) &&
              (0L == lDeltaX) && (0L == lDeltaY)))
    {
        PlayBall_ClearVelocitySample(pGame);
    }
    pGame->sBallX = sClampedX;
    pGame->sBallY = sClampedY;
    pGame->lBallXQ8 = (int32_t)sClampedX * PLAYBALL_Q8_ONE;
    pGame->lBallYQ8 = (int32_t)sClampedY * PLAYBALL_Q8_ONE;
    pGame->sLastTouchX = sClampedX;
    pGame->sLastTouchY = sClampedY;
    pGame->ulLastTouchTickMs = ulTickMs;
    return true;
}

/* 结束拖拽；有有效速度时进入运动，否则安全复位。 */
bool MOMOPLAYBALL_Release(MOMO_PLAY_BALL *pGame, int16_t sTouchX,
                          int16_t sTouchY, uint32_t ulTickMs)
{
    if ((NULL == pGame) || (MOMO_PLAY_BALL_STATE_DRAGGING != pGame->eState))
    {
        return false;
    }
    (void)MOMOPLAYBALL_Drag(pGame, sTouchX, sTouchY, ulTickMs);
    if (!pGame->bHasVelocitySample ||
        (PLAYBALL_MAX_SAMPLE_MS <
         (uint32_t)(ulTickMs - pGame->ulVelocitySampleTickMs)))
    {
        PlayBall_Reset(pGame);
        return false;
    }
    pGame->lVelocityXQ8 = pGame->lReleaseVelocityXQ8;
    pGame->lVelocityYQ8 = pGame->lReleaseVelocityYQ8;
    pGame->usRoundElapsedMs = 0U;
    pGame->eState = MOMO_PLAY_BALL_STATE_MOVING;
    return true;
}

/* 取消当前轮次，供页面退出和动画仲裁调用。 */
void MOMOPLAYBALL_Cancel(MOMO_PLAY_BALL *pGame)
{
    if ((NULL != pGame) && (MOMO_PLAY_BALL_STATE_DISABLED != pGame->eState))
    {
        PlayBall_Reset(pGame);
    }
}

/* 以固定整数速度追逐球，并限制在Momo活动区。 */
static void PlayBall_MoveMomo(MOMO_PLAY_BALL *pGame, uint16_t usDeltaMs)
{
    int32_t lStep;
    int32_t lDelta;

    lStep = (PLAYBALL_MOMO_SPEED_PX_S * usDeltaMs) / 1000L;
    lStep = (1L > lStep) ? 1L : lStep;
    lDelta = (int32_t)pGame->sBallX - pGame->sMomoX;
    pGame->sMomoX = (int16_t)(pGame->sMomoX +
        ((lStep < lDelta) ? lStep : ((lDelta < -lStep) ? -lStep : lDelta)));
    lDelta = (int32_t)pGame->sBallY - pGame->sMomoY;
    pGame->sMomoY = (int16_t)(pGame->sMomoY +
        ((lStep < lDelta) ? lStep : ((lDelta < -lStep) ? -lStep : lDelta)));
    pGame->sMomoX = PlayBall_Clamp16(pGame->sMomoX,
        pGame->tBounds.sMomoMinX, pGame->tBounds.sMomoMaxX);
    pGame->sMomoY = PlayBall_Clamp16(pGame->sMomoY,
        pGame->tBounds.sMomoMinY, pGame->tBounds.sMomoMaxY);
}

/* 积分球的位置，处理四边反弹和速度衰减。 */
static void PlayBall_MoveBall(MOMO_PLAY_BALL *pGame, uint16_t usDeltaMs)
{
    int32_t lMinQ8;
    int32_t lMaxQ8;

    pGame->lBallXQ8 += (int32_t)(((int64_t)pGame->lVelocityXQ8 * usDeltaMs) / 1000LL);
    pGame->lBallYQ8 += (int32_t)(((int64_t)pGame->lVelocityYQ8 * usDeltaMs) / 1000LL);
    lMinQ8 = (int32_t)pGame->tBounds.sBallMinX * PLAYBALL_Q8_ONE;
    lMaxQ8 = (int32_t)pGame->tBounds.sBallMaxX * PLAYBALL_Q8_ONE;
    if ((pGame->lBallXQ8 < lMinQ8) || (lMaxQ8 < pGame->lBallXQ8))
    {
        pGame->lBallXQ8 = (pGame->lBallXQ8 < lMinQ8) ? lMinQ8 : lMaxQ8;
        pGame->lVelocityXQ8 = (-pGame->lVelocityXQ8 * 3L) / 4L;
    }
    lMinQ8 = (int32_t)pGame->tBounds.sBallMinY * PLAYBALL_Q8_ONE;
    lMaxQ8 = (int32_t)pGame->tBounds.sBallMaxY * PLAYBALL_Q8_ONE;
    if ((pGame->lBallYQ8 < lMinQ8) || (lMaxQ8 < pGame->lBallYQ8))
    {
        pGame->lBallYQ8 = (pGame->lBallYQ8 < lMinQ8) ? lMinQ8 : lMaxQ8;
        pGame->lVelocityYQ8 = (-pGame->lVelocityYQ8 * 3L) / 4L;
    }
    pGame->lVelocityXQ8 = (pGame->lVelocityXQ8 * 235L) / 256L;
    pGame->lVelocityYQ8 = (pGame->lVelocityYQ8 * 235L) / 256L;
    pGame->sBallX = (int16_t)(pGame->lBallXQ8 / PLAYBALL_Q8_ONE);
    pGame->sBallY = (int16_t)(pGame->lBallYQ8 / PLAYBALL_Q8_ONE);
}

/* 使用有界矩形碰撞区判断Momo是否接到球。 */
static bool PlayBall_Collided(const MOMO_PLAY_BALL *pGame)
{
    return (pGame->tBounds.ucCollisionX >= PlayBall_Abs32(
                (int32_t)pGame->sBallX - pGame->sMomoX)) &&
           (pGame->tBounds.ucCollisionY >= PlayBall_Abs32(
                (int32_t)pGame->sBallY - pGame->sMomoY));
}

/* 推进单步状态机并返回一次性界面事件。 */
MOMO_PLAY_BALL_EVENT MOMOPLAYBALL_Update(MOMO_PLAY_BALL *pGame,
                                         uint16_t usDeltaMs)
{
    if ((NULL == pGame) || (MOMO_PLAY_BALL_STATE_DISABLED == pGame->eState))
    {
        return MOMO_PLAY_BALL_EVENT_NONE;
    }
    usDeltaMs = (0U == usDeltaMs) ? 1U : usDeltaMs;
    usDeltaMs = (PLAYBALL_MAX_DELTA_MS < usDeltaMs) ?
        PLAYBALL_MAX_DELTA_MS : usDeltaMs;
    if (MOMO_PLAY_BALL_STATE_MOVING == pGame->eState)
    {
        pGame->usRoundElapsedMs = ((uint32_t)pGame->usRoundElapsedMs + usDeltaMs >
            PLAYBALL_ROUND_TIMEOUT_MS) ? PLAYBALL_ROUND_TIMEOUT_MS :
            (uint16_t)(pGame->usRoundElapsedMs + usDeltaMs);
        PlayBall_MoveBall(pGame, usDeltaMs);
        PlayBall_MoveMomo(pGame, usDeltaMs);
        if (!pGame->bCollisionLatched && PlayBall_Collided(pGame))
        {
            pGame->bCollisionLatched = true;
            pGame->lVelocityXQ8 = 0L;
            pGame->lVelocityYQ8 = 0L;
            pGame->usFeedbackElapsedMs = 0U;
            pGame->eState = MOMO_PLAY_BALL_STATE_FEEDBACK;
            return MOMO_PLAY_BALL_EVENT_CAUGHT;
        }
        if ((PLAYBALL_ROUND_TIMEOUT_MS <= pGame->usRoundElapsedMs) ||
            ((PLAYBALL_STOP_SPEED_Q8 > PlayBall_Abs32(pGame->lVelocityXQ8)) &&
             (PLAYBALL_STOP_SPEED_Q8 > PlayBall_Abs32(pGame->lVelocityYQ8))))
        {
            PlayBall_Reset(pGame);
            return MOMO_PLAY_BALL_EVENT_RESET;
        }
        return MOMO_PLAY_BALL_EVENT_POSITION_CHANGED;
    }
    if (MOMO_PLAY_BALL_STATE_FEEDBACK == pGame->eState)
    {
        if ((uint32_t)pGame->usFeedbackElapsedMs + usDeltaMs >= PLAYBALL_FEEDBACK_MS)
        {
            PlayBall_Reset(pGame);
            return MOMO_PLAY_BALL_EVENT_RESET;
        }
        pGame->usFeedbackElapsedMs = (uint16_t)(pGame->usFeedbackElapsedMs + usDeltaMs);
    }
    return MOMO_PLAY_BALL_EVENT_NONE;
}
