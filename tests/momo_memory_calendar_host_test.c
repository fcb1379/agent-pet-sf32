#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "momo_memory_calendar.h"

static uint32_t TEST_Day(uint16_t usYear, uint8_t ucMonth, uint8_t ucDay)
{
    MOMO_MEMORY_DATE tDate;
    uint32_t ulDay;

    tDate.usYear = usYear;
    tDate.ucMonth = ucMonth;
    tDate.ucDay = ucDay;
    ulDay = 0U;
    assert(MOMOMEMORY_DateToDay(&tDate, &ulDay));
    return ulDay;
}

static MOMO_MEMORY_PERSISTED TEST_Export(
    const MOMO_MEMORY_CONTEXT *pContext, uint32_t ulGeneration)
{
    MOMO_MEMORY_PERSISTED tPersisted;

    assert(MOMOMEMORY_Export(pContext, ulGeneration, &tPersisted));
    assert(MOMOMEMORY_ValidatePersisted(&tPersisted));
    return tPersisted;
}

static void TEST_Dates(void)
{
    MOMO_MEMORY_DATE tDate;
    uint32_t ulDay;

    assert(1U == TEST_Day(2020U, 1U, 1U));
    ulDay = TEST_Day(2020U, 2U, 29U);
    assert(MOMOMEMORY_DayToDate(ulDay, &tDate));
    assert(2020U == tDate.usYear);
    assert(2U == tDate.ucMonth);
    assert(29U == tDate.ucDay);
    assert((TEST_Day(2021U, 3U, 1U) - 1U) ==
           TEST_Day(2021U, 2U, 28U));
    assert((TEST_Day(2024U, 3U, 1U) - 1U) ==
           TEST_Day(2024U, 2U, 29U));
    assert((TEST_Day(2025U, 1U, 1U) - 1U) ==
           TEST_Day(2024U, 12U, 31U));
    assert(29220U == TEST_Day(2099U, 12U, 31U));
    assert(MOMOMEMORY_DayToDate(TEST_Day(2099U, 12U, 31U), &tDate));
    assert(!MOMOMEMORY_DayToDate(0U, &tDate));
    assert(!MOMOMEMORY_DayToDate(
        TEST_Day(2099U, 12U, 31U) + 1U, &tDate));

    tDate.usYear = 2019U;
    tDate.ucMonth = 12U;
    tDate.ucDay = 31U;
    assert(!MOMOMEMORY_DateToDay(&tDate, &ulDay));
    tDate.usYear = 2100U;
    assert(!MOMOMEMORY_DateToDay(&tDate, &ulDay));
    tDate.usYear = 2021U;
    tDate.ucMonth = 2U;
    tDate.ucDay = 29U;
    assert(!MOMOMEMORY_DateToDay(&tDate, &ulDay));
    tDate.ucMonth = 13U;
    tDate.ucDay = 1U;
    assert(!MOMOMEMORY_DateToDay(&tDate, &ulDay));
    assert(!MOMOMEMORY_DateToDay(NULL, &ulDay));
    assert(!MOMOMEMORY_DateToDay(&tDate, NULL));
}

static void TEST_EventsAndWriteGate(void)
{
    MOMO_MEMORY_CONTEXT tContext;
    MOMO_MEMORY_RESULT tResult;
    uint32_t ulDay;
    uint32_t ulReadDay;
    uint8_t ucEvents;
    uint16_t usWriteCount;
    uint16_t usIndex;

    ulDay = TEST_Day(2026U, 8U, 18U);
    assert(MOMOMEMORY_Init(&tContext, NULL, NULL, MOMO_MEMORY_SLOT_NONE));
    usWriteCount = 0U;
    assert(MOMOMEMORY_Record(
        &tContext, ulDay, MOMO_MEMORY_EVENT_MEET, &tResult));
    usWriteCount += tResult.bSaveRequired ? 1U : 0U;
    assert(tResult.bSaveRequired);
    for (usIndex = 0U; usIndex < 20U; usIndex++)
    {
        assert(MOMOMEMORY_Record(
            &tContext, ulDay, MOMO_MEMORY_EVENT_MEET, &tResult));
        usWriteCount += tResult.bSaveRequired ? 1U : 0U;
    }
    assert(MOMOMEMORY_Record(
        &tContext, ulDay, MOMO_MEMORY_EVENT_PLAY, &tResult));
    usWriteCount += tResult.bSaveRequired ? 1U : 0U;
    for (usIndex = 0U; usIndex < 100U; usIndex++)
    {
        assert(MOMOMEMORY_Record(
            &tContext, ulDay, MOMO_MEMORY_EVENT_PLAY, &tResult));
        usWriteCount += tResult.bSaveRequired ? 1U : 0U;
    }
    assert(MOMOMEMORY_Record(
        &tContext, ulDay, MOMO_MEMORY_EVENT_ACHIEVEMENT, &tResult));
    usWriteCount += tResult.bSaveRequired ? 1U : 0U;
    assert(3U == usWriteCount);
    assert(MOMOMEMORY_GetDay(
        &tContext, MOMO_MEMORY_DAY_COUNT - 1U, &ulReadDay, &ucEvents));
    assert(ulDay == ulReadDay);
    assert(MOMO_MEMORY_EVENT_ALL == ucEvents);
    assert(!MOMOMEMORY_Record(&tContext, ulDay, 3U, &tResult));
    assert(!MOMOMEMORY_GetDay(
        &tContext, MOMO_MEMORY_DAY_COUNT, &ulReadDay, &ucEvents));
}

