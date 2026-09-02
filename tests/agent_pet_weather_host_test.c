#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "agent_pet_protocol.h"
#include "agent_pet_weather.h"

#define TEST_TICKS_PER_SECOND (1000U)
#define TEST_OBSERVED_UTC (1786147200UL)

#define TEST_ASSERT(expression) do { \
    if (!(expression)) \
    { \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", \
                      __FILE__, __LINE__, #expression); \
        assert(expression); \
    } \
} while (0)

static void TEST_WriteLe16(uint8_t *pData, uint16_t usValue)
{
    pData[0] = (uint8_t)usValue;
    pData[1] = (uint8_t)(usValue >> 8U);

    return;
}

static void TEST_WriteLe32(uint8_t *pData, uint32_t ulValue)
{
    pData[0] = (uint8_t)ulValue;
    pData[1] = (uint8_t)(ulValue >> 8U);
    pData[2] = (uint8_t)(ulValue >> 16U);
    pData[3] = (uint8_t)(ulValue >> 24U);

    return;
}

static void TEST_BuildPayload(
    uint8_t *pPayload,
    uint8_t ucCondition,
    int16_t sTemperatureDeciC,
    uint32_t ulObservedUtc,
    uint16_t usTtlMinutes,
    uint8_t ucFlags)
{
    (void)memset(pPayload, 0, AGENTPET_WEATHER_PAYLOAD_SIZE);
    pPayload[0] = ucCondition;
    TEST_WriteLe16(&pPayload[1], (uint16_t)sTemperatureDeciC);
    TEST_WriteLe32(&pPayload[3], ulObservedUtc);
    TEST_WriteLe16(&pPayload[7], usTtlMinutes);
    pPayload[9] = ucFlags;

    return;
}

static void TEST_BuildFrame(
    uint8_t *pFrame,
    uint16_t usSequence,
    const uint8_t *pPayload)
{
    (void)memset(pFrame, 0, AGENTPET_FRAME_SIZE);
    pFrame[0] = 0x41U;
    pFrame[1] = 0x50U;
    pFrame[2] = 1U;
    pFrame[3] = 5U;
    TEST_WriteLe16(&pFrame[4], usSequence);
    pFrame[6] = 0U;
    pFrame[7] = 1U;
    pFrame[8] = AGENTPET_WEATHER_PAYLOAD_SIZE;
    (void)memcpy(&pFrame[9], pPayload, AGENTPET_WEATHER_PAYLOAD_SIZE);
    pFrame[19] = AGENTPET_Crc8Atm(pFrame, 19U);

    return;
}

static void TEST_LegalEnumsAndBoundaries(void)
{
    uint8_t aPayload[AGENTPET_WEATHER_PAYLOAD_SIZE];
    AGENTPET_WEATHER_SNAPSHOT tSnapshot;
    uint8_t ucCondition;

    AGENTPETWEATHER_Init();
    for (ucCondition = AGENTPET_WEATHER_UNKNOWN;
         ucCondition <= AGENTPET_WEATHER_STORM;
         ucCondition++)
    {
        TEST_BuildPayload(
            aPayload,
            ucCondition,
            (AGENTPET_WEATHER_UNKNOWN == ucCondition) ? -500 : 600,
            TEST_OBSERVED_UTC,
            (AGENTPET_WEATHER_UNKNOWN == ucCondition) ? 15U : 360U,
            0U);
        TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
            AGENTPETWEATHER_ProcessPayload(
                (uint16_t)(ucCondition + 1U),
                aPayload,
                sizeof(aPayload),
                (uint32_t)ucCondition));
        TEST_ASSERT(AGENTPETWEATHER_GetSnapshot(&tSnapshot, NULL));
        TEST_ASSERT(ucCondition == tSnapshot.ucCondition);
    }
}

