#include "agent_pet_weather.h"

#include <limits.h>
#include <string.h>

#define AGENTPET_WEATHER_TIME_MIN (1577836800UL)
#define AGENTPET_WEATHER_TIME_MAX (2145916800UL)
#define AGENTPET_WEATHER_TEMPERATURE_MIN (-500)
#define AGENTPET_WEATHER_TEMPERATURE_MAX (600)
#define AGENTPET_WEATHER_SEQUENCE_HALF_RANGE (0x8000U)

/* Latest validated weather context. Writers and readers are protected by the
 * Agent Pet BLE service critical section on target firmware. */
static AGENTPET_WEATHER_SNAPSHOT l_tSnapshot;
/* Protocol diagnostics use saturating counters to avoid misleading wrap. */
static AGENTPET_WEATHER_DIAGNOSTICS l_tDiagnostics;
/* True only after one complete weather payload has been published. */
static bool l_bHasSnapshot;
/* Last locally claimed sequence; remote Agent/task state is never modified. */
static uint16_t l_usClaimedSequence;
/* Last successful claim time in monotonic seconds. */
static uint32_t l_ulLastClaimMonotonicTicks;
/* Claim flags distinguish sequence zero and monotonic time zero from empty. */
static bool l_bHasClaimedSequence;
static bool l_bHasLastClaimTime;

static uint16_t Local_ReadLe16(const uint8_t *pData)
{
    uint16_t usValue;

    usValue = (uint16_t)pData[0];
    usValue |= (uint16_t)((uint16_t)pData[1] << 8U);

    return usValue;
}

static uint32_t Local_ReadLe32(const uint8_t *pData)
{
    uint32_t ulValue;

    ulValue = (uint32_t)pData[0];
    ulValue |= (uint32_t)pData[1] << 8U;
    ulValue |= (uint32_t)pData[2] << 16U;
    ulValue |= (uint32_t)pData[3] << 24U;

    return ulValue;
}

static void Local_IncrementSaturated(uint32_t *pCounter)
{
    if ((NULL != pCounter) && (UINT32_MAX != *pCounter))
    {
        (*pCounter)++;
    }

    return;
}

static AGENTPET_WEATHER_RESULT Local_Reject(
    AGENTPET_WEATHER_RESULT eResult)
{
    Local_IncrementSaturated(&l_tDiagnostics.ulRejectedCount);

    return eResult;
}

/*
 * AGENTPETWEATHER_Init
 * Function: clear volatile weather, interaction, and diagnostic state.
 * Parameters: none.
 * Return: none.
 */
void AGENTPETWEATHER_Init(void)
{
    (void)memset(&l_tSnapshot, 0, sizeof(l_tSnapshot));
    (void)memset(&l_tDiagnostics, 0, sizeof(l_tDiagnostics));
    l_bHasSnapshot = false;
    l_usClaimedSequence = 0U;
    l_ulLastClaimMonotonicTicks = 0U;
    l_bHasClaimedSequence = false;
    l_bHasLastClaimTime = false;

    return;
}

/*
 * AGENTPETWEATHER_IsSequenceNewer
 * Function: compare 16-bit serials using an unambiguous half-range window.
 * Parameters:
 *   - usCandidate: candidate sequence.
 *   - usReference: currently published sequence.
 * Return: true only when candidate is strictly newer.
 */
bool AGENTPETWEATHER_IsSequenceNewer(
    uint16_t usCandidate,
    uint16_t usReference)
{
    uint16_t usDelta;

    usDelta = (uint16_t)(usCandidate - usReference);

    return (0U != usDelta) &&
        (AGENTPET_WEATHER_SEQUENCE_HALF_RANGE > usDelta);
}

/*
 * AGENTPETWEATHER_ProcessPayload
 * Function: validate and publish one fixed 10-byte weather payload.
 * Parameters:
 *   - usSequence: frame-header sequence.
 *   - pPayload: input weather payload, read-only.
 *   - ulLength: payload length, exactly 10 bytes.
 *   - ulReceivedMonotonicTicks: raw device tick at receipt.
 * Return: publication, duplicate/stale result, or a field error.
 */
