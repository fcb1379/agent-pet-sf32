#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "momo_play_ball.h"

#define TEST_Q8_ONE (256L)
#define TEST_MAX_SPEED_Q8 (520L * TEST_Q8_ONE)

static MOMO_PLAY_BALL_BOUNDS TEST_Bounds(void)
{
    MOMO_PLAY_BALL_BOUNDS tBounds = {
        48, 342, 61, 353, 96, 294, 96, 354,
        310, 150, 195, 213, 24U, 42U, 42U
    };
    return tBounds;
}

static uint32_t TEST_Random(uint32_t *pState)
{
    *pState = (*pState * 1664525UL) + 1013904223UL;
    return *pState;
}

static void TEST_Basics(void)
{
    MOMO_PLAY_BALL tGame;
    MOMO_PLAY_BALL_BOUNDS tBounds;

    tBounds = TEST_Bounds();
    MOMOPLAYBALL_Init(&tGame, &tBounds);
    assert(MOMO_PLAY_BALL_STATE_IDLE == tGame.eState);
    assert(!MOMOPLAYBALL_Press(&tGame, 100, 100, 1U));
    assert(MOMOPLAYBALL_Press(&tGame, tBounds.sBallStartX,
                              tBounds.sBallStartY, 0xFFFFFFF0UL));
    assert(MOMOPLAYBALL_Drag(&tGame, 290, 140, 4U));
    assert(MOMOPLAYBALL_Release(&tGame, 270, 130, 24U));
    assert(MOMO_PLAY_BALL_STATE_MOVING == tGame.eState);
    assert(0L > tGame.lVelocityXQ8);
    MOMOPLAYBALL_Cancel(&tGame);
    assert((MOMO_PLAY_BALL_STATE_IDLE == tGame.eState) &&
           (tBounds.sBallStartX == tGame.sBallX));

    tBounds.sBallMaxX = tBounds.sBallMinX;
    MOMOPLAYBALL_Init(&tGame, &tBounds);
    assert(MOMO_PLAY_BALL_STATE_DISABLED == tGame.eState);
    tBounds = TEST_Bounds();
    MOMOPLAYBALL_Init(&tGame, &tBounds);
    assert(MOMOPLAYBALL_Press(&tGame, tBounds.sBallStartX,
                              tBounds.sBallStartY, 10U));
    assert(!MOMOPLAYBALL_Release(&tGame, tBounds.sBallStartX,
                                 tBounds.sBallStartY, 20U));
    assert(MOMO_PLAY_BALL_EVENT_NONE == MOMOPLAYBALL_Update(NULL, 50U));
}

static void TEST_VelocityFreshness(void)
{
    MOMO_PLAY_BALL tGame;
    MOMO_PLAY_BALL_BOUNDS tBounds;
    int16_t sDragX;

    tBounds = TEST_Bounds();
    sDragX = (int16_t)(tBounds.sBallStartX - 20);

    MOMOPLAYBALL_Init(&tGame, &tBounds);
    assert(MOMOPLAYBALL_Press(&tGame, tBounds.sBallStartX,
                              tBounds.sBallStartY, 100U));
    assert(MOMOPLAYBALL_Drag(&tGame, sDragX, tBounds.sBallStartY, 120U));
    assert(tGame.bHasVelocitySample);
    assert(MOMOPLAYBALL_Drag(&tGame, sDragX, tBounds.sBallStartY, 128U));
    assert(!tGame.bHasVelocitySample);
    assert(!MOMOPLAYBALL_Release(&tGame, sDragX, tBounds.sBallStartY, 132U));

    MOMOPLAYBALL_Init(&tGame, &tBounds);
    assert(MOMOPLAYBALL_Press(&tGame, tBounds.sBallStartX,
                              tBounds.sBallStartY, 100U));
    assert(MOMOPLAYBALL_Drag(&tGame, sDragX, tBounds.sBallStartY, 120U));
    assert(!MOMOPLAYBALL_Release(&tGame, sDragX, tBounds.sBallStartY, 321U));

    MOMOPLAYBALL_Init(&tGame, &tBounds);
    assert(MOMOPLAYBALL_Press(&tGame, tBounds.sBallStartX,
                              tBounds.sBallStartY, 100U));
    assert(MOMOPLAYBALL_Drag(&tGame, sDragX, tBounds.sBallStartY, 120U));
    assert(MOMOPLAYBALL_Release(&tGame, sDragX, tBounds.sBallStartY, 124U));
    assert(MOMO_PLAY_BALL_STATE_MOVING == tGame.eState);
}

