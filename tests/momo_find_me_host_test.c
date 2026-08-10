#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "momo_find_me.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
    return EXIT_FAILURE; } } while (0)

static int test_duplicate_and_stop(void)
{
    MOMO_FIND_SNAPSHOT before;
    MOMO_FIND_SNAPSHOT after;
    MOMO_FIND_END_EVENT event;
    bool wake;
    unsigned int index;

    MOMOFIND_Init();
    CHECK(MOMO_FIND_RESULT_ACCEPTED == MOMOFIND_StartAt(10U, 100U, 60000U,
                                                        1000U, false, false, &wake));
    CHECK(wake && MOMOFIND_ConfirmStart(10U));
    CHECK(MOMOFIND_GetSnapshot(&before));
    for (index = 0U; index < 100U; index++)
    {
        wake = true;
        CHECK(MOMO_FIND_RESULT_ACCEPTED == MOMOFIND_StartAt(
            10U, 100U + index, 60000U, 1000U, false, false, &wake));
        CHECK(!wake);
    }
    CHECK(MOMOFIND_GetSnapshot(&after));
    CHECK(before.ulStartTick == after.ulStartTick);
    CHECK(before.ulDurationTicks == after.ulDurationTicks);
    CHECK(before.ulGeneration == after.ulGeneration);
    CHECK(before.ulAcceptedCount == after.ulAcceptedCount);
    CHECK(100U == after.ulDuplicateCount);
    CHECK(MOMO_FIND_RESULT_ALREADY_ACTIVE == MOMOFIND_StartAt(
        11U, 200U, 60000U, 1000U, false, false, &wake));

    CHECK(MOMO_FIND_RESULT_STOPPING == MOMOFIND_StopAt(
        MOMO_FIND_END_PHONE_STOP, 300U));
    for (index = 0U; index < 100U; index++)
    {
        CHECK(MOMO_FIND_RESULT_STOPPING == MOMOFIND_StopAt(
            MOMO_FIND_END_PHONE_STOP, 301U + index));
    }
    CHECK(MOMOFIND_AcknowledgeUiStopped(500U));
    CHECK(MOMOFIND_PeekEndEvent(&event));
    CHECK((10U == event.usSessionId) &&
          (MOMO_FIND_END_PHONE_STOP == event.eReason));
    CHECK(MOMO_FIND_RESULT_RATE_LIMIT == MOMOFIND_StartAt(
        12U, 2000U, 60000U, 1000U, false, false, &wake));
    MOMOFIND_AcknowledgeEndEvent();
    CHECK(MOMO_FIND_RESULT_STALE == MOMOFIND_StartAt(
        10U, 2000U, 60000U, 1000U, false, false, &wake));

    return EXIT_SUCCESS;
}

static int test_timeout_and_wrap(void)
{
    MOMO_FIND_SNAPSHOT snapshot;
    bool wake;

    MOMOFIND_Init();
    CHECK(MOMO_FIND_RESULT_ACCEPTED == MOMOFIND_StartAt(
        1U, UINT32_MAX - 20U, 100U, 0U, false, false, &wake));
    CHECK(MOMOFIND_ConfirmStart(1U));
    MOMOFIND_PollAt(29U);
    CHECK(MOMOFIND_GetSnapshot(&snapshot));
    CHECK(MOMO_FIND_STATE_ACTIVE == snapshot.eState);
    MOMOFIND_PollAt(79U);
    CHECK(MOMOFIND_GetSnapshot(&snapshot));
    CHECK((MOMO_FIND_STATE_STOPPING == snapshot.eState) &&
          (MOMO_FIND_END_TIMEOUT == snapshot.eEndReason));

    MOMOFIND_Init();
    CHECK(MOMO_FIND_RESULT_ACCEPTED == MOMOFIND_StartAt(
        2U, 10U, 60000U, 0U, false, false, &wake));
    CHECK(MOMOFIND_ConfirmStart(2U));
    MOMOFIND_PollAt(60009U);
    CHECK(MOMOFIND_GetSnapshot(&snapshot));
    CHECK(MOMO_FIND_STATE_ACTIVE == snapshot.eState);
    CHECK(1U == MOMOFIND_GetRemainingSeconds(&snapshot, 60009U, 1000U));
    MOMOFIND_PollAt(60010U);
    CHECK(MOMOFIND_GetSnapshot(&snapshot));
    CHECK(MOMO_FIND_STATE_STOPPING == snapshot.eState);

    MOMOFIND_Init();
    CHECK(MOMO_FIND_RESULT_ACCEPTED == MOMOFIND_StartAt(
        3U, 0U, UINT32_MAX, 0U, false, false, &wake));
    CHECK(MOMOFIND_ConfirmStart(3U));
    CHECK(MOMOFIND_GetSnapshot(&snapshot));
    CHECK(4294968U == MOMOFIND_GetRemainingSeconds(&snapshot, 0U, 1000U));

    return EXIT_SUCCESS;
}

