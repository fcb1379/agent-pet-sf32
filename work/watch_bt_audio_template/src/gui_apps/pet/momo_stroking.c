#include "momo_stroking.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MOMO_STROKE_DEFAULT_STAGE_WIDTH        (192U)
#define MOMO_STROKE_DEFAULT_STAGE_HEIGHT       (192U)
#define MOMO_STROKE_DEFAULT_HEAD_LEFT          (38U)
#define MOMO_STROKE_DEFAULT_HEAD_RIGHT         (154U)
#define MOMO_STROKE_DEFAULT_HEAD_TOP           (10U)
#define MOMO_STROKE_DEFAULT_HEAD_BOTTOM        (96U)
#define MOMO_STROKE_DEFAULT_SAMPLE_MS          (100U)
#define MOMO_STROKE_DEFAULT_DWELL_MS           (650U)
#define MOMO_STROKE_DEFAULT_GENTLE_MS          (350U)
#define MOMO_STROKE_DEFAULT_RELEASE_MS         (150U)
#define MOMO_STROKE_DEFAULT_LEAVE_MS           (250U)
#define MOMO_STROKE_DEFAULT_STATIONARY_PX      (12U)
#define MOMO_STROKE_DEFAULT_GENTLE_MIN_PX      (12U)
#define MOMO_STROKE_DEFAULT_GENTLE_MAX_PX      (160U)
#define MOMO_STROKE_DEFAULT_MAX_STEP_PX        (80U)
#define MOMO_STROKE_DEFAULT_INSIDE_PERCENT     (70U)

_Static_assert(
    sizeof(MOMO_STROKE_CONTEXT) <= 256U,
    "MOMO stroke context exceeds the fixed RAM budget");

static bool Local_ConfigValid(const MOMO_STROKE_CONFIG *pConfig)
{
    if ((NULL == pConfig) ||
        (0U == pConfig->usStageWidth) ||
        (0U == pConfig->usStageHeight) ||
        (pConfig->usStageWidth <= pConfig->usHeadRight) ||
        (pConfig->usStageHeight <= pConfig->usHeadBottom) ||
        (pConfig->usHeadRight < pConfig->usHeadLeft) ||
        (pConfig->usHeadBottom < pConfig->usHeadTop) ||
        (0U == pConfig->usSampleMs) ||
        (0U == pConfig->usDwellMs) ||
        (0U == pConfig->usGentleMs) ||
        (0U == pConfig->usReleaseMs) ||
        (0U == pConfig->usLeaveMs) ||
        (pConfig->usGentleMaxDistance < pConfig->usGentleMinDistance) ||
        (100U < pConfig->ucInsidePercent))
    {
        return false;
    }

    return true;
}

static bool Local_InStage(
    const MOMO_STROKE_CONFIG *pConfig,
    int16_t sX,
    int16_t sY)
{
    return (0 <= sX) && (0 <= sY) &&
        ((uint16_t)sX < pConfig->usStageWidth) &&
        ((uint16_t)sY < pConfig->usStageHeight);
}

static bool Local_InHead(
    const MOMO_STROKE_CONFIG *pConfig,
    int16_t sX,
    int16_t sY)
{
    return Local_InStage(pConfig, sX, sY) &&
        (pConfig->usHeadLeft <= (uint16_t)sX) &&
        ((uint16_t)sX <= pConfig->usHeadRight) &&
        (pConfig->usHeadTop <= (uint16_t)sY) &&
        ((uint16_t)sY <= pConfig->usHeadBottom);
}

static uint32_t Local_AbsoluteDifference(int16_t sLeft, int16_t sRight)
{
    int32_t lDifference;

    lDifference = (int32_t)sLeft - (int32_t)sRight;
    if (0 > lDifference)
    {
        lDifference = -lDifference;
    }

    return (uint32_t)lDifference;
}

static uint32_t Local_SaturatingAdd(uint32_t ulLeft, uint32_t ulRight)
{
    if ((UINT32_MAX - ulLeft) < ulRight)
    {
        return UINT32_MAX;
    }

    return ulLeft + ulRight;
}

static uint16_t Local_SaturatingIncrement(uint16_t usValue)
{
    return (UINT16_MAX == usValue) ? UINT16_MAX : (uint16_t)(usValue + 1U);
}

static bool Local_InsideRatioSatisfied(
    const MOMO_STROKE_CONTEXT *pContext,
    const MOMO_STROKE_CONFIG *pConfig)
{
    uint32_t ulInsideScaled;
    uint32_t ulRequiredScaled;

    if (0U == pContext->usSampleCount)
    {
        return false;
    }
    ulInsideScaled = (uint32_t)pContext->usInsideCount * 100U;
    ulRequiredScaled = (uint32_t)pContext->usSampleCount *
        pConfig->ucInsidePercent;

    return ulRequiredScaled <= ulInsideScaled;
}

static MOMO_STROKE_EVENT Local_End(
    MOMO_STROKE_CONTEXT *pContext,
    bool bWasConsumed)
{
    pContext->eState = MOMO_STROKE_STATE_IDLE;
    pContext->bConsumed = bWasConsumed;
    pContext->bOutside = false;

    return bWasConsumed ? MOMO_STROKE_EVENT_ENDED :
        MOMO_STROKE_EVENT_CANCELLED;
}

