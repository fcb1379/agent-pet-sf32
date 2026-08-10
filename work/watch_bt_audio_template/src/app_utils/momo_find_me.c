#include "momo_find_me.h"

#include <rthw.h>
#include <string.h>

typedef struct _MOMO_FIND_ENV
{
    MOMO_FIND_SNAPSHOT tSnapshot;
    MOMO_FIND_END_EVENT tPendingEvent;
    uint16_t usLastCompletedSessionId;
    uint32_t ulLastEndTick;
    uint32_t ulCooldownTicks;
    bool bHasCompletedSession;
    bool bEndEventPending;
} MOMO_FIND_ENV;

static MOMO_FIND_ENV l_tFindEnv;

static void Local_IncrementSaturated(uint32_t *pValue)
{
    if ((NULL != pValue) && (UINT32_MAX != *pValue))
    {
        (*pValue)++;
    }

    return;
}

static void Local_BeginStopping(MOMO_FIND_END_REASON eReason)
{
    if ((MOMO_FIND_STATE_ACTIVE == l_tFindEnv.tSnapshot.eState) ||
        (MOMO_FIND_STATE_ARMING == l_tFindEnv.tSnapshot.eState))
    {
        l_tFindEnv.tSnapshot.eState = MOMO_FIND_STATE_STOPPING;
        l_tFindEnv.tSnapshot.eEndReason = eReason;
        l_tFindEnv.tSnapshot.ulGeneration++;
        Local_IncrementSaturated(&l_tFindEnv.tSnapshot.ulStopCount);
    }

    return;
}

void MOMOFIND_Init(void)
{
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    (void)memset(&l_tFindEnv, 0, sizeof(l_tFindEnv));
    l_tFindEnv.tSnapshot.eState = MOMO_FIND_STATE_IDLE;
    rt_hw_interrupt_enable(tLevel);

    return;
}

MOMO_FIND_RESULT MOMOFIND_StartAt(uint16_t usRequestId,
                                  uint32_t ulNowTick,
                                  uint32_t ulDurationTicks,
                                  uint32_t ulCooldownTicks,
                                  bool bAlarmBusy,
                                  bool bTransferBusy,
                                  bool *pNeedsWake)
{
    MOMO_FIND_RESULT eResult;
    rt_base_t tLevel;

    if ((0U == usRequestId) || (0U == ulDurationTicks) || (NULL == pNeedsWake))
    {
        return MOMO_FIND_RESULT_INVALID;
    }

    *pNeedsWake = false;
    tLevel = rt_hw_interrupt_disable();
    if ((MOMO_FIND_STATE_ACTIVE == l_tFindEnv.tSnapshot.eState) ||
        (MOMO_FIND_STATE_ARMING == l_tFindEnv.tSnapshot.eState))
    {
        if (usRequestId == l_tFindEnv.tSnapshot.usSessionId)
        {
            Local_IncrementSaturated(&l_tFindEnv.tSnapshot.ulDuplicateCount);
            eResult = MOMO_FIND_RESULT_ACCEPTED;
        }
        else
        {
            eResult = MOMO_FIND_RESULT_ALREADY_ACTIVE;
        }
    }
    else if (MOMO_FIND_STATE_STOPPING == l_tFindEnv.tSnapshot.eState)
    {
        eResult = MOMO_FIND_RESULT_ALREADY_ACTIVE;
    }
    else if (l_tFindEnv.bHasCompletedSession &&
             (usRequestId == l_tFindEnv.usLastCompletedSessionId))
    {
        Local_IncrementSaturated(&l_tFindEnv.tSnapshot.ulDuplicateCount);
        eResult = MOMO_FIND_RESULT_STALE;
    }
    else if (l_tFindEnv.bEndEventPending)
    {
        /* 保留唯一结束事件，通知层确认发送前不允许新会话覆盖。 */
        eResult = MOMO_FIND_RESULT_RATE_LIMIT;
    }
    else if (bAlarmBusy)
    {
        Local_IncrementSaturated(&l_tFindEnv.tSnapshot.ulBusyCount);
        eResult = MOMO_FIND_RESULT_BUSY_ALARM;
    }
    else if (bTransferBusy)
    {
        Local_IncrementSaturated(&l_tFindEnv.tSnapshot.ulBusyCount);
        eResult = MOMO_FIND_RESULT_BUSY_TRANSFER;
    }
    else if (l_tFindEnv.bHasCompletedSession &&
             ((uint32_t)(ulNowTick - l_tFindEnv.ulLastEndTick) <
              l_tFindEnv.ulCooldownTicks))
    {
        eResult = MOMO_FIND_RESULT_RATE_LIMIT;
    }
    else
    {
        l_tFindEnv.tSnapshot.eState = MOMO_FIND_STATE_ARMING;
        l_tFindEnv.tSnapshot.eEndReason = MOMO_FIND_END_NONE;
        l_tFindEnv.tSnapshot.usSessionId = usRequestId;
        l_tFindEnv.tSnapshot.ulStartTick = ulNowTick;
        l_tFindEnv.tSnapshot.ulDurationTicks = ulDurationTicks;
        l_tFindEnv.ulCooldownTicks = ulCooldownTicks;
        l_tFindEnv.tSnapshot.ulGeneration++;
        Local_IncrementSaturated(&l_tFindEnv.tSnapshot.ulAcceptedCount);
        *pNeedsWake = true;
        eResult = MOMO_FIND_RESULT_ACCEPTED;
    }
    rt_hw_interrupt_enable(tLevel);

    return eResult;
}