static int test_priority_matrix(void)
{
    MOMO_FIND_SNAPSHOT snapshot;
    bool wake;
    unsigned int index;

    for (index = 0U; index < 24U; index++)
    {
        bool alarm = (0U == (index % 3U));
        bool transfer = (0U == (index % 2U));
        MOMO_FIND_RESULT expected = alarm ? MOMO_FIND_RESULT_BUSY_ALARM :
            (transfer ? MOMO_FIND_RESULT_BUSY_TRANSFER : MOMO_FIND_RESULT_ACCEPTED);

        MOMOFIND_Init();
        CHECK(expected == MOMOFIND_StartAt((uint16_t)(100U + index), index,
                                            100U, 0U, alarm, transfer, &wake));
        if (MOMO_FIND_RESULT_ACCEPTED == expected)
        {
            CHECK(wake && MOMOFIND_ConfirmStart((uint16_t)(100U + index)));
            if (0U == (index % 4U))
            {
                MOMOFIND_PreemptAlarm(index + 1U);
                CHECK(MOMOFIND_GetSnapshot(&snapshot));
                CHECK((MOMO_FIND_STATE_STOPPING == snapshot.eState) &&
                      (MOMO_FIND_END_PREEMPTED_ALARM == snapshot.eEndReason));
            }
            else
            {
                CHECK(MOMO_FIND_RESULT_STOPPING == MOMOFIND_StopAt(
                    MOMO_FIND_END_USER_STOP, index + 1U));
            }
        }
    }

    MOMOFIND_Init();
    CHECK(MOMO_FIND_RESULT_ACCEPTED == MOMOFIND_StartAt(
        65535U, 0U, 100U, 0U, false, false, &wake));
    MOMOFIND_PreemptAlarm(0U);
    CHECK(!MOMOFIND_ConfirmStart(65535U));
    CHECK(MOMOFIND_GetSnapshot(&snapshot));
    CHECK((MOMO_FIND_STATE_STOPPING == snapshot.eState) &&
          (MOMO_FIND_END_PREEMPTED_ALARM == snapshot.eEndReason));
    MOMOFIND_Init();
    CHECK(MOMO_FIND_RESULT_ACCEPTED == MOMOFIND_StartAt(
        65535U, 0U, 100U, 0U, false, false, &wake));
    MOMOFIND_CancelArming(65535U);
    CHECK(MOMO_FIND_RESULT_ACCEPTED == MOMOFIND_StartAt(
        1U, 1U, 100U, 0U, false, false, &wake));
    CHECK(MOMOFIND_ConfirmStart(1U));

    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(EXIT_SUCCESS == test_duplicate_and_stop());
    CHECK(EXIT_SUCCESS == test_timeout_and_wrap());
    CHECK(EXIT_SUCCESS == test_priority_matrix());
    puts("momo_find_me_host_test: PASS");
    return EXIT_SUCCESS;
}
