#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "momo_stroking.h"

#define TEST_ASSERT(condition)                                                   \
    do                                                                           \
    {                                                                            \
        if (!(condition))                                                        \
        {                                                                        \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n",                           \
                          __FILE__, __LINE__, #condition);                        \
            exit(1);                                                             \
        }                                                                        \
    } while (0)

static MOMO_STROKE_CONFIG Local_Config(void)
{
    MOMO_STROKE_CONFIG tConfig;

    MOMOSTROKE_GetDefaultConfig(&tConfig);
    return tConfig;
}

static void Local_TestDwellAndRelease(void)
{
    MOMO_STROKE_CONFIG tConfig;
    MOMO_STROKE_CONTEXT tContext;

    tConfig = Local_Config();
    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 96, 50, 1000U));
    TEST_ASSERT(MOMO_STROKE_EVENT_NONE ==
                MOMOSTROKE_Poll(&tContext, &tConfig, 1649U));
    TEST_ASSERT(MOMO_STROKE_EVENT_STARTED ==
                MOMOSTROKE_Poll(&tContext, &tConfig, 1650U));
    TEST_ASSERT(MOMOSTROKE_IsConsumed(&tContext));
    TEST_ASSERT(MOMO_STROKE_EVENT_NONE ==
                MOMOSTROKE_Release(&tContext, 1700U));
    TEST_ASSERT(MOMO_STROKE_EVENT_NONE ==
                MOMOSTROKE_Poll(&tContext, &tConfig, 1849U));
    TEST_ASSERT(MOMO_STROKE_EVENT_ENDED ==
                MOMOSTROKE_Poll(&tContext, &tConfig, 1850U));
    TEST_ASSERT(MOMO_STROKE_STATE_IDLE == MOMOSTROKE_GetState(&tContext));
    TEST_ASSERT(MOMOSTROKE_IsConsumed(&tContext));
}

static void Local_TestGentleAndBoundaries(void)
{
    MOMO_STROKE_CONFIG tConfig;
    MOMO_STROKE_CONTEXT tContext;

    tConfig = Local_Config();
    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 80, 50, 0U));
    TEST_ASSERT(MOMO_STROKE_EVENT_NONE ==
                MOMOSTROKE_Sample(&tContext, &tConfig, 91, 50, 349U));

    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 80, 50, 0U));
    TEST_ASSERT(MOMO_STROKE_EVENT_STARTED ==
                MOMOSTROKE_Sample(&tContext, &tConfig, 92, 50, 350U));

    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 38, 10, 0U));
    TEST_ASSERT(MOMO_STROKE_EVENT_STARTED ==
                MOMOSTROKE_Poll(&tContext, &tConfig, 650U));
    TEST_ASSERT(MOMO_STROKE_EVENT_NONE ==
                MOMOSTROKE_Sample(&tContext, &tConfig, 37, 10, 750U));
    TEST_ASSERT(MOMO_STROKE_EVENT_NONE ==
                MOMOSTROKE_Poll(&tContext, &tConfig, 999U));
    TEST_ASSERT(MOMO_STROKE_EVENT_ENDED ==
                MOMOSTROKE_Poll(&tContext, &tConfig, 1000U));

    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(!MOMOSTROKE_Press(&tContext, &tConfig, 37, 10, 0U));
    TEST_ASSERT(!MOMOSTROKE_Press(&tContext, &tConfig, 38, 9, 0U));
    TEST_ASSERT(!MOMOSTROKE_Press(&tContext, &tConfig, 192, 50, 0U));
}

static void Local_TestNegativeSequences(void)
{
    MOMO_STROKE_CONFIG tConfig;
    MOMO_STROKE_CONTEXT tContext;
    uint16_t usIndex;

    tConfig = Local_Config();
    for (usIndex = 0U; usIndex < 100U; usIndex++)
    {
        MOMOSTROKE_Init(&tContext);
        TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 96, 50, 0U));
        TEST_ASSERT(MOMO_STROKE_EVENT_NONE ==
                    MOMOSTROKE_Release(&tContext, 120U));
        TEST_ASSERT(!MOMOSTROKE_IsConsumed(&tContext));

        TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 96, 50, 200U));
        TEST_ASSERT(MOMO_STROKE_EVENT_CANCELLED ==
                    MOMOSTROKE_Sample(&tContext, &tConfig, 180, 50, 300U));
        TEST_ASSERT(!MOMOSTROKE_IsConsumed(&tContext));
    }
}