static void TEST_SetMoving(MOMO_PLAY_BALL *pGame, int16_t sBallX,
                           int16_t sBallY, int32_t lVelocityXQ8,
                           int32_t lVelocityYQ8)
{
    pGame->sBallX = sBallX;
    pGame->sBallY = sBallY;
    pGame->lBallXQ8 = (int32_t)sBallX * TEST_Q8_ONE;
    pGame->lBallYQ8 = (int32_t)sBallY * TEST_Q8_ONE;
    pGame->lVelocityXQ8 = lVelocityXQ8;
    pGame->lVelocityYQ8 = lVelocityYQ8;
    pGame->usRoundElapsedMs = 0U;
    pGame->eState = MOMO_PLAY_BALL_STATE_MOVING;
}

static void TEST_ExactBoundaryBounce(void)
{
    MOMO_PLAY_BALL tGame;
    MOMO_PLAY_BALL_BOUNDS tBounds;

    tBounds = TEST_Bounds();
    tBounds.sMomoMinX = tBounds.sMomoMaxX = tBounds.sMomoStartX =
        tBounds.sBallMaxX;
    tBounds.sMomoMinY = tBounds.sMomoMaxY = tBounds.sMomoStartY =
        tBounds.sBallMaxY;
    tBounds.ucCollisionX = 1U;
    tBounds.ucCollisionY = 1U;
    MOMOPLAYBALL_Init(&tGame, &tBounds);
    TEST_SetMoving(&tGame, (int16_t)(tBounds.sBallMinX + 1),
                   tBounds.sBallMinY, -TEST_MAX_SPEED_Q8, 0L);
    assert(MOMO_PLAY_BALL_EVENT_POSITION_CHANGED ==
           MOMOPLAYBALL_Update(&tGame, 50U));
    assert((tBounds.sBallMinX == tGame.sBallX) &&
           (0L < tGame.lVelocityXQ8));

    MOMOPLAYBALL_Init(&tGame, &tBounds);
    TEST_SetMoving(&tGame, (int16_t)(tBounds.sBallMaxX - 1),
                   tBounds.sBallMinY, TEST_MAX_SPEED_Q8, 0L);
    assert(MOMO_PLAY_BALL_EVENT_POSITION_CHANGED ==
           MOMOPLAYBALL_Update(&tGame, 50U));
    assert((tBounds.sBallMaxX == tGame.sBallX) &&
           (0L > tGame.lVelocityXQ8));

    MOMOPLAYBALL_Init(&tGame, &tBounds);
    TEST_SetMoving(&tGame, tBounds.sBallMinX,
                   (int16_t)(tBounds.sBallMinY + 1),
                   0L, -TEST_MAX_SPEED_Q8);
    assert(MOMO_PLAY_BALL_EVENT_POSITION_CHANGED ==
           MOMOPLAYBALL_Update(&tGame, 50U));
    assert((tBounds.sBallMinY == tGame.sBallY) &&
           (0L < tGame.lVelocityYQ8));

    MOMOPLAYBALL_Init(&tGame, &tBounds);
    TEST_SetMoving(&tGame, tBounds.sBallMinX,
                   (int16_t)(tBounds.sBallMaxY - 1),
                   0L, TEST_MAX_SPEED_Q8);
    assert(MOMO_PLAY_BALL_EVENT_POSITION_CHANGED ==
           MOMOPLAYBALL_Update(&tGame, 50U));
    assert((tBounds.sBallMaxY == tGame.sBallY) &&
           (0L > tGame.lVelocityYQ8));
}