AGENTPET_WEATHER_RESULT AGENTPETWEATHER_ProcessPayload(
    uint16_t usSequence,
    const uint8_t *pPayload,
    size_t ulLength,
    uint32_t ulReceivedMonotonicTicks)
{
    AGENTPET_WEATHER_SNAPSHOT tCandidate;

    if (NULL == pPayload)
    {
        return Local_Reject(AGENTPET_WEATHER_ERROR_INVALID_PARAMETER);
    }
    if (AGENTPET_WEATHER_PAYLOAD_SIZE != ulLength)
    {
        return Local_Reject(AGENTPET_WEATHER_ERROR_LENGTH);
    }

    (void)memset(&tCandidate, 0, sizeof(tCandidate));
    tCandidate.ucCondition = pPayload[0];
    tCandidate.sTemperatureDeciC = (int16_t)Local_ReadLe16(&pPayload[1]);
    tCandidate.ulObservedUtc = Local_ReadLe32(&pPayload[3]);
    tCandidate.usTtlMinutes = Local_ReadLe16(&pPayload[7]);
    tCandidate.ucFlags = pPayload[9];
    if (AGENTPET_WEATHER_STORM < tCandidate.ucCondition)
    {
        return Local_Reject(AGENTPET_WEATHER_ERROR_CONDITION);
    }
    if ((AGENTPET_WEATHER_TEMPERATURE_MIN > tCandidate.sTemperatureDeciC) ||
        (AGENTPET_WEATHER_TEMPERATURE_MAX < tCandidate.sTemperatureDeciC))
    {
        return Local_Reject(AGENTPET_WEATHER_ERROR_TEMPERATURE);
    }
    if ((AGENTPET_WEATHER_TIME_MIN > tCandidate.ulObservedUtc) ||
        (AGENTPET_WEATHER_TIME_MAX < tCandidate.ulObservedUtc))
    {
        return Local_Reject(AGENTPET_WEATHER_ERROR_TIME);
    }
    if ((AGENTPET_WEATHER_TTL_MINUTES_MIN > tCandidate.usTtlMinutes) ||
        (AGENTPET_WEATHER_TTL_MINUTES_MAX < tCandidate.usTtlMinutes))
    {
        return Local_Reject(AGENTPET_WEATHER_ERROR_TTL);
    }
    if ((0U != (tCandidate.ucFlags &
                (uint8_t)(~AGENTPET_WEATHER_FLAG_MASK))) ||
        (AGENTPET_WEATHER_FLAG_MASK == tCandidate.ucFlags))
    {
        return Local_Reject(AGENTPET_WEATHER_ERROR_FLAGS);
    }
    if (l_bHasSnapshot)
    {
        if (l_tSnapshot.usSequence == usSequence)
        {
            Local_IncrementSaturated(&l_tDiagnostics.ulDuplicateCount);
            return AGENTPET_WEATHER_RESULT_DUPLICATE;
        }
        if (!AGENTPETWEATHER_IsSequenceNewer(
                usSequence,
                l_tSnapshot.usSequence))
        {
            Local_IncrementSaturated(&l_tDiagnostics.ulStaleCount);
            return AGENTPET_WEATHER_RESULT_STALE;
        }
    }

    tCandidate.usSequence = usSequence;
    tCandidate.ulReceivedMonotonicTicks = ulReceivedMonotonicTicks;
    tCandidate.ulGeneration = l_tSnapshot.ulGeneration + 1U;
    l_tSnapshot = tCandidate;
    l_bHasSnapshot = true;
    Local_IncrementSaturated(&l_tDiagnostics.ulPublishedCount);

    return AGENTPET_WEATHER_RESULT_PUBLISHED;
}

/*
 * AGENTPETWEATHER_GetSnapshot
 * Function: copy the latest weather state and optional diagnostics.
 * Parameters:
 *   - pSnapshot: required output snapshot.
 *   - pDiagnostics: optional output diagnostics.
 * Return: true when a weather snapshot has been published.
 */
bool AGENTPETWEATHER_GetSnapshot(
    AGENTPET_WEATHER_SNAPSHOT *pSnapshot,
    AGENTPET_WEATHER_DIAGNOSTICS *pDiagnostics)
{
    if (NULL == pSnapshot)
    {
        return false;
    }

    *pSnapshot = l_tSnapshot;
    if (NULL != pDiagnostics)
    {
        *pDiagnostics = l_tDiagnostics;
    }

    return l_bHasSnapshot;
}

/*
 * AGENTPETWEATHER_EvaluateFreshness
 * Function: select RTC freshness or the bounded monotonic fallback.
 * Parameters:
 *   - pSnapshot: validated snapshot.
 *   - ulNowUtc: current UTC seconds when RTC is valid.
 *   - ulNowMonotonicTicks: current raw device tick.
 *   - ulTicksPerSecond: nonzero RT tick frequency.
 *   - bRtcValid: true only after trusted time synchronization.
 * Return: empty, fresh by RTC/monotonic source, or expired.
 */
AGENTPET_WEATHER_FRESHNESS AGENTPETWEATHER_EvaluateFreshness(
    const AGENTPET_WEATHER_SNAPSHOT *pSnapshot,
    uint32_t ulNowUtc,
    uint32_t ulNowMonotonicTicks,
    uint32_t ulTicksPerSecond,
    bool bRtcValid)
{
    uint32_t ulElapsedTicks;
    uint32_t ulFallbackMinutes;
    uint64_t udFallbackTicks;
    uint64_t udExpiryUtc;

    if ((NULL == pSnapshot) || (0U == ulTicksPerSecond))
    {
        return AGENTPET_WEATHER_FRESHNESS_EMPTY;
    }
    if (bRtcValid && (ulNowUtc >= pSnapshot->ulObservedUtc))
    {
        udExpiryUtc = (uint64_t)pSnapshot->ulObservedUtc +
            ((uint64_t)pSnapshot->usTtlMinutes * 60ULL);
        if ((uint64_t)ulNowUtc < udExpiryUtc)
        {
            return AGENTPET_WEATHER_FRESHNESS_RTC;
        }

        return AGENTPET_WEATHER_FRESHNESS_EXPIRED;
    }

    ulFallbackMinutes = pSnapshot->usTtlMinutes;
    if (AGENTPET_WEATHER_MONOTONIC_MAX_MINUTES < ulFallbackMinutes)
    {
        ulFallbackMinutes = AGENTPET_WEATHER_MONOTONIC_MAX_MINUTES;
    }
    ulElapsedTicks = ulNowMonotonicTicks -
        pSnapshot->ulReceivedMonotonicTicks;
    udFallbackTicks = (uint64_t)ulFallbackMinutes * 60ULL *
        (uint64_t)ulTicksPerSecond;
    if ((uint64_t)ulElapsedTicks < udFallbackTicks)
    {
        return AGENTPET_WEATHER_FRESHNESS_MONOTONIC;
    }

    return AGENTPET_WEATHER_FRESHNESS_EXPIRED;
}