static void Local_TestDistanceAndRatioBoundaries(void)
{
    MOMO_STROKE_CONFIG tConfig;
    MOMO_STROKE_CONTEXT tContext;
    uint16_t usIndex;

    tConfig = Local_Config();
    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 70, 50, 0U));
    TEST_ASSERT(MOMO_STROKE_EVENT_STARTED ==
                MOMOSTROKE_Sample(&tContext, &tConfig, 150, 50, 350U));

    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 70, 50, 0U));
    TEST_ASSERT(MOMO_STROKE_EVENT_CANCELLED ==
                MOMOSTROKE_Sample(&tContext, &tConfig, 151, 50, 350U));

    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 38, 10, 0U));
    TEST_ASSERT(MOMO_STROKE_EVENT_STARTED ==
                MOMOSTROKE_Sample(&tContext, &tConfig, 50, 10, 350U));

    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 38, 10, 0U));
    tConfig.usMaxStepDistance = 160U;
    TEST_ASSERT(MOMO_STROKE_EVENT_STARTED ==
                MOMOSTROKE_Sample(&tContext, &tConfig, 154, 54, 350U));

    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 38, 10, 0U));
    TEST_ASSERT(MOMO_STROKE_EVENT_CANCELLED ==
                MOMOSTROKE_Sample(&tContext, &tConfig, 154, 55, 350U));

    tConfig = Local_Config();
    tConfig.usGentleMs = 900U;
    tConfig.usGentleMinDistance = 0U;
    tConfig.usStationaryDistance = 0U;
    tConfig.ucInsidePercent = 71U;
    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 154, 50, 0U));
    for (usIndex = 1U; usIndex <= 3U; usIndex++)
    {
        TEST_ASSERT(MOMO_STROKE_EVENT_NONE == MOMOSTROKE_Sample(
            &tContext, &tConfig, 155, 50, (uint32_t)usIndex * 100U));
    }
    for (usIndex = 4U; usIndex <= 9U; usIndex++)
    {
        TEST_ASSERT(MOMO_STROKE_EVENT_NONE == MOMOSTROKE_Sample(
            &tContext, &tConfig, 154, 50, (uint32_t)usIndex * 100U));
    }
    TEST_ASSERT(MOMO_STROKE_STATE_CANDIDATE == MOMOSTROKE_GetState(&tContext));
    tConfig.ucInsidePercent = 70U;
    TEST_ASSERT(MOMO_STROKE_EVENT_STARTED ==
                MOMOSTROKE_Poll(&tContext, &tConfig, 900U));
}

static void Local_TestInvalidCancelAndStageExit(void)
{
    MOMO_STROKE_CONFIG tConfig;
    MOMO_STROKE_CONTEXT tContext;

    tConfig = Local_Config();
    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(!MOMOSTROKE_Press(NULL, &tConfig, 96, 50, 0U));
    TEST_ASSERT(!MOMOSTROKE_Press(&tContext, NULL, 96, 50, 0U));
    tConfig.usSampleMs = 0U;
    TEST_ASSERT(!MOMOSTROKE_Press(&tContext, &tConfig, 96, 50, 0U));
    TEST_ASSERT(MOMO_STROKE_EVENT_NONE == MOMOSTROKE_Poll(NULL, &tConfig, 0U));
    TEST_ASSERT(MOMO_STROKE_EVENT_NONE == MOMOSTROKE_Cancel(NULL));
    TEST_ASSERT(MOMO_STROKE_EVENT_NONE == MOMOSTROKE_Preempt(NULL));

    tConfig = Local_Config();
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 96, 50, 0U));
    TEST_ASSERT(MOMO_STROKE_EVENT_CANCELLED ==
                MOMOSTROKE_Sample(&tContext, &tConfig, -1, 50, 100U));
    TEST_ASSERT(!MOMOSTROKE_IsConsumed(&tContext));

    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 96, 50, 200U));
    TEST_ASSERT(MOMO_STROKE_EVENT_CANCELLED == MOMOSTROKE_Cancel(&tContext));
    TEST_ASSERT(!MOMOSTROKE_IsConsumed(&tContext));

    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 96, 50, 400U));
    TEST_ASSERT(MOMO_STROKE_EVENT_STARTED ==
                MOMOSTROKE_Poll(&tContext, &tConfig, 1050U));
    TEST_ASSERT(MOMO_STROKE_EVENT_ENDED == MOMOSTROKE_Cancel(&tContext));
    TEST_ASSERT(MOMOSTROKE_IsConsumed(&tContext));
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 96, 50, 1100U));
    TEST_ASSERT(!MOMOSTROKE_IsConsumed(&tContext));

    TEST_ASSERT(MOMO_STROKE_EVENT_ENDED == MOMOSTROKE_Preempt(&tContext));
    TEST_ASSERT(MOMOSTROKE_IsConsumed(&tContext));
    TEST_ASSERT(MOMO_STROKE_STATE_IDLE == MOMOSTROKE_GetState(&tContext));
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 96, 50, 1200U));
    TEST_ASSERT(!MOMOSTROKE_IsConsumed(&tContext));
}