static void TEST_InvalidPayloadKeepsOldSnapshot(void)
{
    uint8_t aPayload[AGENTPET_WEATHER_PAYLOAD_SIZE];
    AGENTPET_WEATHER_SNAPSHOT tBefore;
    AGENTPET_WEATHER_SNAPSHOT tAfter;
    AGENTPET_WEATHER_DIAGNOSTICS tDiagnostics;

    AGENTPETWEATHER_Init();
    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_CLEAR, 250,
                      TEST_OBSERVED_UTC, 60U, 0U);
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
        AGENTPETWEATHER_ProcessPayload(1U, aPayload, sizeof(aPayload), 10U));
    TEST_ASSERT(AGENTPETWEATHER_GetSnapshot(&tBefore, NULL));

    TEST_ASSERT(AGENTPET_WEATHER_ERROR_INVALID_PARAMETER ==
        AGENTPETWEATHER_ProcessPayload(2U, NULL, sizeof(aPayload), 11U));
    TEST_ASSERT(AGENTPET_WEATHER_ERROR_LENGTH ==
        AGENTPETWEATHER_ProcessPayload(2U, aPayload, sizeof(aPayload) - 1U, 11U));
    aPayload[0] = 6U;
    TEST_ASSERT(AGENTPET_WEATHER_ERROR_CONDITION ==
        AGENTPETWEATHER_ProcessPayload(2U, aPayload, sizeof(aPayload), 11U));
    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_CLEAR, -501,
                      TEST_OBSERVED_UTC, 60U, 0U);
    TEST_ASSERT(AGENTPET_WEATHER_ERROR_TEMPERATURE ==
        AGENTPETWEATHER_ProcessPayload(2U, aPayload, sizeof(aPayload), 11U));
    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_CLEAR, 601,
                      TEST_OBSERVED_UTC, 60U, 0U);
    TEST_ASSERT(AGENTPET_WEATHER_ERROR_TEMPERATURE ==
        AGENTPETWEATHER_ProcessPayload(2U, aPayload, sizeof(aPayload), 11U));
    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_CLEAR, 250,
                      1577836799UL, 60U, 0U);
    TEST_ASSERT(AGENTPET_WEATHER_ERROR_TIME ==
        AGENTPETWEATHER_ProcessPayload(2U, aPayload, sizeof(aPayload), 11U));
    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_CLEAR, 250,
                      TEST_OBSERVED_UTC, 14U, 0U);
    TEST_ASSERT(AGENTPET_WEATHER_ERROR_TTL ==
        AGENTPETWEATHER_ProcessPayload(2U, aPayload, sizeof(aPayload), 11U));
    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_CLEAR, 250,
                      TEST_OBSERVED_UTC, 361U, 0U);
    TEST_ASSERT(AGENTPET_WEATHER_ERROR_TTL ==
        AGENTPETWEATHER_ProcessPayload(2U, aPayload, sizeof(aPayload), 11U));
    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_CLEAR, 250,
                      TEST_OBSERVED_UTC, 60U, 0x04U);
    TEST_ASSERT(AGENTPET_WEATHER_ERROR_FLAGS ==
        AGENTPETWEATHER_ProcessPayload(2U, aPayload, sizeof(aPayload), 11U));
    aPayload[9] = AGENTPET_WEATHER_FLAG_MASK;
    TEST_ASSERT(AGENTPET_WEATHER_ERROR_FLAGS ==
        AGENTPETWEATHER_ProcessPayload(2U, aPayload, sizeof(aPayload), 11U));

    TEST_ASSERT(AGENTPETWEATHER_GetSnapshot(&tAfter, &tDiagnostics));
    TEST_ASSERT(0 == memcmp(&tBefore, &tAfter, sizeof(tBefore)));
    TEST_ASSERT(10U == tDiagnostics.ulRejectedCount);
}

static void TEST_DuplicateOldAndWrapSequences(void)
{
    uint8_t aPayload[AGENTPET_WEATHER_PAYLOAD_SIZE];
    AGENTPET_WEATHER_DIAGNOSTICS tDiagnostics;
    AGENTPET_WEATHER_SNAPSHOT tSnapshot;
    uint16_t usIndex;

    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_RAIN, 180,
                      TEST_OBSERVED_UTC, 60U, 0U);
    AGENTPETWEATHER_Init();
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
        AGENTPETWEATHER_ProcessPayload(100U, aPayload, sizeof(aPayload), 0U));
    for (usIndex = 0U; usIndex < 100U; usIndex++)
    {
        TEST_ASSERT(AGENTPET_WEATHER_RESULT_DUPLICATE ==
            AGENTPETWEATHER_ProcessPayload(100U, aPayload, sizeof(aPayload), 1U));
    }
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_STALE ==
        AGENTPETWEATHER_ProcessPayload(99U, aPayload, sizeof(aPayload), 2U));
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_STALE ==
        AGENTPETWEATHER_ProcessPayload(
            (uint16_t)(100U + 0x8000U),
            aPayload,
            sizeof(aPayload),
            2U));
    TEST_ASSERT(AGENTPETWEATHER_GetSnapshot(&tSnapshot, &tDiagnostics));
    TEST_ASSERT(100U == tSnapshot.usSequence);
    TEST_ASSERT(100U == tDiagnostics.ulDuplicateCount);
    TEST_ASSERT(2U == tDiagnostics.ulStaleCount);

    AGENTPETWEATHER_Init();
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
        AGENTPETWEATHER_ProcessPayload(65534U, aPayload, sizeof(aPayload), 0U));
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
        AGENTPETWEATHER_ProcessPayload(65535U, aPayload, sizeof(aPayload), 1U));
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
        AGENTPETWEATHER_ProcessPayload(0U, aPayload, sizeof(aPayload), 2U));
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
        AGENTPETWEATHER_ProcessPayload(1U, aPayload, sizeof(aPayload), 3U));
}