static MOMO_STROKE_EVENT Local_EvaluateCandidate(
    MOMO_STROKE_CONTEXT *pContext,
    const MOMO_STROKE_CONFIG *pConfig,
    uint32_t ulNow)
{
    uint32_t ulElapsed;
    bool bStationary;
    bool bGentle;

    ulElapsed = (uint32_t)(ulNow - pContext->ulStartTick);
    bStationary = (pConfig->usDwellMs <= ulElapsed) &&
        (pContext->ulTotalDistance <= pConfig->usStationaryDistance) &&
        !pContext->bOutside;
    bGentle = (pConfig->usGentleMs <= ulElapsed) &&
        (pConfig->usGentleMinDistance <= pContext->ulTotalDistance) &&
        (pContext->ulTotalDistance <= pConfig->usGentleMaxDistance) &&
        !pContext->bOutside &&
        Local_InsideRatioSatisfied(pContext, pConfig);
    if (bStationary || bGentle)
    {
        pContext->eState = MOMO_STROKE_STATE_STROKING;
        pContext->bConsumed = true;
        return MOMO_STROKE_EVENT_STARTED;
    }

    return MOMO_STROKE_EVENT_NONE;
}

void MOMOSTROKE_GetDefaultConfig(MOMO_STROKE_CONFIG *pConfig)
{
    if (NULL == pConfig)
    {
        return;
    }

    (void)memset(pConfig, 0, sizeof(*pConfig));
    pConfig->usStageWidth = MOMO_STROKE_DEFAULT_STAGE_WIDTH;
    pConfig->usStageHeight = MOMO_STROKE_DEFAULT_STAGE_HEIGHT;
    pConfig->usHeadLeft = MOMO_STROKE_DEFAULT_HEAD_LEFT;
    pConfig->usHeadRight = MOMO_STROKE_DEFAULT_HEAD_RIGHT;
    pConfig->usHeadTop = MOMO_STROKE_DEFAULT_HEAD_TOP;
    pConfig->usHeadBottom = MOMO_STROKE_DEFAULT_HEAD_BOTTOM;
    pConfig->usSampleMs = MOMO_STROKE_DEFAULT_SAMPLE_MS;
    pConfig->usDwellMs = MOMO_STROKE_DEFAULT_DWELL_MS;
    pConfig->usGentleMs = MOMO_STROKE_DEFAULT_GENTLE_MS;
    pConfig->usReleaseMs = MOMO_STROKE_DEFAULT_RELEASE_MS;
    pConfig->usLeaveMs = MOMO_STROKE_DEFAULT_LEAVE_MS;
    pConfig->usStationaryDistance = MOMO_STROKE_DEFAULT_STATIONARY_PX;
    pConfig->usGentleMinDistance = MOMO_STROKE_DEFAULT_GENTLE_MIN_PX;
    pConfig->usGentleMaxDistance = MOMO_STROKE_DEFAULT_GENTLE_MAX_PX;
    pConfig->usMaxStepDistance = MOMO_STROKE_DEFAULT_MAX_STEP_PX;
    pConfig->ucInsidePercent = MOMO_STROKE_DEFAULT_INSIDE_PERCENT;
}

void MOMOSTROKE_Init(MOMO_STROKE_CONTEXT *pContext)
{
    if (NULL != pContext)
    {
        (void)memset(pContext, 0, sizeof(*pContext));
        pContext->eState = MOMO_STROKE_STATE_IDLE;
    }
}

bool MOMOSTROKE_Press(
    MOMO_STROKE_CONTEXT *pContext,
    const MOMO_STROKE_CONFIG *pConfig,
    int16_t sX,
    int16_t sY,
    uint32_t ulNow)
{
    if ((NULL == pContext) || !Local_ConfigValid(pConfig))
    {
        return false;
    }

    MOMOSTROKE_Init(pContext);
    if (!Local_InHead(pConfig, sX, sY))
    {
        return false;
    }
    pContext->eState = MOMO_STROKE_STATE_CANDIDATE;
    pContext->sLastX = sX;
    pContext->sLastY = sY;
    pContext->ulStartTick = ulNow;
    pContext->ulSampleTick = ulNow;
    pContext->usSampleCount = 1U;
    pContext->usInsideCount = 1U;

    return true;
}

