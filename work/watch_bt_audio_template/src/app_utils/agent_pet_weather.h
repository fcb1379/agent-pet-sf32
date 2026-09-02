#ifndef AGENT_PET_WEATHER_H
#define AGENT_PET_WEATHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AGENTPET_WEATHER_PAYLOAD_SIZE       (10U)
#define AGENTPET_WEATHER_FLAG_HOT           (0x01U)
#define AGENTPET_WEATHER_FLAG_COLD          (0x02U)
#define AGENTPET_WEATHER_FLAG_MASK          (0x03U)
#define AGENTPET_WEATHER_TTL_MINUTES_MIN    (15U)
#define AGENTPET_WEATHER_TTL_MINUTES_MAX    (360U)
#define AGENTPET_WEATHER_MONOTONIC_MAX_MINUTES (60U)
#define AGENTPET_WEATHER_INTERACTION_COOLDOWN_SECONDS (10800UL)

typedef enum _AGENTPET_WEATHER_CONDITION
{
    AGENTPET_WEATHER_UNKNOWN = 0,
    AGENTPET_WEATHER_CLEAR = 1,
    AGENTPET_WEATHER_CLOUDY = 2,
    AGENTPET_WEATHER_RAIN = 3,
    AGENTPET_WEATHER_SNOW = 4,
    AGENTPET_WEATHER_STORM = 5
} AGENTPET_WEATHER_CONDITION;

typedef enum _AGENTPET_WEATHER_RESULT
{
    AGENTPET_WEATHER_RESULT_PUBLISHED = 0,
    AGENTPET_WEATHER_RESULT_DUPLICATE = 1,
    AGENTPET_WEATHER_RESULT_STALE = 2,
    AGENTPET_WEATHER_ERROR_INVALID_PARAMETER = 100,
    AGENTPET_WEATHER_ERROR_LENGTH = 101,
    AGENTPET_WEATHER_ERROR_CONDITION = 102,
    AGENTPET_WEATHER_ERROR_TEMPERATURE = 103,
    AGENTPET_WEATHER_ERROR_TIME = 104,
    AGENTPET_WEATHER_ERROR_TTL = 105,
    AGENTPET_WEATHER_ERROR_FLAGS = 106
} AGENTPET_WEATHER_RESULT;

typedef enum _AGENTPET_WEATHER_FRESHNESS
{
    AGENTPET_WEATHER_FRESHNESS_EMPTY = 0,
    AGENTPET_WEATHER_FRESHNESS_RTC = 1,
    AGENTPET_WEATHER_FRESHNESS_MONOTONIC = 2,
    AGENTPET_WEATHER_FRESHNESS_EXPIRED = 3
} AGENTPET_WEATHER_FRESHNESS;

typedef enum _AGENTPET_WEATHER_PRESENTATION
{
    AGENTPET_WEATHER_PRESENTATION_HIDDEN = 0,
    AGENTPET_WEATHER_PRESENTATION_AMBIENCE = 1,
    AGENTPET_WEATHER_PRESENTATION_INTERACTION = 2
} AGENTPET_WEATHER_PRESENTATION;

/* AGENTPET_WEATHER_SNAPSHOT: fixed weather context published from BLE.
 * Members:
 *   - ulObservedUtc: observation UTC Unix timestamp, 2020..2037
 *   - ulReceivedMonotonicTicks: raw RT tick at acceptance
 *   - ulGeneration: publication generation for GUI change detection
 *   - usSequence: 16-bit serial number with half-range ordering
 *   - sTemperatureDeciC: temperature in 0.1 degrees Celsius, -500..600
 *   - usTtlMinutes: provider-independent validity, 15..360 minutes
 *   - ucCondition: AGENTPET_WEATHER_CONDITION value
 *   - ucFlags: HOT/COLD flags; both cannot be set together
 */
typedef struct _AGENTPET_WEATHER_SNAPSHOT
{
    uint32_t ulObservedUtc;
    uint32_t ulReceivedMonotonicTicks;
    uint32_t ulGeneration;
    uint16_t usSequence;
    int16_t sTemperatureDeciC;
    uint16_t usTtlMinutes;
    uint8_t ucCondition;
    uint8_t ucFlags;
} AGENTPET_WEATHER_SNAPSHOT;

/* AGENTPET_WEATHER_DIAGNOSTICS: saturating protocol counters.
 * Members record published, duplicate, stale and invalid payload outcomes.
 */
typedef struct _AGENTPET_WEATHER_DIAGNOSTICS
{
    uint32_t ulPublishedCount;
    uint32_t ulDuplicateCount;
    uint32_t ulStaleCount;
    uint32_t ulRejectedCount;
} AGENTPET_WEATHER_DIAGNOSTICS;

/* AGENTPET_WEATHER_PRIORITY_INPUT: pure display-priority inputs.
 * Each flag describes a higher-priority presentation or local eligibility.
 */
typedef struct _AGENTPET_WEATHER_PRIORITY_INPUT
{
    bool bFeatureEnabled;
    bool bSnapshotFresh;
    bool bImageTransferActive;
    bool bAgentBlocksWeather;
    bool bTypingActive;
    bool bRemoteExpressionActive;
    bool bQuestFeedbackPending;
    bool bInteractionAvailable;
} AGENTPET_WEATHER_PRIORITY_INPUT;

void AGENTPETWEATHER_Init(void);
AGENTPET_WEATHER_RESULT AGENTPETWEATHER_ProcessPayload(
    uint16_t usSequence,
    const uint8_t *pPayload,
    size_t ulLength,
    uint32_t ulReceivedMonotonicTicks);
bool AGENTPETWEATHER_GetSnapshot(
    AGENTPET_WEATHER_SNAPSHOT *pSnapshot,
    AGENTPET_WEATHER_DIAGNOSTICS *pDiagnostics);
bool AGENTPETWEATHER_IsSequenceNewer(
    uint16_t usCandidate,
    uint16_t usReference);
AGENTPET_WEATHER_FRESHNESS AGENTPETWEATHER_EvaluateFreshness(
    const AGENTPET_WEATHER_SNAPSHOT *pSnapshot,
    uint32_t ulNowUtc,
    uint32_t ulNowMonotonicTicks,
    uint32_t ulTicksPerSecond,
    bool bRtcValid);
AGENTPET_WEATHER_PRESENTATION AGENTPETWEATHER_SelectPresentation(
    const AGENTPET_WEATHER_PRIORITY_INPUT *pInput);
bool AGENTPETWEATHER_CanInteract(
    const AGENTPET_WEATHER_SNAPSHOT *pSnapshot,
    uint32_t ulNowMonotonicTicks,
    uint32_t ulTicksPerSecond);
bool AGENTPETWEATHER_ClaimInteraction(
    uint16_t usSequence,
    uint32_t ulNowMonotonicTicks,
    uint32_t ulTicksPerSecond);

#endif /* AGENT_PET_WEATHER_H */
