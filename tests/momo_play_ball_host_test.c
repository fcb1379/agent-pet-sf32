#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "momo_play_ball.h"

static MOMO_PLAY_BALL_BOUNDS TEST_Bounds(void)
{
    MOMO_PLAY_BALL_BOUNDS tBounds = {
        37, 353, 61, 353, 96, 294, 96, 354,
        320, 150, 195, 213, 24U, 42U, 42U
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
    assert(MOMOPLAYBALL_Press(&tGame, 320, 150, 0xFFFFFFF0UL));
    assert(MOMOPLAYBALL_Drag(&tGame, 300, 140, 4U));
    assert(MOMOPLAYBALL_Release(&tGame, 280, 130, 24U));
    assert(MOMO_PLAY_BALL_STATE_MOVING == tGame.eState);
    assert(0L > tGame.lVelocityXQ8);
    MOMOPLAYBALL_Cancel(&tGame);
    assert((MOMO_PLAY_BALL_STATE_IDLE == tGame.eState) && (320 == tGame.sBallX));

    tBounds.sBallMaxX = tBounds.sBallMinX;
    MOMOPLAYBALL_Init(&tGame, &tBounds);
    assert(MOMO_PLAY_BALL_STATE_DISABLED == tGame.eState);
    tBounds = TEST_Bounds();
    MOMOPLAYBALL_Init(&tGame, &tBounds);
    assert(MOMOPLAYBALL_Press(&tGame, 320, 150, 10U));
    assert(!MOMOPLAYBALL_Release(&tGame, 320, 150, 20U));
    assert(MOMO_PLAY_BALL_EVENT_NONE == MOMOPLAYBALL_Update(NULL, 50U));
}

static void TEST_TwoHundredRounds(void)
{
    MOMO_PLAY_BALL tGame;
    MOMO_PLAY_BALL_BOUNDS tBounds;
    uint32_t ulRandom = 0xC0DEC0DEUL;
    uint32_t ulTick = 0xFFFFFF00UL;
    uint16_t usRound;
    uint16_t usStep;
    uint16_t usCaughtCount = 0U;
    uint16_t usResetCount = 0U;
    int16_t sDragX;
    int16_t sDragY;
    MOMO_PLAY_BALL_EVENT eEvent;

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
            eEvent = MOMOPLAYBALL_Update(&tGame, 50U);
            if (MOMO_PLAY_BALL_EVENT_CAUGHT == eEvent)
            {
                usCaughtCount++;
            }
            else if (MOMO_PLAY_BALL_EVENT_RESET == eEvent)
            {
                usResetCount++;
            }
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
    assert(0U < usCaughtCount);
    assert(0U < usResetCount);
}

int main(void)
{
    TEST_Basics();
    TEST_TwoHundredRounds();
    printf("PASS momo_play_ball_host_test state=%u bytes rounds=200\n",
           (unsigned int)sizeof(MOMO_PLAY_BALL));
    return 0;
}