/*
 * AGENTPETWEATHER_SelectPresentation
 * Function: resolve weather visibility after all higher-priority inputs.
 * Parameters:
 *   - pInput: pure priority input structure.
 * Return: hidden, ambience, or claimable interaction presentation.
 */
AGENTPET_WEATHER_PRESENTATION AGENTPETWEATHER_SelectPresentation(
    const AGENTPET_WEATHER_PRIORITY_INPUT *pInput)
{
    if ((NULL == pInput) ||
        !pInput->bFeatureEnabled ||
        !pInput->bSnapshotFresh ||
        pInput->bImageTransferActive ||
        pInput->bAgentBlocksWeather ||
        pInput->bTypingActive ||
        pInput->bRemoteExpressionActive ||
        pInput->bQuestFeedbackPending)
    {
        return AGENTPET_WEATHER_PRESENTATION_HIDDEN;
    }
    if (pInput->bInteractionAvailable)
    {
        return AGENTPET_WEATHER_PRESENTATION_INTERACTION;
    }

    return AGENTPET_WEATHER_PRESENTATION_AMBIENCE;
}

/*
 * AGENTPETWEATHER_CanInteract
 * Function: check condition eligibility, one-shot sequence, and cooldown.
 * Parameters:
 *   - pSnapshot: current validated snapshot.
 *   - ulNowMonotonicTicks: current raw device tick.
 *   - ulTicksPerSecond: nonzero RT tick frequency.
 * Return: true when a local weather care interaction may be claimed.
 */
bool AGENTPETWEATHER_CanInteract(
    const AGENTPET_WEATHER_SNAPSHOT *pSnapshot,
    uint32_t ulNowMonotonicTicks,
    uint32_t ulTicksPerSecond)
{
    bool bConditionInteractive;
    uint32_t ulElapsedTicks;
    uint64_t udCooldownTicks;

    if ((NULL == pSnapshot) || (0U == ulTicksPerSecond))
    {
        return false;
    }
    bConditionInteractive =
        (AGENTPET_WEATHER_CLEAR == pSnapshot->ucCondition) ||
        (AGENTPET_WEATHER_CLOUDY == pSnapshot->ucCondition) ||
        (AGENTPET_WEATHER_RAIN == pSnapshot->ucCondition) ||
        (AGENTPET_WEATHER_SNOW == pSnapshot->ucCondition) ||
        (0U != (pSnapshot->ucFlags & AGENTPET_WEATHER_FLAG_MASK));
    if (!bConditionInteractive ||
        (l_bHasClaimedSequence &&
         (l_usClaimedSequence == pSnapshot->usSequence)))
    {
        return false;
    }
    ulElapsedTicks = ulNowMonotonicTicks - l_ulLastClaimMonotonicTicks;
    udCooldownTicks =
        (uint64_t)AGENTPET_WEATHER_INTERACTION_COOLDOWN_SECONDS *
        (uint64_t)ulTicksPerSecond;
    if (l_bHasLastClaimTime &&
        ((uint64_t)ulElapsedTicks < udCooldownTicks))
    {
        return false;
    }

    return true;
}

/*
 * AGENTPETWEATHER_ClaimInteraction
 * Function: atomically claim one local weather-care interaction.
 * Parameters:
 *   - usSequence: weather sequence currently shown by the GUI.
 *   - ulNowMonotonicTicks: current raw device tick.
 *   - ulTicksPerSecond: nonzero RT tick frequency.
 * Return: true once when the matching current sequence is claimable.
 */
bool AGENTPETWEATHER_ClaimInteraction(
    uint16_t usSequence,
    uint32_t ulNowMonotonicTicks,
    uint32_t ulTicksPerSecond)
{
    if (!l_bHasSnapshot ||
        (l_tSnapshot.usSequence != usSequence) ||
        !AGENTPETWEATHER_CanInteract(
            &l_tSnapshot,
            ulNowMonotonicTicks,
            ulTicksPerSecond))
    {
        return false;
    }

    l_usClaimedSequence = usSequence;
    l_ulLastClaimMonotonicTicks = ulNowMonotonicTicks;
    l_bHasClaimedSequence = true;
    l_bHasLastClaimTime = true;

    return true;
}
