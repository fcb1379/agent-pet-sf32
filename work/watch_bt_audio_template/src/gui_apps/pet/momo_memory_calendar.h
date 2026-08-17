#ifndef MOMO_MEMORY_CALENDAR_H
#define MOMO_MEMORY_CALENDAR_H

#include <stdbool.h>
#include <stdint.h>

#define MOMO_MEMORY_DAY_COUNT (30U)
#define MOMO_MEMORY_PACKED_EVENT_SIZE (12U)
#define MOMO_MEMORY_PERSIST_SIZE (30U)
#define MOMO_MEMORY_SLOT_A (0U)
#define MOMO_MEMORY_SLOT_B (1U)
#define MOMO_MEMORY_SLOT_NONE (0xFFU)

#define MOMO_MEMORY_EVENT_MEET (0x01U)
#define MOMO_MEMORY_EVENT_PLAY (0x02U)
#define MOMO_MEMORY_EVENT_ACHIEVEMENT (0x04U)
#define MOMO_MEMORY_EVENT_ALL (0x07U)

/* MOMO_MEMORY_DATE: 经过严格公历校验的本地日期。 */
typedef struct _MOMO_MEMORY_DATE
{
    uint16_t usYear;
    uint8_t ucMonth;
    uint8_t ucDay;
} MOMO_MEMORY_DATE;

/* MOMO_MEMORY_PERSISTED: 固定30字节、与编译器对齐无关的持久化块。 */
typedef struct _MOMO_MEMORY_PERSISTED
{
    uint8_t aData[MOMO_MEMORY_PERSIST_SIZE];
} MOMO_MEMORY_PERSISTED;

/* MOMO_MEMORY_CONTEXT: 固定容量的30天回忆状态机。 */
typedef struct _MOMO_MEMORY_CONTEXT
{
    uint8_t aEvents[MOMO_MEMORY_DAY_COUNT];
    uint32_t ulAnchorDay;
    uint32_t ulGeneration;
    uint8_t ucPendingEvents;
    uint8_t ucActiveSlot;
    bool bRtcInvalid;
    bool bRollback;
    bool bDirty;
} MOMO_MEMORY_CONTEXT;

/* MOMO_MEMORY_RESULT: 一次日期或事件操作的有界结果。 */
typedef struct _MOMO_MEMORY_RESULT
{
    bool bChanged;
    bool bSaveRequired;
    bool bRtcInvalid;
    bool bRollback;
} MOMO_MEMORY_RESULT;

bool MOMOMEMORY_DateToDay(const MOMO_MEMORY_DATE *pDate,
                          uint32_t *pDay);
bool MOMOMEMORY_DayToDate(uint32_t ulDay, MOMO_MEMORY_DATE *pDate);
bool MOMOMEMORY_ValidatePersisted(
    const MOMO_MEMORY_PERSISTED *pPersisted);
bool MOMOMEMORY_Init(MOMO_MEMORY_CONTEXT *pContext,
                     const MOMO_MEMORY_PERSISTED *pSlotA,
                     const MOMO_MEMORY_PERSISTED *pSlotB,
                     uint8_t ucPreferredSlot);
bool MOMOMEMORY_ProcessDay(MOMO_MEMORY_CONTEXT *pContext,
                           uint32_t ulCurrentDay,
                           MOMO_MEMORY_RESULT *pResult);
bool MOMOMEMORY_Record(MOMO_MEMORY_CONTEXT *pContext,
                       uint32_t ulCurrentDay,
                       uint8_t ucEvent,
                       MOMO_MEMORY_RESULT *pResult);
bool MOMOMEMORY_Clear(MOMO_MEMORY_CONTEXT *pContext,
                      uint32_t ulCurrentDay,
                      MOMO_MEMORY_RESULT *pResult);
bool MOMOMEMORY_Export(const MOMO_MEMORY_CONTEXT *pContext,
                       uint32_t ulGeneration,
                       MOMO_MEMORY_PERSISTED *pPersisted);
bool MOMOMEMORY_Commit(MOMO_MEMORY_CONTEXT *pContext,
                       uint32_t ulGeneration,
                       uint8_t ucActiveSlot);
bool MOMOMEMORY_GetDay(const MOMO_MEMORY_CONTEXT *pContext,
                       uint8_t ucIndex,
                       uint32_t *pDay,
                       uint8_t *pEvents);

#endif /* MOMO_MEMORY_CALENDAR_H */