MOMO_STROKE_EVENT MOMOSTROKE_Sample(
    MOMO_STROKE_CONTEXT *pContext,
    const MOMO_STROKE_CONFIG *pConfig,
    int16_t sX,
    int16_t sY,
    uint32_t ulNow)
{
    uint32_t ulStepDistance;
    bool bInsideHead;

    if ((NULL == pContext) || !Local_ConfigValid(pConfig) ||
        ((MOMO_STROKE_STATE_CANDIDATE != pContext->eState) &&
         (MOMO_STROKE_STATE_STROKING != pContext->eState)))
    {
        return MOMO_STROKE_EVENT_NONE;
    }
    if (!Local_InStage(pConfig, sX, sY))
    {
        return Local_End(
            pContext,
            MOMO_STROKE_STATE_STROKING == pContext->eState);
    }
    if (pConfig->usSampleMs >
        (uint32_t)(ulNow - pContext->ulSampleTick))
    {
        return MOMOSTROKE_Poll(pContext, pConfig, ulNow);
    }

    ulStepDistance = Local_AbsoluteDifference(sX, pContext->sLastX) +
        Local_AbsoluteDifference(sY, pContext->sLastY);
    pContext->sLastX = sX;
    pContext->sLastY = sY;
    pContext->ulSampleTick = ulNow;
    pContext->ulTotalDistance = Local_SaturatingAdd(
        pContext->ulTotalDistance,
        ulStepDistance);
    pContext->usSampleCount = Local_SaturatingIncrement(
        pContext->usSampleCount);
    bInsideHead = Local_InHead(pConfig, sX, sY);
    if (bInsideHead)
    {
        pContext->usInsideCount = Local_SaturatingIncrement(
            pContext->usInsideCount);
        pContext->bOutside = false;
    }
    else if (!pContext->bOutside)
    {
        pContext->bOutside = true;
        pContext->ulOutsideTick = ulNow;
    }

    if ((MOMO_STROKE_STATE_CANDIDATE == pContext->eState) &&
        ((pConfig->usMaxStepDistance < ulStepDistance) ||
         (pConfig->usGentleMaxDistance < pContext->ulTotalDistance)))
    {
        return Local_End(pContext, false);
    }
    if ((MOMO_STROKE_STATE_STROKING == pContext->eState) &&
        (pConfig->usMaxStepDistance < ulStepDistance))
    {
        return Local_End(pContext, true);
    }

    return MOMOSTROKE_Poll(pContext, pConfig, ulNow);
}

MOMO_STROKE_EVENT MOMOSTROKE_Release(
    MOMO_STROKE_CONTEXT *pContext,
    uint32_t ulNow)
{
    if (NULL == pContext)
    {
        return MOMO_STROKE_EVENT_NONE;
    }
    if (MOMO_STROKE_STATE_CANDIDATE == pContext->eState)
    {
        (void)Local_End(pContext, false);
        return MOMO_STROKE_EVENT_NONE;
    }
    if (MOMO_STROKE_STATE_STROKING == pContext->eState)
    {
        pContext->eState = MOMO_STROKE_STATE_RELEASING;
        pContext->ulReleaseTick = ulNow;
        pContext->bConsumed = true;
    }

    return MOMO_STROKE_EVENT_NONE;
}

MOMO_STROKE_EVENT MOMOSTROKE_Poll(
    MOMO_STROKE_CONTEXT *pContext,
    const MOMO_STROKE_CONFIG *pConfig,
    uint32_t ulNow)
{
    if ((NULL == pContext) || !Local_ConfigValid(pConfig))
    {
        return MOMO_STROKE_EVENT_NONE;
    }
    if (MOMO_STROKE_STATE_CANDIDATE == pContext->eState)
    {
        return Local_EvaluateCandidate(pContext, pConfig, ulNow);
    }
    if ((MOMO_STROKE_STATE_STROKING == pContext->eState) &&
        pContext->bOutside &&
        (pConfig->usLeaveMs <=
         (uint32_t)(ulNow - pContext->ulOutsideTick)))
    {
        return Local_End(pContext, true);
    }
    if ((MOMO_STROKE_STATE_RELEASING == pContext->eState) &&
        (pConfig->usReleaseMs <=
         (uint32_t)(ulNow - pContext->ulReleaseTick)))
    {
        return Local_End(pContext, true);
    }

    return MOMO_STROKE_EVENT_NONE;
}

MOMO_STROKE_EVENT MOMOSTROKE_Cancel(MOMO_STROKE_CONTEXT *pContext)
{
    bool bWasConsumed;

    if (NULL == pContext)
    {
        return MOMO_STROKE_EVENT_NONE;
    }
    if (MOMO_STROKE_STATE_IDLE == pContext->eState)
    {
        return MOMO_STROKE_EVENT_NONE;
    }
    bWasConsumed = (MOMO_STROKE_STATE_STROKING == pContext->eState) ||
        (MOMO_STROKE_STATE_RELEASING == pContext->eState) ||
        pContext->bConsumed;

    return Local_End(pContext, bWasConsumed);
}

MOMO_STROKE_EVENT MOMOSTROKE_Preempt(MOMO_STROKE_CONTEXT *pContext)
{
    if ((NULL == pContext) ||
        (MOMO_STROKE_STATE_IDLE == pContext->eState))
    {
        return MOMO_STROKE_EVENT_NONE;
    }

    return Local_End(pContext, true);
}

MOMO_STROKE_STATE MOMOSTROKE_GetState(const MOMO_STROKE_CONTEXT *pContext)
{
    return (NULL == pContext) ? MOMO_STROKE_STATE_IDLE : pContext->eState;
}

bool MOMOSTROKE_IsConsumed(const MOMO_STROKE_CONTEXT *pContext)
{
    return (NULL != pContext) && pContext->bConsumed;
}
