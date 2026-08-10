#ifndef MOMO_FIND_ME_H
#define MOMO_FIND_ME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum _MOMO_FIND_STATE
{
    MOMO_FIND_STATE_IDLE = 0,
    MOMO_FIND_STATE_ARMING,
    MOMO_FIND_STATE_ACTIVE,
    MOMO_FIND_STATE_STOPPING
} MOMO_FIND_STATE;

typedef enum _MOMO_FIND_RESULT
{
    MOMO_FIND_RESULT_ACCEPTED = 0,
    MOMO_FIND_RESULT_ALREADY_ACTIVE,
    MOMO_FIND_RESULT_BUSY_ALARM,
    MOMO_FIND_RESULT_BUSY_TRANSFER,
    MOMO_FIND_RESULT_RATE_LIMIT,
    MOMO_FIND_RESULT_STOPPING,
    MOMO_FIND_RESULT_IDLE,
    MOMO_FIND_RESULT_STALE,
    MOMO_FIND_RESULT_INVALID
} MOMO_FIND_RESULT;

typedef enum _MOMO_FIND_END_REASON
{
    MOMO_FIND_END_NONE = 0,
    MOMO_FIND_END_USER_STOP,
    MOMO_FIND_END_PHONE_STOP,
    MOMO_FIND_END_TIMEOUT,
    MOMO_FIND_END_PREEMPTED_ALARM
} MOMO_FIND_END_REASON;

typedef struct _MOMO_FIND_SNAPSHOT
{
    MOMO_FIND_STATE eState;
    MOMO_FIND_END_REASON eEndReason;
    uint16_t usSessionId;
    uint32_t ulStartTick;
    uint32_t ulDurationTicks;
    uint32_t ulGeneration;
    uint32_t ulAcceptedCount;
    uint32_t ulDuplicateCount;
    uint32_t ulBusyCount;
    uint32_t ulStopCount;
} MOMO_FIND_SNAPSHOT;

typedef struct _MOMO_FIND_END_EVENT
{
    uint16_t usSessionId;
    MOMO_FIND_END_REASON eReason;
} MOMO_FIND_END_EVENT;

void MOMOFIND_Init(void);
MOMO_FIND_RESULT MOMOFIND_StartAt(uint16_t usRequestId,
                                  uint32_t ulNowTick,
                                  uint32_t ulDurationTicks,
                                  uint32_t ulCooldownTicks,
                                  bool bAlarmBusy,
                                  bool bTransferBusy,
                                  bool *pNeedsWake);
bool MOMOFIND_ConfirmStart(uint16_t usRequestId);
void MOMOFIND_CancelArming(uint16_t usRequestId);
MOMO_FIND_RESULT MOMOFIND_StopAt(MOMO_FIND_END_REASON eReason,
                                 uint32_t ulNowTick);
MOMO_FIND_RESULT MOMOFIND_StopSessionAt(uint16_t usRequestId,
                                        MOMO_FIND_END_REASON eReason,
                                        uint32_t ulNowTick);
void MOMOFIND_PreemptAlarm(uint32_t ulNowTick);
void MOMOFIND_PollAt(uint32_t ulNowTick);
bool MOMOFIND_AcknowledgeUiStopped(uint32_t ulNowTick);
bool MOMOFIND_GetSnapshot(MOMO_FIND_SNAPSHOT *pSnapshot);
uint32_t MOMOFIND_GetRemainingSeconds(const MOMO_FIND_SNAPSHOT *pSnapshot,
                                      uint32_t ulNowTick,
                                      uint32_t ulTicksPerSecond);
bool MOMOFIND_PeekEndEvent(MOMO_FIND_END_EVENT *pEvent);
void MOMOFIND_AcknowledgeEndEvent(void);
const char *MOMOFIND_StateName(MOMO_FIND_STATE eState);
const char *MOMOFIND_ResultName(MOMO_FIND_RESULT eResult);
const char *MOMOFIND_EndReasonName(MOMO_FIND_END_REASON eReason);

#endif /* MOMO_FIND_ME_H */