static void Local_TestPositiveRecognitionCount(void)
{
    MOMO_STROKE_CONFIG tConfig;
    MOMO_STROKE_CONTEXT tContext;
    uint16_t usIndex;
    uint16_t usStarted;

    tConfig = Local_Config();
    usStarted = 0U;
    for (usIndex = 0U; usIndex < 100U; usIndex++)
    {
        MOMOSTROKE_Init(&tContext);
        TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 80, 50, 0U));
        if (MOMO_STROKE_EVENT_STARTED == MOMOSTROKE_Sample(
                &tContext, &tConfig, 92, 50, 350U))
        {
            usStarted++;
        }
    }
    TEST_ASSERT(100U == usStarted);
}

static void Local_TestTickWrap(void)
{
    MOMO_STROKE_CONFIG tConfig;
    MOMO_STROKE_CONTEXT tContext;
    uint32_t ulStart;

    tConfig = Local_Config();
    ulStart = UINT32_MAX - 300U;
    MOMOSTROKE_Init(&tContext);
    TEST_ASSERT(MOMOSTROKE_Press(&tContext, &tConfig, 96, 50, ulStart));
    TEST_ASSERT(MOMO_STROKE_EVENT_STARTED ==
                MOMOSTROKE_Poll(&tContext, &tConfig, ulStart + 650U));
    TEST_ASSERT(MOMO_STROKE_EVENT_NONE ==
                MOMOSTROKE_Release(&tContext, ulStart + 700U));
    TEST_ASSERT(MOMO_STROKE_EVENT_ENDED ==
                MOMOSTROKE_Poll(&tContext, &tConfig, ulStart + 850U));
}

static void Local_TestStress(void)
{
    MOMO_STROKE_CONFIG tConfig;
    MOMO_STROKE_CONTEXT tContext;
    uint16_t usIndex;
    uint32_t ulTick;

    tConfig = Local_Config();
    MOMOSTROKE_Init(&tContext);
    ulTick = 0U;
    for (usIndex = 0U; usIndex < 1000U; usIndex++)
    {
        TEST_ASSERT(MOMOSTROKE_Press(
            &tContext, &tConfig, 80, 50, ulTick));
        TEST_ASSERT(MOMO_STROKE_EVENT_STARTED == MOMOSTROKE_Sample(
            &tContext, &tConfig, 92, 50, ulTick + 350U));
        TEST_ASSERT(MOMO_STROKE_EVENT_NONE ==
                    MOMOSTROKE_Release(&tContext, ulTick + 360U));
        TEST_ASSERT(MOMO_STROKE_EVENT_ENDED == MOMOSTROKE_Poll(
            &tContext, &tConfig, ulTick + 510U));
        TEST_ASSERT(MOMO_STROKE_STATE_IDLE == MOMOSTROKE_GetState(&tContext));
        ulTick += 600U;
    }
}

int main(void)
{
    Local_TestDwellAndRelease();
    Local_TestGentleAndBoundaries();
    Local_TestNegativeSequences();
    Local_TestDistanceAndRatioBoundaries();
    Local_TestInvalidCancelAndStageExit();
    Local_TestPositiveRecognitionCount();
    Local_TestTickWrap();
    Local_TestStress();
    (void)printf("PASS momo_stroking_host_test\n");
    return 0;
}