static void TEST_FreshnessRtcFallbackAndTickWrap(void)
{
    uint8_t aPayload[AGENTPET_WEATHER_PAYLOAD_SIZE];
    AGENTPET_WEATHER_SNAPSHOT tSnapshot;

    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_SNOW, -20,
                      TEST_OBSERVED_UTC, 120U, 0U);
    AGENTPETWEATHER_Init();
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
        AGENTPETWEATHER_ProcessPayload(
            1U, aPayload, sizeof(aPayload), UINT32_MAX - 500U));
    TEST_ASSERT(AGENTPETWEATHER_GetSnapshot(&tSnapshot, NULL));
    TEST_ASSERT(AGENTPET_WEATHER_FRESHNESS_RTC ==
        AGENTPETWEATHER_EvaluateFreshness(
            &tSnapshot, TEST_OBSERVED_UTC + 7199U, 0U,
            TEST_TICKS_PER_SECOND, true));
    TEST_ASSERT(AGENTPET_WEATHER_FRESHNESS_EXPIRED ==
        AGENTPETWEATHER_EvaluateFreshness(
            &tSnapshot, TEST_OBSERVED_UTC + 7200U, 0U,
            TEST_TICKS_PER_SECOND, true));
    TEST_ASSERT(AGENTPET_WEATHER_FRESHNESS_MONOTONIC ==
        AGENTPETWEATHER_EvaluateFreshness(
            &tSnapshot, TEST_OBSERVED_UTC - 1U, 499U,
            TEST_TICKS_PER_SECOND, true));
    TEST_ASSERT(AGENTPET_WEATHER_FRESHNESS_MONOTONIC ==
        AGENTPETWEATHER_EvaluateFreshness(
            &tSnapshot, 0U, 3599498U,
            TEST_TICKS_PER_SECOND, false));
    TEST_ASSERT(AGENTPET_WEATHER_FRESHNESS_EXPIRED ==
        AGENTPETWEATHER_EvaluateFreshness(
            &tSnapshot, 0U, 3599499U,
            TEST_TICKS_PER_SECOND, false));
    TEST_ASSERT(AGENTPET_WEATHER_FRESHNESS_EMPTY ==
        AGENTPETWEATHER_EvaluateFreshness(
            &tSnapshot, 0U, 0U, 0U, false));
    AGENTPETWEATHER_Init();
    TEST_ASSERT(!AGENTPETWEATHER_GetSnapshot(&tSnapshot, NULL));
}

static void TEST_InteractionAndCooldown(void)
{
    uint8_t aPayload[AGENTPET_WEATHER_PAYLOAD_SIZE];
    AGENTPET_WEATHER_SNAPSHOT tSnapshot;
    uint32_t ulCooldownTicks;

    ulCooldownTicks = AGENTPET_WEATHER_INTERACTION_COOLDOWN_SECONDS *
        TEST_TICKS_PER_SECOND;
    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_CLEAR, 250,
                      TEST_OBSERVED_UTC, 60U, 0U);
    AGENTPETWEATHER_Init();
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
        AGENTPETWEATHER_ProcessPayload(1U, aPayload, sizeof(aPayload), 100U));
    TEST_ASSERT(AGENTPETWEATHER_GetSnapshot(&tSnapshot, NULL));
    TEST_ASSERT(AGENTPETWEATHER_CanInteract(
        &tSnapshot, 100U, TEST_TICKS_PER_SECOND));
    TEST_ASSERT(AGENTPETWEATHER_ClaimInteraction(
        1U, 100U, TEST_TICKS_PER_SECOND));
    TEST_ASSERT(!AGENTPETWEATHER_ClaimInteraction(
        1U, 100U, TEST_TICKS_PER_SECOND));
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
        AGENTPETWEATHER_ProcessPayload(2U, aPayload, sizeof(aPayload), 101U));
    TEST_ASSERT(AGENTPETWEATHER_GetSnapshot(&tSnapshot, NULL));
    TEST_ASSERT(!AGENTPETWEATHER_CanInteract(
        &tSnapshot, 100U + ulCooldownTicks - 1U,
        TEST_TICKS_PER_SECOND));
    TEST_ASSERT(AGENTPETWEATHER_CanInteract(
        &tSnapshot, 100U + ulCooldownTicks,
        TEST_TICKS_PER_SECOND));

    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_STORM, 250,
                      TEST_OBSERVED_UTC, 60U, 0U);
    TEST_ASSERT(AGENTPET_WEATHER_RESULT_PUBLISHED ==
        AGENTPETWEATHER_ProcessPayload(3U, aPayload, sizeof(aPayload), 102U));
    TEST_ASSERT(AGENTPETWEATHER_GetSnapshot(&tSnapshot, NULL));
    TEST_ASSERT(!AGENTPETWEATHER_CanInteract(
        &tSnapshot, 100U + (2U * ulCooldownTicks),
        TEST_TICKS_PER_SECOND));
}