bool MOMOFIND_ConfirmStart(uint16_t usRequestId)
{
    bool bConfirmed;
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    bConfirmed = (MOMO_FIND_STATE_ARMING == l_tFindEnv.tSnapshot.eState) &&
                 (usRequestId == l_tFindEnv.tSnapshot.usSessionId);
    if (bConfirmed)
    {
        l_tFindEnv.tSnapshot.eState = MOMO_FIND_STATE_ACTIVE;
        l_tFindEnv.tSnapshot.ulGeneration++;
    }
    rt_hw_interrupt_enable(tLevel);

    return bConfirmed;
}

void MOMOFIND_CancelArming(uint16_t usRequestId)
{
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    if ((MOMO_FIND_STATE_ARMING == l_tFindEnv.tSnapshot.eState) &&
        (usRequestId == l_tFindEnv.tSnapshot.usSessionId))
    {
        l_tFindEnv.tSnapshot.eState = MOMO_FIND_STATE_IDLE;
        l_tFindEnv.tSnapshot.eEndReason = MOMO_FIND_END_NONE;
        l_tFindEnv.tSnapshot.usSessionId = 0U;
        l_tFindEnv.tSnapshot.ulStartTick = 0U;
        l_tFindEnv.tSnapshot.ulDurationTicks = 0U;
        l_tFindEnv.tSnapshot.ulGeneration++;
    }
    rt_hw_interrupt_enable(tLevel);

    return;
}

MOMO_FIND_RESULT MOMOFIND_StopAt(MOMO_FIND_END_REASON eReason,
                                 uint32_t ulNowTick)
{
    MOMO_FIND_RESULT eResult;
    rt_base_t tLevel;

    (void)ulNowTick;
    if ((MOMO_FIND_END_USER_STOP != eReason) &&
        (MOMO_FIND_END_PHONE_STOP != eReason))
    {
        return MOMO_FIND_RESULT_INVALID;
    }

    tLevel = rt_hw_interrupt_disable();
    if (MOMO_FIND_STATE_ACTIVE == l_tFindEnv.tSnapshot.eState)
    {
        Local_BeginStopping(eReason);
        eResult = MOMO_FIND_RESULT_STOPPING;
    }
    else if (MOMO_FIND_STATE_STOPPING == l_tFindEnv.tSnapshot.eState)
    {
        eResult = MOMO_FIND_RESULT_STOPPING;
    }
    else
    {
        eResult = MOMO_FIND_RESULT_IDLE;
    }
    rt_hw_interrupt_enable(tLevel);

    return eResult;
}

MOMO_FIND_RESULT MOMOFIND_StopSessionAt(uint16_t usRequestId,
                                        MOMO_FIND_END_REASON eReason,
                                        uint32_t ulNowTick)
{
    MOMO_FIND_SNAPSHOT tSnapshot;

    if ((0U == usRequestId) || !MOMOFIND_GetSnapshot(&tSnapshot))
    {
        return MOMO_FIND_RESULT_INVALID;
    }
    if (((MOMO_FIND_STATE_ACTIVE == tSnapshot.eState) ||
         (MOMO_FIND_STATE_STOPPING == tSnapshot.eState)) &&
        (usRequestId != tSnapshot.usSessionId))
    {
        return MOMO_FIND_RESULT_STALE;
    }

    return MOMOFIND_StopAt(eReason, ulNowTick);
}

void MOMOFIND_PreemptAlarm(uint32_t ulNowTick)
{
    rt_base_t tLevel;

    (void)ulNowTick;
    tLevel = rt_hw_interrupt_disable();
    Local_BeginStopping(MOMO_FIND_END_PREEMPTED_ALARM);
    rt_hw_interrupt_enable(tLevel);

    return;
}

void MOMOFIND_PollAt(uint32_t ulNowTick)
{
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    if ((MOMO_FIND_STATE_ACTIVE == l_tFindEnv.tSnapshot.eState) &&
        ((uint32_t)(ulNowTick - l_tFindEnv.tSnapshot.ulStartTick) >=
         l_tFindEnv.tSnapshot.ulDurationTicks))
    {
        Local_BeginStopping(MOMO_FIND_END_TIMEOUT);
    }
    rt_hw_interrupt_enable(tLevel);

    return;
}