static void TEST_InvalidRtcRollbackAndRecovery(void)
{
    MOMO_MEMORY_CONTEXT tContext;
    MOMO_MEMORY_RESULT tResult;
    uint32_t ulDay;
    uint32_t ulReadDay;
    uint8_t ucEvents;

    ulDay = TEST_Day(2026U, 8U, 18U);
    assert(MOMOMEMORY_Init(&tContext, NULL, NULL, MOMO_MEMORY_SLOT_NONE));
    assert(MOMOMEMORY_Record(
        &tContext, 0U, MOMO_MEMORY_EVENT_MEET, &tResult));
    assert(tResult.bRtcInvalid);
    assert(!tResult.bSaveRequired);
    assert(MOMO_MEMORY_EVENT_MEET == tContext.ucPendingEvents);
    assert(MOMOMEMORY_ProcessDay(&tContext, ulDay, &tResult));
    assert(tResult.bSaveRequired);
    assert(0U == tContext.ucPendingEvents);

    assert(MOMOMEMORY_Record(
        &tContext, ulDay - 1U, MOMO_MEMORY_EVENT_PLAY, &tResult));
    assert(tResult.bRollback);
    assert(!tResult.bSaveRequired);
    assert(ulDay == tContext.ulAnchorDay);
    assert(MOMO_MEMORY_EVENT_PLAY == tContext.ucPendingEvents);
    assert(MOMOMEMORY_Record(
        &tContext, ulDay - 2U, MOMO_MEMORY_EVENT_PLAY, &tResult));
    assert(MOMO_MEMORY_EVENT_PLAY == tContext.ucPendingEvents);
    assert(MOMOMEMORY_ProcessDay(&tContext, ulDay, &tResult));
    assert(!tResult.bRollback);
    assert(tResult.bSaveRequired);
    assert(MOMOMEMORY_GetDay(
        &tContext, MOMO_MEMORY_DAY_COUNT - 1U, &ulReadDay, &ucEvents));
    assert(ulDay == ulReadDay);
    assert((MOMO_MEMORY_EVENT_MEET | MOMO_MEMORY_EVENT_PLAY) == ucEvents);
}