static void TEST_PriorityMatrix(void)
{
    AGENTPET_WEATHER_PRIORITY_INPUT tInput;
    AGENTPET_WEATHER_PRESENTATION eExpected;
    uint8_t ucCase;

    for (ucCase = 0U; ucCase < 20U; ucCase++)
    {
        (void)memset(&tInput, 0, sizeof(tInput));
        tInput.bFeatureEnabled = true;
        tInput.bSnapshotFresh = true;
        tInput.bInteractionAvailable = (0U == (ucCase & 1U));
        if (1U == ucCase)
        {
            tInput.bFeatureEnabled = false;
        }
        else if (2U == ucCase)
        {
            tInput.bSnapshotFresh = false;
        }
        else if (3U == ucCase)
        {
            tInput.bImageTransferActive = true;
        }
        else if ((4U <= ucCase) && (6U >= ucCase))
        {
            tInput.bAgentBlocksWeather = true;
        }
        else if ((7U == ucCase) || (8U == ucCase))
        {
            tInput.bTypingActive = true;
        }
        else if ((9U == ucCase) || (10U == ucCase))
        {
            tInput.bRemoteExpressionActive = true;
        }
        else if ((11U <= ucCase) && (13U >= ucCase))
        {
            tInput.bQuestFeedbackPending = true;
        }
        else if (14U == ucCase)
        {
            tInput.bImageTransferActive = true;
            tInput.bTypingActive = true;
        }
        else if (15U == ucCase)
        {
            tInput.bAgentBlocksWeather = true;
            tInput.bRemoteExpressionActive = true;
        }
        else if (16U == ucCase)
        {
            tInput.bQuestFeedbackPending = true;
            tInput.bTypingActive = true;
        }

        eExpected = (0U == (ucCase & 1U)) ?
            AGENTPET_WEATHER_PRESENTATION_INTERACTION :
            AGENTPET_WEATHER_PRESENTATION_AMBIENCE;
        if ((1U <= ucCase) && (16U >= ucCase))
        {
            eExpected = AGENTPET_WEATHER_PRESENTATION_HIDDEN;
        }
        TEST_ASSERT(eExpected ==
            AGENTPETWEATHER_SelectPresentation(&tInput));
    }
}

static void TEST_ProtocolIntegration(void)
{
    uint8_t aPayload[AGENTPET_WEATHER_PAYLOAD_SIZE];
    uint8_t aFrame[AGENTPET_FRAME_SIZE];
    AGENTPET_WEATHER_SNAPSHOT tSnapshot;

    TEST_BuildPayload(aPayload, AGENTPET_WEATHER_CLOUDY, 205,
                      TEST_OBSERVED_UTC, 45U, 0U);
    TEST_BuildFrame(aFrame, 77U, aPayload);
    AGENTPET_ProtocolInit();
    TEST_ASSERT(AGENTPET_RESULT_WEATHER_PUBLISHED ==
        AGENTPET_ProcessFrameAt(aFrame, sizeof(aFrame), 1234U));
    TEST_ASSERT(AGENTPETWEATHER_GetSnapshot(&tSnapshot, NULL));
    TEST_ASSERT(77U == tSnapshot.usSequence);
    TEST_ASSERT(1234U == tSnapshot.ulReceivedMonotonicTicks);
    TEST_ASSERT(AGENTPET_RESULT_DUPLICATE ==
        AGENTPET_ProcessFrameAt(aFrame, sizeof(aFrame), 1235U));
    aFrame[19] ^= 0x01U;
    TEST_ASSERT(AGENTPET_ERROR_CRC ==
        AGENTPET_ProcessFrameAt(aFrame, sizeof(aFrame), 1236U));
    aFrame[19] ^= 0x01U;
    aFrame[2] = 2U;
    aFrame[19] = AGENTPET_Crc8Atm(aFrame, 19U);
    TEST_ASSERT(AGENTPET_ERROR_HEADER ==
        AGENTPET_ProcessFrameAt(aFrame, sizeof(aFrame), 1237U));
}

int main(void)
{
    TEST_LegalEnumsAndBoundaries();
    TEST_InvalidPayloadKeepsOldSnapshot();
    TEST_DuplicateOldAndWrapSequences();
    TEST_FreshnessRtcFallbackAndTickWrap();
    TEST_InteractionAndCooldown();
    TEST_PriorityMatrix();
    TEST_ProtocolIntegration();
    (void)printf("agent_pet_weather_host_test: PASS\n");

    return 0;
}