static void TEST_TimeoutAndFeedback(void)
{
    MOMO_PLAY_BALL tGame;
    MOMO_PLAY_BALL_BOUNDS tBounds;
    MOMO_PLAY_BALL_EVENT eEvent;
    uint8_t ucStep;

    tBounds = TEST_Bounds();
    tBounds.sMomoMinX = tBounds.sMomoMaxX = tBounds.sMomoStartX =
        tBounds.sBallMaxX;
    tBounds.sMomoMinY = tBounds.sMomoMaxY = tBounds.sMomoStartY =
        tBounds.sBallMaxY;
    tBounds.ucCollisionX = 1U;
    tBounds.ucCollisionY = 1U;
    MOMOPLAYBALL_Init(&tGame, &tBounds);
    TEST_SetMoving(&tGame, tBounds.sBallMinX, tBounds.sBallMinY,
                   TEST_MAX_SPEED_Q8, 0L);
    for (ucStep = 0U; ucStep < 49U; ucStep++)
    {
        tGame.lVelocityXQ8 = TEST_MAX_SPEED_Q8;
        assert(MOMO_PLAY_BALL_EVENT_POSITION_CHANGED ==
               MOMOPLAYBALL_Update(&tGame, 100U));
    }
    tGame.lVelocityXQ8 = TEST_MAX_SPEED_Q8;
    assert(MOMO_PLAY_BALL_EVENT_RESET == MOMOPLAYBALL_Update(&tGame, 100U));
    assert(MOMO_PLAY_BALL_STATE_IDLE == tGame.eState);

    tBounds = TEST_Bounds();
    tBounds.sMomoMinX = tBounds.sMomoMaxX = tBounds.sMomoStartX =
        tBounds.sBallStartX;
    tBounds.sMomoMinY = tBounds.sMomoMaxY = tBounds.sMomoStartY =
        tBounds.sBallStartY;
    MOMOPLAYBALL_Init(&tGame, &tBounds);
    TEST_SetMoving(&tGame, tBounds.sBallStartX, tBounds.sBallStartY, 0L, 0L);
    assert(MOMO_PLAY_BALL_EVENT_CAUGHT == MOMOPLAYBALL_Update(&tGame, 50U));
    for (ucStep = 0U; ucStep < 13U; ucStep++)
    {
        eEvent = MOMOPLAYBALL_Update(&tGame, 50U);
        assert(MOMO_PLAY_BALL_EVENT_NONE == eEvent);
        assert(MOMO_PLAY_BALL_STATE_FEEDBACK == tGame.eState);
    }
    assert(MOMO_PLAY_BALL_EVENT_RESET == MOMOPLAYBALL_Update(&tGame, 50U));
    assert(MOMO_PLAY_BALL_STATE_IDLE == tGame.eState);
}

static void TEST_TwoHundredRounds(void)
{
    MOMO_PLAY_BALL tGame;
    MOMO_PLAY_BALL_BOUNDS tBounds;
    uint32_t ulRandom = 0xC0DEC0DEUL;
    uint32_t ulTick = 0xFFFFFF00UL;
    uint16_t usRound;
    uint16_t usStep;
    int16_t sDragX;
    int16_t sDragY;
    tBounds = TEST_Bounds();
    MOMOPLAYBALL_Init(&tGame, &tBounds);
    for (usRound = 0U; usRound < 200U; usRound++)
    {
        assert(MOMO_PLAY_BALL_STATE_IDLE == tGame.eState);
        assert(MOMOPLAYBALL_Press(&tGame, tGame.sBallX, tGame.sBallY, ulTick));
        ulTick += 20U;
        sDragX = (int16_t)(tBounds.sBallMinX + (TEST_Random(&ulRandom) %
            (uint32_t)(tBounds.sBallMaxX - tBounds.sBallMinX + 1)));
        sDragY = (int16_t)(tBounds.sBallMinY + (TEST_Random(&ulRandom) %
            (uint32_t)(tBounds.sBallMaxY - tBounds.sBallMinY + 1)));
        assert(MOMOPLAYBALL_Drag(&tGame, sDragX, sDragY, ulTick));
        ulTick += 20U;
        sDragX = (int16_t)(sDragX + (int16_t)(TEST_Random(&ulRandom) % 41U) - 20);
        sDragY = (int16_t)(sDragY + (int16_t)(TEST_Random(&ulRandom) % 41U) - 20);
        (void)MOMOPLAYBALL_Release(&tGame, sDragX, sDragY, ulTick);
        for (usStep = 0U; usStep < 140U; usStep++)
        {
            (void)MOMOPLAYBALL_Update(&tGame, 50U);
            assert((tBounds.sBallMinX <= tGame.sBallX) &&
                   (tGame.sBallX <= tBounds.sBallMaxX));
            assert((tBounds.sBallMinY <= tGame.sBallY) &&
                   (tGame.sBallY <= tBounds.sBallMaxY));
            assert((tBounds.sMomoMinX <= tGame.sMomoX) &&
                   (tGame.sMomoX <= tBounds.sMomoMaxX));
            assert((tBounds.sMomoMinY <= tGame.sMomoY) &&
                   (tGame.sMomoY <= tBounds.sMomoMaxY));
            if (MOMO_PLAY_BALL_STATE_IDLE == tGame.eState)
            {
                break;
            }
        }
        assert(MOMO_PLAY_BALL_STATE_IDLE == tGame.eState);
        ulTick += 7000U;
    }
}

int main(void)
{
    TEST_Basics();
    TEST_VelocityFreshness();
    TEST_ExactBoundaryBounce();
    TEST_TimeoutAndFeedback();
    TEST_TwoHundredRounds();
    printf("PASS momo_play_ball_host_test state=%u bytes deterministic=5 rounds=200\n",
           (unsigned int)sizeof(MOMO_PLAY_BALL));
    return 0;
}