static void TEST_RollingAndSkippedDays(void)
{
    MOMO_MEMORY_CONTEXT tContext;
    MOMO_MEMORY_RESULT tResult;
    uint32_t ulStart;
    uint32_t ulReadDay;
    uint8_t ucEvents;
    uint8_t ucIndex;

    ulStart = TEST_Day(2026U, 1U, 28U);
    assert(MOMOMEMORY_Init(&tContext, NULL, NULL, MOMO_MEMORY_SLOT_NONE));
    assert(MOMOMEMORY_Record(
        &tContext, ulStart, MOMO_MEMORY_EVENT_MEET, &tResult));
    assert(MOMOMEMORY_Record(
        &tContext, ulStart + 3U, MOMO_MEMORY_EVENT_PLAY, &tResult));
    assert(tResult.bSaveRequired);
    assert(MOMOMEMORY_GetDay(&tContext, 26U, &ulReadDay, &ucEvents));
    assert(ulStart == ulReadDay);
    assert(MOMO_MEMORY_EVENT_MEET == ucEvents);
    assert(MOMOMEMORY_GetDay(&tContext, 27U, &ulReadDay, &ucEvents));
    assert(0U == ucEvents);
    assert(MOMOMEMORY_GetDay(&tContext, 28U, &ulReadDay, &ucEvents));
    assert(0U == ucEvents);
    assert(MOMOMEMORY_GetDay(&tContext, 29U, &ulReadDay, &ucEvents));
    assert(MOMO_MEMORY_EVENT_PLAY == ucEvents);

    for (ucIndex = 4U; ucIndex < 35U; ucIndex++)
    {
        assert(MOMOMEMORY_Record(
            &tContext, ulStart + ucIndex,
            MOMO_MEMORY_EVENT_MEET, &tResult));
    }
    assert((ulStart + 34U) == tContext.ulAnchorDay);
    assert(MOMOMEMORY_GetDay(&tContext, 0U, &ulReadDay, &ucEvents));
    assert((ulStart + 5U) == ulReadDay);
    assert(MOMO_MEMORY_EVENT_MEET == ucEvents);
    assert(MOMOMEMORY_ProcessDay(
        &tContext, ulStart + 100U, &tResult));
    assert(tResult.bSaveRequired);
    for (ucIndex = 0U; ucIndex < MOMO_MEMORY_DAY_COUNT; ucIndex++)
    {
        assert(MOMOMEMORY_GetDay(
            &tContext, ucIndex, &ulReadDay, &ucEvents));
        assert(0U == ucEvents);
    }

    assert(MOMOMEMORY_Init(&tContext, NULL, NULL, MOMO_MEMORY_SLOT_NONE));
    assert(MOMOMEMORY_Record(
        &tContext, ulStart, MOMO_MEMORY_EVENT_MEET, &tResult));
    assert(MOMOMEMORY_ProcessDay(
        &tContext, ulStart + 29U, &tResult));
    assert(MOMOMEMORY_GetDay(&tContext, 0U, &ulReadDay, &ucEvents));
    assert(ulStart == ulReadDay);
    assert(MOMO_MEMORY_EVENT_MEET == ucEvents);
    assert(MOMOMEMORY_ProcessDay(
        &tContext, ulStart + 30U, &tResult));
    assert(MOMOMEMORY_GetDay(&tContext, 0U, &ulReadDay, &ucEvents));
    assert(0U == ucEvents);
}

static void TEST_PackingIntegrityAndBitBoundaries(void)
{
    MOMO_MEMORY_CONTEXT tContext;
    MOMO_MEMORY_CONTEXT tRestored;
    MOMO_MEMORY_PERSISTED tPersisted;
    MOMO_MEMORY_PERSISTED tDamaged;
    uint32_t ulDay;
    uint32_t ulReadDay;
    uint8_t ucEvents;
    uint8_t ucIndex;

    ulDay = TEST_Day(2026U, 8U, 18U);
    assert(MOMOMEMORY_Init(&tContext, NULL, NULL, MOMO_MEMORY_SLOT_NONE));
    tContext.ulAnchorDay = ulDay;
    for (ucIndex = 0U; ucIndex < MOMO_MEMORY_DAY_COUNT; ucIndex++)
    {
        tContext.aEvents[ucIndex] = (uint8_t)(ucIndex & MOMO_MEMORY_EVENT_ALL);
    }
    tPersisted = TEST_Export(&tContext, 0x12345678UL);
    assert(MOMOMEMORY_Init(
        &tRestored, &tPersisted, NULL, MOMO_MEMORY_SLOT_NONE));
    assert(0x12345678UL == tRestored.ulGeneration);
    for (ucIndex = 0U; ucIndex < MOMO_MEMORY_DAY_COUNT; ucIndex++)
    {
        assert(MOMOMEMORY_GetDay(
            &tRestored, ucIndex, &ulReadDay, &ucEvents));
        assert((uint8_t)(ucIndex & MOMO_MEMORY_EVENT_ALL) == ucEvents);
    }
    for (ucIndex = 0U; ucIndex < MOMO_MEMORY_PERSIST_SIZE; ucIndex++)
    {
        tDamaged = tPersisted;
        tDamaged.aData[ucIndex] ^= 0x01U;
        assert(!MOMOMEMORY_ValidatePersisted(&tDamaged));
    }
    tDamaged = tPersisted;
    tDamaged.aData[27] |= 0x80U;
    assert(!MOMOMEMORY_ValidatePersisted(&tDamaged));
    assert(!MOMOMEMORY_Export(NULL, 0U, &tPersisted));
    assert(!MOMOMEMORY_Export(&tContext, 0U, NULL));
}

