#include "momo_memory_calendar.h"

#include <stddef.h>
#include <string.h>

#define MOMO_MEMORY_MAGIC (0x4F4D454DUL)
#define MOMO_MEMORY_VERSION (1U)
#define MOMO_MEMORY_MIN_YEAR (2020U)
#define MOMO_MEMORY_MAX_YEAR (2099U)
#define MOMO_MEMORY_MAX_DAY (29220UL)

#define MOMO_MEMORY_MAGIC_OFFSET (0U)
#define MOMO_MEMORY_VERSION_OFFSET (4U)
#define MOMO_MEMORY_SIZE_OFFSET (5U)
#define MOMO_MEMORY_RESERVED_OFFSET (6U)
#define MOMO_MEMORY_GENERATION_OFFSET (8U)
#define MOMO_MEMORY_ANCHOR_OFFSET (12U)
#define MOMO_MEMORY_EVENTS_OFFSET (16U)
#define MOMO_MEMORY_CRC_OFFSET (28U)
#define MOMO_MEMORY_CRC_INPUT_SIZE (28U)
#define MOMO_MEMORY_SERIAL_HALF_RANGE (0x80000000UL)

#if defined(_MSC_VER)
typedef char MOMO_MEMORY_PERSIST_SIZE_CHECK[
    (sizeof(MOMO_MEMORY_PERSISTED) == MOMO_MEMORY_PERSIST_SIZE) ? 1 : -1];
typedef char MOMO_MEMORY_RAM_BUDGET_CHECK[
    (sizeof(MOMO_MEMORY_CONTEXT) <= 64U) ? 1 : -1];
#else
_Static_assert(sizeof(MOMO_MEMORY_PERSISTED) == MOMO_MEMORY_PERSIST_SIZE,
               "Momo memory persistent layout size mismatch");
_Static_assert(sizeof(MOMO_MEMORY_CONTEXT) <= 64U,
               "Momo memory state exceeds RAM budget");
#endif

/*
 * MOMOMEMORY_IsLeapYear
 * 功能：按公历规则判断闰年。
 * 参数：
 *   - usYear: 年份
 * 返回值：闰年返回true，否则返回false。
 */
static bool MOMOMEMORY_IsLeapYear(uint16_t usYear)
{
    return ((0U == (usYear % 4U)) && (0U != (usYear % 100U))) ||
        (0U == (usYear % 400U));
}

/*
 * MOMOMEMORY_DaysInMonth
 * 功能：返回指定年月的公历天数。
 * 参数：
 *   - usYear: 年份
 *   - ucMonth: 月份1~12
 * 返回值：合法月份的天数，非法月份返回0。
 */