bool MOMOFIND_AcknowledgeUiStopped(uint32_t ulNowTick)
{
    bool bAcknowledged;
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    bAcknowledged = (MOMO_FIND_STATE_STOPPING == l_tFindEnv.tSnapshot.eState);
    if (bAcknowledged)
    {
        l_tFindEnv.tPendingEvent.usSessionId = l_tFindEnv.tSnapshot.usSessionId;
        l_tFindEnv.tPendingEvent.eReason = l_tFindEnv.tSnapshot.eEndReason;
        l_tFindEnv.bEndEventPending = true;
        l_tFindEnv.usLastCompletedSessionId = l_tFindEnv.tSnapshot.usSessionId;
        l_tFindEnv.bHasCompletedSession = true;
        l_tFindEnv.ulLastEndTick = ulNowTick;
        l_tFindEnv.tSnapshot.eState = MOMO_FIND_STATE_IDLE;
        l_tFindEnv.tSnapshot.usSessionId = 0U;
        l_tFindEnv.tSnapshot.ulStartTick = 0U;
        l_tFindEnv.tSnapshot.ulDurationTicks = 0U;
        l_tFindEnv.tSnapshot.ulGeneration++;
    }
    rt_hw_interrupt_enable(tLevel);

    return bAcknowledged;
}

bool MOMOFIND_GetSnapshot(MOMO_FIND_SNAPSHOT *pSnapshot)
{
    rt_base_t tLevel;

    if (NULL == pSnapshot)
    {
        return false;
    }
    tLevel = rt_hw_interrupt_disable();
    *pSnapshot = l_tFindEnv.tSnapshot;
    rt_hw_interrupt_enable(tLevel);

    return true;
}

uint32_t MOMOFIND_GetRemainingSeconds(const MOMO_FIND_SNAPSHOT *pSnapshot,
                                      uint32_t ulNowTick,
                                      uint32_t ulTicksPerSecond)
{
    uint32_t ulElapsed;
    uint32_t ulRemaining;

    if ((NULL == pSnapshot) || (0U == ulTicksPerSecond) ||
        ((MOMO_FIND_STATE_ACTIVE != pSnapshot->eState) &&
         (MOMO_FIND_STATE_ARMING != pSnapshot->eState)))
    {
        return 0U;
    }
    ulElapsed = (uint32_t)(ulNowTick - pSnapshot->ulStartTick);
    if (ulElapsed >= pSnapshot->ulDurationTicks)
    {
        return 0U;
    }
    ulRemaining = pSnapshot->ulDurationTicks - ulElapsed;

    return (ulRemaining / ulTicksPerSecond) +
           ((0U != (ulRemaining % ulTicksPerSecond)) ? 1U : 0U);
}

bool MOMOFIND_PeekEndEvent(MOMO_FIND_END_EVENT *pEvent)
{
    bool bPending;
    rt_base_t tLevel;

    if (NULL == pEvent)
    {
        return false;
    }
    tLevel = rt_hw_interrupt_disable();
    bPending = l_tFindEnv.bEndEventPending;
    if (bPending)
    {
        *pEvent = l_tFindEnv.tPendingEvent;
    }
    rt_hw_interrupt_enable(tLevel);

    return bPending;
}

void MOMOFIND_AcknowledgeEndEvent(void)
{
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    l_tFindEnv.bEndEventPending = false;
    rt_hw_interrupt_enable(tLevel);

    return;
}

const char *MOMOFIND_StateName(MOMO_FIND_STATE eState)
{
    static const char * const aNames[] = {"IDLE", "ARMING", "ACTIVE", "STOPPING"};

    return ((unsigned int)eState < (sizeof(aNames) / sizeof(aNames[0]))) ?
           aNames[eState] : "UNKNOWN";
}

const char *MOMOFIND_ResultName(MOMO_FIND_RESULT eResult)
{
    static const char * const aNames[] =
    {
        "ACCEPTED", "ALREADY", "BUSY_ALARM", "BUSY_TRANSFER", "RATE_LIMIT",
        "STOPPING", "IDLE", "STALE", "INVALID"
    };

    return ((unsigned int)eResult < (sizeof(aNames) / sizeof(aNames[0]))) ?
           aNames[eResult] : "INVALID";
}

const char *MOMOFIND_EndReasonName(MOMO_FIND_END_REASON eReason)
{
    static const char * const aNames[] =
    {
        "NONE", "USER_STOP", "PHONE_STOP", "TIMEOUT", "PREEMPTED_ALARM"
    };

    return ((unsigned int)eReason < (sizeof(aNames) / sizeof(aNames[0]))) ?
           aNames[eReason] : "NONE";
}