static void TEST_DualSlotGenerationWrap(void)
{
    MOMO_MEMORY_CONTEXT tAContext;
    MOMO_MEMORY_CONTEXT tBContext;
    MOMO_MEMORY_CONTEXT tLoaded;
    MOMO_MEMORY_PERSISTED tSlotA;
    MOMO_MEMORY_PERSISTED tSlotB;
    uint32_t ulDay;

    ulDay = TEST_Day(2026U, 8U, 18U);
    assert(MOMOMEMORY_Init(&tAContext, NULL, NULL, MOMO_MEMORY_SLOT_NONE));
    assert(MOMOMEMORY_Init(&tBContext, NULL, NULL, MOMO_MEMORY_SLOT_NONE));
    tAContext.ulAnchorDay = ulDay;
    tBContext.ulAnchorDay = ulDay;
    tAContext.aEvents[29] = MOMO_MEMORY_EVENT_MEET;
    tBContext.aEvents[29] = MOMO_MEMORY_EVENT_PLAY;
    tSlotA = TEST_Export(&tAContext, 0xFFFFFFFFUL);
    tSlotB = TEST_Export(&tBContext, 0U);
    assert(MOMOMEMORY_Init(
        &tLoaded, &tSlotA, &tSlotB, MOMO_MEMORY_SLOT_A));
    assert(MOMO_MEMORY_SLOT_B == tLoaded.ucActiveSlot);
    assert(MOMO_MEMORY_EVENT_PLAY == tLoaded.aEvents[29]);

    tSlotA = TEST_Export(&tAContext, 1U);
    tSlotB = TEST_Export(&tBContext, 1U + 0x80000000UL);
    assert(MOMOMEMORY_Init(
        &tLoaded, &tSlotA, &tSlotB, MOMO_MEMORY_SLOT_B));
    assert(MOMO_MEMORY_SLOT_B == tLoaded.ucActiveSlot);
    assert(MOMOMEMORY_Init(
        &tLoaded, &tSlotA, &tSlotB, MOMO_MEMORY_SLOT_A));
    assert(MOMO_MEMORY_SLOT_A == tLoaded.ucActiveSlot);

    tSlotA.aData[0] ^= 1U;
    assert(MOMOMEMORY_Init(
        &tLoaded, &tSlotA, &tSlotB, MOMO_MEMORY_SLOT_A));
    assert(MOMO_MEMORY_SLOT_B == tLoaded.ucActiveSlot);
}

static void TEST_ClearCandidateAndCommit(void)
{
    MOMO_MEMORY_CONTEXT tContext;
    MOMO_MEMORY_CONTEXT tCandidate;
    MOMO_MEMORY_RESULT tResult;
    MOMO_MEMORY_PERSISTED tPersisted;
    uint32_t ulDay;

    ulDay = TEST_Day(2026U, 8U, 18U);
    assert(MOMOMEMORY_Init(&tContext, NULL, NULL, MOMO_MEMORY_SLOT_NONE));
    assert(MOMOMEMORY_Record(
        &tContext, ulDay, MOMO_MEMORY_EVENT_MEET, &tResult));
    tCandidate = tContext;
    assert(MOMOMEMORY_Clear(&tCandidate, ulDay, &tResult));
    assert(0U != tContext.aEvents[29]);
    assert(0U == tCandidate.aEvents[29]);
    assert(MOMOMEMORY_Export(&tCandidate, 8U, &tPersisted));
    assert(MOMOMEMORY_Commit(&tCandidate, 8U, MOMO_MEMORY_SLOT_A));
    assert(8U == tCandidate.ulGeneration);
    assert(MOMO_MEMORY_SLOT_A == tCandidate.ucActiveSlot);
    assert(!tCandidate.bDirty);
    assert(!MOMOMEMORY_Commit(&tCandidate, 9U, MOMO_MEMORY_SLOT_NONE));

    tCandidate = tContext;
    assert(MOMOMEMORY_Clear(&tCandidate, 0U, &tResult));
    assert(tCandidate.bRtcInvalid);
    assert(0U == tCandidate.ulAnchorDay);
    assert(MOMOMEMORY_Export(&tCandidate, 9U, &tPersisted));

    assert(MOMOMEMORY_Init(&tCandidate, NULL, NULL, MOMO_MEMORY_SLOT_NONE));
    tCandidate.ulAnchorDay = TEST_Day(2099U, 12U, 31U);
    assert(MOMOMEMORY_Export(&tCandidate, 10U, &tPersisted));
    assert(MOMOMEMORY_ProcessDay(
        &tCandidate, tCandidate.ulAnchorDay, &tResult));
    assert(!MOMOMEMORY_ProcessDay(NULL, 1U, &tResult));
}

int main(void)
{
    TEST_Dates();
    TEST_EventsAndWriteGate();
    TEST_InvalidRtcRollbackAndRecovery();
    TEST_RollingAndSkippedDays();
    TEST_PackingIntegrityAndBitBoundaries();
    TEST_DualSlotGenerationWrap();
    TEST_ClearCandidateAndCommit();
    printf("momo_memory_calendar_host_test: PASS\n");
    return 0;
}