static uint8_t MOMOMEMORY_DaysInMonth(uint16_t usYear, uint8_t ucMonth)
{
    static const uint8_t l_aMonthDays[12] =
    {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    uint8_t ucDays;

    if ((1U > ucMonth) || (12U < ucMonth))
    {
        return 0U;
    }
    ucDays = l_aMonthDays[ucMonth - 1U];
    if ((2U == ucMonth) && MOMOMEMORY_IsLeapYear(usYear))
    {
        ucDays++;
    }

    return ucDays;
}

/*
 * MOMOMEMORY_ReadU16
 * 功能：从固定布局读取小端16位值。
 */
static uint16_t MOMOMEMORY_ReadU16(const uint8_t *pData)
{
    if (NULL == pData)
    {
        return 0U;
    }

    return (uint16_t)((uint16_t)pData[0] |
                      ((uint16_t)pData[1] << 8U));
}

/*
 * MOMOMEMORY_ReadU32
 * 功能：从固定布局读取小端32位值。
 */
static uint32_t MOMOMEMORY_ReadU32(const uint8_t *pData)
{
    if (NULL == pData)
    {
        return 0U;
    }

    return (uint32_t)pData[0] |
        ((uint32_t)pData[1] << 8U) |
        ((uint32_t)pData[2] << 16U) |
        ((uint32_t)pData[3] << 24U);
}

/*
 * MOMOMEMORY_WriteU16
 * 功能：向固定布局写入小端16位值。
 */
static void MOMOMEMORY_WriteU16(uint8_t *pData, uint16_t usValue)
{
    if (NULL != pData)
    {
        pData[0] = (uint8_t)(usValue & 0xFFU);
        pData[1] = (uint8_t)((usValue >> 8U) & 0xFFU);
    }

    return;
}

/*
 * MOMOMEMORY_WriteU32
 * 功能：向固定布局写入小端32位值。
 */
static void MOMOMEMORY_WriteU32(uint8_t *pData, uint32_t ulValue)
{
    if (NULL != pData)
    {
        pData[0] = (uint8_t)(ulValue & 0xFFU);
        pData[1] = (uint8_t)((ulValue >> 8U) & 0xFFU);
        pData[2] = (uint8_t)((ulValue >> 16U) & 0xFFU);
        pData[3] = (uint8_t)((ulValue >> 24U) & 0xFFU);
    }

    return;
}

/*
 * MOMOMEMORY_Crc16
 * 功能：计算CRC-16/CCITT-FALSE完整性校验。
 */
static uint16_t MOMOMEMORY_Crc16(const uint8_t *pData, uint8_t ucLength)
{
    uint16_t usCrc;
    uint8_t ucIndex;
    uint8_t ucBit;

    if (NULL == pData)
    {
        return 0U;
    }
    usCrc = 0xFFFFU;
    for (ucIndex = 0U; ucIndex < ucLength; ucIndex++)
    {
        usCrc ^= (uint16_t)((uint16_t)pData[ucIndex] << 8U);
        for (ucBit = 0U; ucBit < 8U; ucBit++)
        {
            usCrc = (0U != (usCrc & 0x8000U)) ?
                (uint16_t)((usCrc << 1U) ^ 0x1021U) :
                (uint16_t)(usCrc << 1U);
        }
    }

    return usCrc;
}

/*
 * MOMOMEMORY_ClearResult
 * 功能：初始化操作结果并复制当前RTC状态。
 */
static void MOMOMEMORY_ClearResult(const MOMO_MEMORY_CONTEXT *pContext,
                                   MOMO_MEMORY_RESULT *pResult)
{
    if (NULL != pResult)
    {
        (void)memset(pResult, 0, sizeof(*pResult));
        if (NULL != pContext)
        {
            pResult->bRtcInvalid = pContext->bRtcInvalid;
            pResult->bRollback = pContext->bRollback;
        }
    }

    return;
}

/*
 * MOMOMEMORY_SetPackedEvent
 * 功能：把一个3-bit事件写入无对齐依赖的90-bit数组。
 */
static void MOMOMEMORY_SetPackedEvent(uint8_t *pPacked, uint8_t ucIndex,
                                      uint8_t ucEvent)
{
    uint8_t ucByteIndex;
    uint8_t ucShift;
    uint16_t usWindow;
    uint16_t usMask;

    if ((NULL == pPacked) || (MOMO_MEMORY_DAY_COUNT <= ucIndex))
    {
        return;
    }
    ucByteIndex = (uint8_t)(((uint16_t)ucIndex * 3U) / 8U);
    ucShift = (uint8_t)(((uint16_t)ucIndex * 3U) % 8U);
    usWindow = pPacked[ucByteIndex];
    if ((uint8_t)(ucByteIndex + 1U) < MOMO_MEMORY_PACKED_EVENT_SIZE)
    {
        usWindow |= (uint16_t)((uint16_t)pPacked[ucByteIndex + 1U] << 8U);
    }
    usMask = (uint16_t)(0x07U << ucShift);
    usWindow = (uint16_t)((usWindow & (uint16_t)(~usMask)) |
                          ((uint16_t)(ucEvent & MOMO_MEMORY_EVENT_ALL) <<
                           ucShift));
    pPacked[ucByteIndex] = (uint8_t)(usWindow & 0xFFU);
    if ((uint8_t)(ucByteIndex + 1U) < MOMO_MEMORY_PACKED_EVENT_SIZE)
    {
        pPacked[ucByteIndex + 1U] =
            (uint8_t)((usWindow >> 8U) & 0xFFU);
    }

    return;
}

/*
 * MOMOMEMORY_GetPackedEvent
 * 功能：从无对齐依赖的90-bit数组读取一个3-bit事件。
 */
static uint8_t MOMOMEMORY_GetPackedEvent(const uint8_t *pPacked,
                                         uint8_t ucIndex)
{
    uint8_t ucByteIndex;
    uint8_t ucShift;
    uint16_t usWindow;

    if ((NULL == pPacked) || (MOMO_MEMORY_DAY_COUNT <= ucIndex))
    {
        return 0U;
    }
    ucByteIndex = (uint8_t)(((uint16_t)ucIndex * 3U) / 8U);
    ucShift = (uint8_t)(((uint16_t)ucIndex * 3U) % 8U);
    usWindow = pPacked[ucByteIndex];
    if ((uint8_t)(ucByteIndex + 1U) < MOMO_MEMORY_PACKED_EVENT_SIZE)
    {
        usWindow |= (uint16_t)((uint16_t)pPacked[ucByteIndex + 1U] << 8U);
    }

    return (uint8_t)((usWindow >> ucShift) & MOMO_MEMORY_EVENT_ALL);
}

/*
 * MOMOMEMORY_Decode
 * 功能：把已校验持久化块解码到RAM上下文。
 */
static void MOMOMEMORY_Decode(const MOMO_MEMORY_PERSISTED *pPersisted,
                              uint8_t ucSlot,
                              MOMO_MEMORY_CONTEXT *pContext)
{
    uint8_t ucIndex;

    (void)memset(pContext, 0, sizeof(*pContext));
    pContext->ulGeneration = MOMOMEMORY_ReadU32(
        &pPersisted->aData[MOMO_MEMORY_GENERATION_OFFSET]);
    pContext->ulAnchorDay = MOMOMEMORY_ReadU32(
        &pPersisted->aData[MOMO_MEMORY_ANCHOR_OFFSET]);
    pContext->ucActiveSlot = ucSlot;
    for (ucIndex = 0U; ucIndex < MOMO_MEMORY_DAY_COUNT; ucIndex++)
    {
        pContext->aEvents[ucIndex] = MOMOMEMORY_GetPackedEvent(
            &pPersisted->aData[MOMO_MEMORY_EVENTS_OFFSET], ucIndex);
    }

    return;
}

/*
 * MOMOMEMORY_SerialCompare
 * 功能：按32位串行号规则比较代号，处理自然回绕。
 * 返回值：A更新返回1，B更新返回-1，相等/半范围歧义返回0。
 */
static int8_t MOMOMEMORY_SerialCompare(uint32_t ulA, uint32_t ulB)
{
    uint32_t ulDifference;

    ulDifference = ulA - ulB;
    if ((0U == ulDifference) ||
        (MOMO_MEMORY_SERIAL_HALF_RANGE == ulDifference))
    {
        return 0;
    }

    return (MOMO_MEMORY_SERIAL_HALF_RANGE > ulDifference) ? 1 : -1;
}

/*
 * MOMOMEMORY_DateToDay
 * 功能：把2020—2099严格公历日期转换为从1开始的自然日序号。
 */
bool MOMOMEMORY_DateToDay(const MOMO_MEMORY_DATE *pDate,
                          uint32_t *pDay)
{
    uint32_t ulDay;
    uint16_t usYear;
    uint8_t ucMonth;
    uint8_t ucMaximumDay;

    if ((NULL == pDate) || (NULL == pDay) ||
        (MOMO_MEMORY_MIN_YEAR > pDate->usYear) ||
        (MOMO_MEMORY_MAX_YEAR < pDate->usYear))
    {
        return false;
    }
    ucMaximumDay = MOMOMEMORY_DaysInMonth(
        pDate->usYear, pDate->ucMonth);
    if ((0U == ucMaximumDay) || (1U > pDate->ucDay) ||
        (ucMaximumDay < pDate->ucDay))
    {
        return false;
    }

    ulDay = 1U;
    for (usYear = MOMO_MEMORY_MIN_YEAR; usYear < pDate->usYear; usYear++)
    {
        ulDay += MOMOMEMORY_IsLeapYear(usYear) ? 366U : 365U;
    }
    for (ucMonth = 1U; ucMonth < pDate->ucMonth; ucMonth++)
    {
        ulDay += MOMOMEMORY_DaysInMonth(pDate->usYear, ucMonth);
    }
    ulDay += (uint32_t)pDate->ucDay - 1U;
    *pDay = ulDay;

    return true;
}

/*
 * MOMOMEMORY_DayToDate
 * 功能：把自然日序号转换为2020—2099公历日期。
 */
bool MOMOMEMORY_DayToDate(uint32_t ulDay, MOMO_MEMORY_DATE *pDate)
{
    uint32_t ulRemaining;
    uint16_t usYear;
    uint16_t usYearDays;
    uint8_t ucMonth;
    uint8_t ucMonthDays;

    if ((NULL == pDate) || (0U == ulDay) ||
        (MOMO_MEMORY_MAX_DAY < ulDay))
    {
        return false;
    }
    ulRemaining = ulDay - 1U;
    usYear = MOMO_MEMORY_MIN_YEAR;
    while (MOMO_MEMORY_MAX_YEAR >= usYear)
    {
        usYearDays = MOMOMEMORY_IsLeapYear(usYear) ? 366U : 365U;
        if (usYearDays > ulRemaining)
        {
            break;
        }
        ulRemaining -= usYearDays;
        usYear++;
    }
    if (MOMO_MEMORY_MAX_YEAR < usYear)
    {
        return false;
    }
    ucMonth = 1U;
    while (12U >= ucMonth)
    {
        ucMonthDays = MOMOMEMORY_DaysInMonth(usYear, ucMonth);
        if ((uint32_t)ucMonthDays > ulRemaining)
        {
            break;
        }
        ulRemaining -= ucMonthDays;
        ucMonth++;
    }
    if (12U < ucMonth)
    {
        return false;
    }
    pDate->usYear = usYear;
    pDate->ucMonth = ucMonth;
    pDate->ucDay = (uint8_t)(ulRemaining + 1U);

    return true;
}

/*
 * MOMOMEMORY_ValidatePersisted
 * 功能：逐字段和CRC校验一个固定持久化槽。
 */
bool MOMOMEMORY_ValidatePersisted(
    const MOMO_MEMORY_PERSISTED *pPersisted)
{
    uint16_t usExpectedCrc;
    uint16_t usStoredCrc;
    uint32_t ulAnchorDay;
    uint8_t ucIndex;
    bool bHasEvents;

    if (NULL == pPersisted)
    {
        return false;
    }
    if ((MOMO_MEMORY_MAGIC != MOMOMEMORY_ReadU32(
            &pPersisted->aData[MOMO_MEMORY_MAGIC_OFFSET])) ||
        (MOMO_MEMORY_VERSION !=
            pPersisted->aData[MOMO_MEMORY_VERSION_OFFSET]) ||
        (MOMO_MEMORY_PERSIST_SIZE !=
            pPersisted->aData[MOMO_MEMORY_SIZE_OFFSET]) ||
        (0U != MOMOMEMORY_ReadU16(
            &pPersisted->aData[MOMO_MEMORY_RESERVED_OFFSET])))
    {
        return false;
    }
    usExpectedCrc = MOMOMEMORY_Crc16(
        pPersisted->aData, MOMO_MEMORY_CRC_INPUT_SIZE);
    usStoredCrc = MOMOMEMORY_ReadU16(
        &pPersisted->aData[MOMO_MEMORY_CRC_OFFSET]);
    if (usExpectedCrc != usStoredCrc)
    {
        return false;
    }
    if (0U != (pPersisted->aData[
            MOMO_MEMORY_EVENTS_OFFSET + MOMO_MEMORY_PACKED_EVENT_SIZE - 1U] &
        0xFCU))
    {
        return false;
    }
    ulAnchorDay = MOMOMEMORY_ReadU32(
        &pPersisted->aData[MOMO_MEMORY_ANCHOR_OFFSET]);
    if (MOMO_MEMORY_MAX_DAY < ulAnchorDay)
    {
        return false;
    }
    bHasEvents = false;
    for (ucIndex = 0U; ucIndex < MOMO_MEMORY_DAY_COUNT; ucIndex++)
    {
        if (0U != MOMOMEMORY_GetPackedEvent(
                &pPersisted->aData[MOMO_MEMORY_EVENTS_OFFSET], ucIndex))
        {
            bHasEvents = true;
            break;
        }
    }
    if ((0U == ulAnchorDay) && bHasEvents)
    {
        return false;
    }

    return true;
}

/*
 * MOMOMEMORY_Init
 * 功能：选择双槽最新有效版本并初始化固定内存上下文。
 */
bool MOMOMEMORY_Init(MOMO_MEMORY_CONTEXT *pContext,
                     const MOMO_MEMORY_PERSISTED *pSlotA,
                     const MOMO_MEMORY_PERSISTED *pSlotB,
                     uint8_t ucPreferredSlot)
{
    uint32_t ulGenerationA;
    uint32_t ulGenerationB;
    int8_t cCompare;
    bool bValidA;
    bool bValidB;

    if (NULL == pContext)
    {
        return false;
    }
    (void)memset(pContext, 0, sizeof(*pContext));
    pContext->ucActiveSlot = MOMO_MEMORY_SLOT_NONE;
    bValidA = MOMOMEMORY_ValidatePersisted(pSlotA);
    bValidB = MOMOMEMORY_ValidatePersisted(pSlotB);
    if (!bValidA && !bValidB)
    {
        return true;
    }
    if (bValidA && !bValidB)
    {
        MOMOMEMORY_Decode(pSlotA, MOMO_MEMORY_SLOT_A, pContext);
        return true;
    }
    if (!bValidA && bValidB)
    {
        MOMOMEMORY_Decode(pSlotB, MOMO_MEMORY_SLOT_B, pContext);
        return true;
    }

    ulGenerationA = MOMOMEMORY_ReadU32(
        &pSlotA->aData[MOMO_MEMORY_GENERATION_OFFSET]);
    ulGenerationB = MOMOMEMORY_ReadU32(
        &pSlotB->aData[MOMO_MEMORY_GENERATION_OFFSET]);
    cCompare = MOMOMEMORY_SerialCompare(ulGenerationA, ulGenerationB);
    if ((0 > cCompare) ||
        ((0 == cCompare) && (MOMO_MEMORY_SLOT_B == ucPreferredSlot)))
    {
        MOMOMEMORY_Decode(pSlotB, MOMO_MEMORY_SLOT_B, pContext);
    }
    else
    {
        MOMOMEMORY_Decode(pSlotA, MOMO_MEMORY_SLOT_A, pContext);
    }

    return true;
}

/*
 * MOMOMEMORY_ProcessDay
 * 功能：处理可信日期前进、无效RTC、回拨和待处理事件。
 */
bool MOMOMEMORY_ProcessDay(MOMO_MEMORY_CONTEXT *pContext,
                           uint32_t ulCurrentDay,
                           MOMO_MEMORY_RESULT *pResult)
{
    uint32_t ulDelta;
    uint8_t ucIndex;
    uint8_t ucPending;
    uint8_t ucPrevious;

    if ((NULL == pContext) || (NULL == pResult))
    {
        return false;
    }
    MOMOMEMORY_ClearResult(pContext, pResult);
    if ((0U == ulCurrentDay) || (MOMO_MEMORY_MAX_DAY < ulCurrentDay))
    {
        pContext->bRtcInvalid = true;
        pContext->bRollback = false;
        pResult->bRtcInvalid = true;
        pResult->bRollback = false;
        return true;
    }
    pContext->bRtcInvalid = false;
    pResult->bRtcInvalid = false;
    if ((0U != pContext->ulAnchorDay) &&
        (ulCurrentDay < pContext->ulAnchorDay))
    {
        pContext->bRollback = true;
        pResult->bRollback = true;
        return true;
    }
    pContext->bRollback = false;
    pResult->bRollback = false;
    if (0U == pContext->ulAnchorDay)
    {
        pContext->ulAnchorDay = ulCurrentDay;
        pResult->bChanged = true;
    }
    else if (ulCurrentDay > pContext->ulAnchorDay)
    {
        ulDelta = ulCurrentDay - pContext->ulAnchorDay;
        if (MOMO_MEMORY_DAY_COUNT <= ulDelta)
        {
            (void)memset(pContext->aEvents, 0, sizeof(pContext->aEvents));
        }
        else
        {
            for (ucIndex = 0U;
                 ucIndex < (uint8_t)(MOMO_MEMORY_DAY_COUNT - ulDelta);
                 ucIndex++)
            {
                pContext->aEvents[ucIndex] =
                    pContext->aEvents[(uint8_t)(ucIndex + ulDelta)];
            }
            (void)memset(
                &pContext->aEvents[MOMO_MEMORY_DAY_COUNT - ulDelta],
                0,
                ulDelta);
        }
        pContext->ulAnchorDay = ulCurrentDay;
        pContext->bDirty = true;
        pResult->bChanged = true;
        pResult->bSaveRequired = true;
    }

    ucPending = pContext->ucPendingEvents;
    if (0U != ucPending)
    {
        ucPrevious = pContext->aEvents[MOMO_MEMORY_DAY_COUNT - 1U];
        pContext->aEvents[MOMO_MEMORY_DAY_COUNT - 1U] =
            (uint8_t)(ucPrevious | ucPending);
        pContext->ucPendingEvents = 0U;
        if (ucPrevious != pContext->aEvents[MOMO_MEMORY_DAY_COUNT - 1U])
        {
            pContext->bDirty = true;
            pResult->bChanged = true;
            pResult->bSaveRequired = true;
        }
    }

    return true;
}

/*
 * MOMOMEMORY_Record
 * 功能：记录一个受控内部事件，同日同类事件严格幂等。
 */
bool MOMOMEMORY_Record(MOMO_MEMORY_CONTEXT *pContext,
                       uint32_t ulCurrentDay,
                       uint8_t ucEvent,
                       MOMO_MEMORY_RESULT *pResult)
{
    MOMO_MEMORY_RESULT tDayResult;
    uint8_t ucPrevious;

    if ((NULL == pContext) || (NULL == pResult) ||
        ((MOMO_MEMORY_EVENT_MEET != ucEvent) &&
         (MOMO_MEMORY_EVENT_PLAY != ucEvent) &&
         (MOMO_MEMORY_EVENT_ACHIEVEMENT != ucEvent)))
    {
        return false;
    }
    if (!MOMOMEMORY_ProcessDay(pContext, ulCurrentDay, &tDayResult))
    {
        return false;
    }
    *pResult = tDayResult;
    if (tDayResult.bRtcInvalid || tDayResult.bRollback)
    {
        ucPrevious = pContext->ucPendingEvents;
        pContext->ucPendingEvents = (uint8_t)(ucPrevious | ucEvent);
        if (ucPrevious != pContext->ucPendingEvents)
        {
            pResult->bChanged = true;
        }
        return true;
    }

    ucPrevious = pContext->aEvents[MOMO_MEMORY_DAY_COUNT - 1U];
    pContext->aEvents[MOMO_MEMORY_DAY_COUNT - 1U] =
        (uint8_t)(ucPrevious | ucEvent);
    if (ucPrevious != pContext->aEvents[MOMO_MEMORY_DAY_COUNT - 1U])
    {
        pContext->bDirty = true;
        pResult->bChanged = true;
        pResult->bSaveRequired = true;
    }

    return true;
}

/*
 * MOMOMEMORY_Clear
 * 功能：构造可原子保存的空历史候选，不依赖RTC有效性。
 */
bool MOMOMEMORY_Clear(MOMO_MEMORY_CONTEXT *pContext,
                      uint32_t ulCurrentDay,
                      MOMO_MEMORY_RESULT *pResult)
{
    uint32_t ulAnchorDay;

    if ((NULL == pContext) || (NULL == pResult))
    {
        return false;
    }
    MOMOMEMORY_ClearResult(pContext, pResult);
    ulAnchorDay = ((0U != ulCurrentDay) &&
                   (MOMO_MEMORY_MAX_DAY >= ulCurrentDay)) ?
        ulCurrentDay : 0U;
    (void)memset(pContext->aEvents, 0, sizeof(pContext->aEvents));
    pContext->ulAnchorDay = ulAnchorDay;
    pContext->ucPendingEvents = 0U;
    pContext->bRtcInvalid = (0U == ulAnchorDay);
    pContext->bRollback = false;
    pContext->bDirty = true;
    pResult->bChanged = true;
    pResult->bSaveRequired = true;
    pResult->bRtcInvalid = pContext->bRtcInvalid;
    pResult->bRollback = false;

    return true;
}

/*
 * MOMOMEMORY_Export
 * 功能：把上下文按固定字节布局和指定代号导出。
 */
bool MOMOMEMORY_Export(const MOMO_MEMORY_CONTEXT *pContext,
                       uint32_t ulGeneration,
                       MOMO_MEMORY_PERSISTED *pPersisted)
{
    uint16_t usCrc;
    uint8_t ucIndex;
    bool bHasEvents;

    if ((NULL == pContext) || (NULL == pPersisted) ||
        (MOMO_MEMORY_MAX_DAY < pContext->ulAnchorDay))
    {
        return false;
    }
    (void)memset(pPersisted, 0, sizeof(*pPersisted));
    MOMOMEMORY_WriteU32(
        &pPersisted->aData[MOMO_MEMORY_MAGIC_OFFSET], MOMO_MEMORY_MAGIC);
    pPersisted->aData[MOMO_MEMORY_VERSION_OFFSET] = MOMO_MEMORY_VERSION;
    pPersisted->aData[MOMO_MEMORY_SIZE_OFFSET] = MOMO_MEMORY_PERSIST_SIZE;
    MOMOMEMORY_WriteU32(
        &pPersisted->aData[MOMO_MEMORY_GENERATION_OFFSET], ulGeneration);
    MOMOMEMORY_WriteU32(
        &pPersisted->aData[MOMO_MEMORY_ANCHOR_OFFSET],
        pContext->ulAnchorDay);
    bHasEvents = false;
    for (ucIndex = 0U; ucIndex < MOMO_MEMORY_DAY_COUNT; ucIndex++)
    {
        if (0U != (pContext->aEvents[ucIndex] &
                   (uint8_t)(~MOMO_MEMORY_EVENT_ALL)))
        {
            return false;
        }
        MOMOMEMORY_SetPackedEvent(
            &pPersisted->aData[MOMO_MEMORY_EVENTS_OFFSET],
            ucIndex,
            pContext->aEvents[ucIndex]);
        if (0U != pContext->aEvents[ucIndex])
        {
            bHasEvents = true;
        }
    }
    if ((0U == pContext->ulAnchorDay) && bHasEvents)
    {
        return false;
    }
    usCrc = MOMOMEMORY_Crc16(
        pPersisted->aData, MOMO_MEMORY_CRC_INPUT_SIZE);
    MOMOMEMORY_WriteU16(
        &pPersisted->aData[MOMO_MEMORY_CRC_OFFSET], usCrc);

    return true;
}

/*
 * MOMOMEMORY_Commit
 * 功能：在外部双槽回读成功后提交代号和活动槽。
 */
bool MOMOMEMORY_Commit(MOMO_MEMORY_CONTEXT *pContext,
                       uint32_t ulGeneration,
                       uint8_t ucActiveSlot)
{
    if ((NULL == pContext) ||
        ((MOMO_MEMORY_SLOT_A != ucActiveSlot) &&
         (MOMO_MEMORY_SLOT_B != ucActiveSlot)))
    {
        return false;
    }
    pContext->ulGeneration = ulGeneration;
    pContext->ucActiveSlot = ucActiveSlot;
    pContext->bDirty = false;

    return true;
}

/*
 * MOMOMEMORY_GetDay
 * 功能：读取从旧到新的固定索引、自然日和3-bit事件。
 */
bool MOMOMEMORY_GetDay(const MOMO_MEMORY_CONTEXT *pContext,
                       uint8_t ucIndex,
                       uint32_t *pDay,
                       uint8_t *pEvents)
{
    uint32_t ulDistance;

    if ((NULL == pContext) || (NULL == pDay) || (NULL == pEvents) ||
        (MOMO_MEMORY_DAY_COUNT <= ucIndex))
    {
        return false;
    }
    *pEvents = pContext->aEvents[ucIndex];
    *pDay = 0U;
    if (0U == pContext->ulAnchorDay)
    {
        return true;
    }
    ulDistance = (MOMO_MEMORY_DAY_COUNT - 1U) - ucIndex;
    if (pContext->ulAnchorDay > ulDistance)
    {
        *pDay = pContext->ulAnchorDay - ulDistance;
    }

    return true;
}
