#ifndef MOMO_PLAY_BALL_H
#define MOMO_PLAY_BALL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum _MOMO_PLAY_BALL_STATE
{
    MOMO_PLAY_BALL_STATE_DISABLED = 0,
    MOMO_PLAY_BALL_STATE_IDLE,
    MOMO_PLAY_BALL_STATE_DRAGGING,
    MOMO_PLAY_BALL_STATE_MOVING,
    MOMO_PLAY_BALL_STATE_FEEDBACK
} MOMO_PLAY_BALL_STATE;

typedef enum _MOMO_PLAY_BALL_EVENT
{
    MOMO_PLAY_BALL_EVENT_NONE = 0,
    MOMO_PLAY_BALL_EVENT_POSITION_CHANGED,
    MOMO_PLAY_BALL_EVENT_CAUGHT,
    MOMO_PLAY_BALL_EVENT_RESET
} MOMO_PLAY_BALL_EVENT;

/* MOMO_PLAY_BALL_BOUNDS: 球和Momo中心点的有界活动区域。 */
typedef struct _MOMO_PLAY_BALL_BOUNDS
{
    int16_t sBallMinX;
    int16_t sBallMaxX;
    int16_t sBallMinY;
    int16_t sBallMaxY;
    int16_t sMomoMinX;
    int16_t sMomoMaxX;
    int16_t sMomoMinY;
    int16_t sMomoMaxY;
    int16_t sBallStartX;
    int16_t sBallStartY;
    int16_t sMomoStartX;
    int16_t sMomoStartY;
    uint8_t ucHitRadius;
    uint8_t ucCollisionX;
    uint8_t ucCollisionY;
} MOMO_PLAY_BALL_BOUNDS;

/* MOMO_PLAY_BALL: 固定内存状态机；不持有LVGL、RTOS或动态内存。 */
typedef struct _MOMO_PLAY_BALL
{
    MOMO_PLAY_BALL_BOUNDS tBounds;
    MOMO_PLAY_BALL_STATE eState;
    int32_t lBallXQ8;
    int32_t lBallYQ8;
    int32_t lVelocityXQ8;
    int32_t lVelocityYQ8;
    int32_t lReleaseVelocityXQ8;
    int32_t lReleaseVelocityYQ8;
    int16_t sBallX;
    int16_t sBallY;
    int16_t sMomoX;
    int16_t sMomoY;
    int16_t sLastTouchX;
    int16_t sLastTouchY;
    uint32_t ulLastTouchTickMs;
    uint32_t ulVelocitySampleTickMs;
    uint16_t usRoundElapsedMs;
    uint16_t usFeedbackElapsedMs;
    bool bHasVelocitySample;
    bool bCollisionLatched;
} MOMO_PLAY_BALL;

void MOMOPLAYBALL_Init(MOMO_PLAY_BALL *pGame,
                       const MOMO_PLAY_BALL_BOUNDS *pBounds);
bool MOMOPLAYBALL_Press(MOMO_PLAY_BALL *pGame, int16_t sTouchX,
                        int16_t sTouchY, uint32_t ulTickMs);
bool MOMOPLAYBALL_Drag(MOMO_PLAY_BALL *pGame, int16_t sTouchX,
                       int16_t sTouchY, uint32_t ulTickMs);
bool MOMOPLAYBALL_Release(MOMO_PLAY_BALL *pGame, int16_t sTouchX,
                          int16_t sTouchY, uint32_t ulTickMs);
void MOMOPLAYBALL_Cancel(MOMO_PLAY_BALL *pGame);
MOMO_PLAY_BALL_EVENT MOMOPLAYBALL_Update(MOMO_PLAY_BALL *pGame,
                                         uint16_t usDeltaMs);

#endif /* MOMO_PLAY_BALL_H */
