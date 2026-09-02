#include "pet_behavior.h"

#include <stddef.h>
#include <string.h>

#define PET_BEHAVIOR_MAX_VALUE (100U)
#define PET_BEHAVIOR_AGENT_IDLE (0U)
#define PET_BEHAVIOR_AGENT_RUNNING (1U)
#define PET_BEHAVIOR_AGENT_NEEDS_INPUT (2U)
#define PET_BEHAVIOR_AGENT_COMPLETED (3U)
#define PET_BEHAVIOR_AGENT_ERROR (4U)
#define PET_BEHAVIOR_TAP_WINDOW_MS (1500U)
#define PET_BEHAVIOR_CLICK_COOLDOWN_MS (10000U)
#define PET_BEHAVIOR_COMFORT_COOLDOWN_MS (20000U)
#define PET_BEHAVIOR_MOTION_COOLDOWN_MS (3000U)
#define PET_BEHAVIOR_AFFINITY_REWARD_MS (1000U)
#define PET_BEHAVIOR_MIN_MOOD_MS (8000U)
#define PET_BEHAVIOR_IDLE_MIN_MS (20000U)
#define PET_BEHAVIOR_IDLE_SPAN_MS (25001U)
#define PET_BEHAVIOR_HAPPY_MS (3000U)
#define PET_BEHAVIOR_COMFORT_MS (5000U)
#define PET_BEHAVIOR_CURIOUS_MS (4000U)
#define PET_BEHAVIOR_CELEBRATE_MS (4000U)
#define PET_BEHAVIOR_STARTLED_MS (3000U)
#define PET_BEHAVIOR_ANNOYED_MS (5000U)
#define PET_BEHAVIOR_SLEEP_AFTER_SECONDS (3600U)
#define PET_BEHAVIOR_LONELY_AFTER_SECONDS (86400U)
#define PET_BEHAVIOR_AROUSAL_DECAY_SECONDS (60U)
#define PET_BEHAVIOR_ENERGY_DECAY_SECONDS (1800U)
#define PET_BEHAVIOR_NIGHT_START_HOUR (22U)
#define PET_BEHAVIOR_NIGHT_END_HOUR (7U)

/* Add an increment to a 0..100 attribute without integer overflow. */
static uint8_t Local_AddBounded(uint8_t ucValue, uint32_t ulIncrement)
{
    uint32_t ulValue;

    ulValue = (uint32_t)ucValue + ulIncrement;
    if (PET_BEHAVIOR_MAX_VALUE < ulValue)
    {
        ulValue = PET_BEHAVIOR_MAX_VALUE;
    }

    return (uint8_t)ulValue;
}

/* Subtract a decrement from a bounded attribute without unsigned underflow. */
static uint8_t Local_SubtractBounded(uint8_t ucValue, uint32_t ulDecrement)
{
    if ((uint32_t)ucValue <= ulDecrement)
    {
        return 0U;
    }

    return (uint8_t)((uint32_t)ucValue - ulDecrement);
}

/* Compare a 32-bit tick deadline while tolerating one counter wrap. */
static bool Local_TimeReached(uint32_t ulNowMs, uint32_t ulDeadlineMs)
{
    return ((uint32_t)(ulNowMs - ulDeadlineMs) < 0x80000000UL);
}

/* Measure a 32-bit tick interval while tolerating one counter wrap. */
static bool Local_ElapsedAtLeast(uint32_t ulNowMs,
                                 uint32_t ulStartMs,
                                 uint32_t ulDurationMs)
{
    return (ulDurationMs <= (uint32_t)(ulNowMs - ulStartMs));
}

/* Advance the deterministic xorshift PRNG stored inside the model. */
static uint32_t Local_Random(PET_BEHAVIOR *pBehavior)
{
    uint32_t ulValue;

    ulValue = pBehavior->ulRandomState;
    if (0U == ulValue)
    {
        ulValue = 0x6D2B79F5UL;
    }
    ulValue ^= ulValue << 13U;
    ulValue ^= ulValue >> 17U;
    ulValue ^= ulValue << 5U;
    pBehavior->ulRandomState = ulValue;

    return ulValue;
}

/* Start or refresh one bounded short-term reaction. */
static void Local_SetReaction(PET_BEHAVIOR *pBehavior,
                              PET_BEHAVIOR_REACTION eReaction,
                              uint32_t ulNowMs,
                              uint32_t ulDurationMs)
{
    if ((NULL == pBehavior) || (PET_BEHAVIOR_REACTION_COUNT <= eReaction))
    {
        return;
    }

    if ((pBehavior->eReaction != eReaction) ||
        (pBehavior->ulReactionUntilMs != (ulNowMs + ulDurationMs)))
    {
        pBehavior->eReaction = eReaction;
        pBehavior->ulReactionUntilMs = ulNowMs + ulDurationMs;
        pBehavior->ulGeneration++;
    }

    return;
}

/* Change the base mood and record its minimum-dwell start tick. */
static void Local_SetMood(PET_BEHAVIOR *pBehavior,
                          PET_BEHAVIOR_MOOD eMood,
                          uint32_t ulNowMs)
{
    if ((NULL == pBehavior) || (PET_BEHAVIOR_MOOD_COUNT <= eMood) ||
        (pBehavior->eMood == eMood))
    {
        return;
    }

    pBehavior->eMood = eMood;
    pBehavior->ulMoodEnteredMs = ulNowMs;
    pBehavior->ulGeneration++;
    return;
}

/* Record a valid RTC interaction time and mark persistent data dirty. */
static void Local_RecordInteraction(PET_BEHAVIOR *pBehavior,
                                    uint32_t ulEpochSeconds)
{
    if ((NULL != pBehavior) && (0U != ulEpochSeconds) &&
        (pBehavior->ulLastInteractionEpoch != ulEpochSeconds))
    {
        pBehavior->ulLastInteractionEpoch = ulEpochSeconds;
        pBehavior->bSaveRequired = true;
    }

    return;
}

/* Return whether the device-local RTC hour falls in the night interval. */
static bool Local_IsNight(uint32_t ulEpochSeconds)
{
    uint32_t ulHour;

    if (86400U > ulEpochSeconds)
    {
        return false;
    }
    ulHour = (ulEpochSeconds % 86400U) / 3600U;

    return ((PET_BEHAVIOR_NIGHT_START_HOUR <= ulHour) ||
            (PET_BEHAVIOR_NIGHT_END_HOUR > ulHour));
}

/* Resolve Agent override, reaction, and base mood into one visual state. */
static PET_BEHAVIOR_VISUAL_STATE Local_EffectiveState(
    const PET_BEHAVIOR *pBehavior)
{
    if (NULL == pBehavior)
    {
        return PET_BEHAVIOR_VISUAL_CALM;
    }

    switch (pBehavior->ucAgentState)
    {
    case PET_BEHAVIOR_AGENT_RUNNING:
        return PET_BEHAVIOR_VISUAL_WORKING;
    case PET_BEHAVIOR_AGENT_NEEDS_INPUT:
        return PET_BEHAVIOR_VISUAL_NEEDS_INPUT;
    case PET_BEHAVIOR_AGENT_COMPLETED:
        return PET_BEHAVIOR_VISUAL_CELEBRATE;
    case PET_BEHAVIOR_AGENT_ERROR:
        return PET_BEHAVIOR_VISUAL_ERROR;
    default:
        break;
    }

    switch (pBehavior->eReaction)
    {
    case PET_BEHAVIOR_REACTION_HAPPY:
        return PET_BEHAVIOR_VISUAL_HAPPY;
    case PET_BEHAVIOR_REACTION_CURIOUS:
        return PET_BEHAVIOR_VISUAL_CURIOUS;
    case PET_BEHAVIOR_REACTION_CELEBRATE:
        return PET_BEHAVIOR_VISUAL_CELEBRATE;
    case PET_BEHAVIOR_REACTION_STARTLED:
        return PET_BEHAVIOR_VISUAL_STARTLED;
    case PET_BEHAVIOR_REACTION_ANNOYED:
        return PET_BEHAVIOR_VISUAL_ANNOYED;
    default:
        break;
    }

    switch (pBehavior->eMood)
    {
    case PET_BEHAVIOR_MOOD_HAPPY:
        return PET_BEHAVIOR_VISUAL_HAPPY;
    case PET_BEHAVIOR_MOOD_CURIOUS:
        return PET_BEHAVIOR_VISUAL_CURIOUS;
    case PET_BEHAVIOR_MOOD_SLEEPY:
        return PET_BEHAVIOR_VISUAL_SLEEPY;
    case PET_BEHAVIOR_MOOD_LONELY:
        return PET_BEHAVIOR_VISUAL_LONELY;
    default:
        return PET_BEHAVIOR_VISUAL_CALM;
    }
}

/* Initialize a behavior model from validated persistence or safe defaults. */
bool PETBEHAVIOR_Init(PET_BEHAVIOR *pBehavior,
                      const PET_BEHAVIOR_PERSISTED *pPersisted,
                      uint32_t ulNowMs,
                      uint32_t ulEpochSeconds,
                      uint32_t ulRandomSeed)
{
    if (NULL == pBehavior)
    {
        return false;
    }

    (void)memset(pBehavior, 0, sizeof(*pBehavior));
    pBehavior->eMood = PET_BEHAVIOR_MOOD_CALM;
    pBehavior->ucAffinity = PET_BEHAVIOR_AFFINITY_DEFAULT;
    pBehavior->ucEnergy = PET_BEHAVIOR_ENERGY_DEFAULT;
    pBehavior->ucArousal = PET_BEHAVIOR_AROUSAL_DEFAULT;
    pBehavior->ulRandomState = (0U != ulRandomSeed) ?
        ulRandomSeed : 0x6D2B79F5UL;
    if ((NULL != pPersisted) &&
        (PET_BEHAVIOR_PERSIST_VERSION == pPersisted->ulVersion) &&
        (PET_BEHAVIOR_MAX_VALUE >= pPersisted->ucAffinity) &&
        (PET_BEHAVIOR_MAX_VALUE >= pPersisted->ucEnergy) &&
        (PET_BEHAVIOR_MAX_VALUE >= pPersisted->ucArousal))
    {
        pBehavior->ulLastInteractionEpoch =
            pPersisted->ulLastInteractionEpoch;
        pBehavior->ucAffinity = pPersisted->ucAffinity;
        pBehavior->ucEnergy = pPersisted->ucEnergy;
        pBehavior->ucArousal = pPersisted->ucArousal;
        if (0U != pPersisted->ulRandomState)
        {
            pBehavior->ulRandomState = pPersisted->ulRandomState;
        }
    }
    else
    {
        pBehavior->bSaveRequired = true;
    }
    if ((0U == pBehavior->ulLastInteractionEpoch) && (0U != ulEpochSeconds))
    {
        pBehavior->ulLastInteractionEpoch = ulEpochSeconds;
        pBehavior->bSaveRequired = true;
    }
    pBehavior->ulLastArousalUpdateEpoch = ulEpochSeconds;
    pBehavior->ulLastEnergyUpdateEpoch = ulEpochSeconds;
    pBehavior->ulMoodEnteredMs = ulNowMs;
    pBehavior->ulClickCooldownUntilMs = ulNowMs;
    pBehavior->ulComfortCooldownUntilMs = ulNowMs;
    pBehavior->ulMotionCooldownUntilMs = ulNowMs;
    pBehavior->ulLastAffinityRewardMs =
        ulNowMs - PET_BEHAVIOR_AFFINITY_REWARD_MS;
    pBehavior->ulNextIdleDecisionMs = ulNowMs + PET_BEHAVIOR_IDLE_MIN_MS +
        (Local_Random(pBehavior) % PET_BEHAVIOR_IDLE_SPAN_MS);
    pBehavior->ulGeneration = 1U;
    (void)PETBEHAVIOR_Update(pBehavior, ulNowMs, ulEpochSeconds);

    return true;
}

/* Validate and apply one touch, motion, impact, or Agent event. */
bool PETBEHAVIOR_ProcessEvent(PET_BEHAVIOR *pBehavior,
                             const PET_BEHAVIOR_EVENT *pEvent)
{
    uint8_t ucPreviousAgentState;

    if ((NULL == pBehavior) || (NULL == pEvent) ||
        (PET_BEHAVIOR_EVENT_COUNT <= pEvent->eType))
    {
        return false;
    }

    switch (pEvent->eType)
    {
    case PET_BEHAVIOR_EVENT_TAP:
        Local_RecordInteraction(pBehavior, pEvent->ulEpochSeconds);
        if (!Local_ElapsedAtLeast(pEvent->ulNowMs,
                                  pBehavior->ulTapWindowStartMs,
                                  PET_BEHAVIOR_TAP_WINDOW_MS))
        {
            if (UINT8_MAX > pBehavior->ucTapCount)
            {
                pBehavior->ucTapCount++;
            }
        }
        else
        {
            pBehavior->ulTapWindowStartMs = pEvent->ulNowMs;
            pBehavior->ucTapCount = 1U;
        }
        if (6U <= pBehavior->ucTapCount)
        {
            Local_SetReaction(pBehavior, PET_BEHAVIOR_REACTION_ANNOYED,
                              pEvent->ulNowMs, PET_BEHAVIOR_ANNOYED_MS);
            pBehavior->ulClickCooldownUntilMs = pEvent->ulNowMs +
                PET_BEHAVIOR_CLICK_COOLDOWN_MS;
            pBehavior->ucTapCount = 0U;
            pBehavior->ulTapWindowStartMs = pEvent->ulNowMs;
        }
        else if (3U == pBehavior->ucTapCount)
        {
            Local_SetReaction(pBehavior, PET_BEHAVIOR_REACTION_CELEBRATE,
                              pEvent->ulNowMs, PET_BEHAVIOR_CELEBRATE_MS);
        }
        else if ((1U == pBehavior->ucTapCount) &&
                 Local_TimeReached(pEvent->ulNowMs,
                                   pBehavior->ulClickCooldownUntilMs))
        {
            Local_SetReaction(pBehavior, PET_BEHAVIOR_REACTION_HAPPY,
                              pEvent->ulNowMs, PET_BEHAVIOR_HAPPY_MS);
            if (Local_ElapsedAtLeast(pEvent->ulNowMs,
                                     pBehavior->ulLastAffinityRewardMs,
                                     PET_BEHAVIOR_AFFINITY_REWARD_MS))
            {
                pBehavior->ucAffinity = Local_AddBounded(
                    pBehavior->ucAffinity, 1U);
                pBehavior->ulLastAffinityRewardMs = pEvent->ulNowMs;
                pBehavior->bSaveRequired = true;
            }
        }
        pBehavior->ucArousal = Local_AddBounded(pBehavior->ucArousal, 4U);
        break;

    case PET_BEHAVIOR_EVENT_COMFORT:
        Local_RecordInteraction(pBehavior, pEvent->ulEpochSeconds);
        if (Local_TimeReached(pEvent->ulNowMs,
                              pBehavior->ulComfortCooldownUntilMs))
        {
            Local_SetReaction(pBehavior, PET_BEHAVIOR_REACTION_HAPPY,
                              pEvent->ulNowMs, PET_BEHAVIOR_COMFORT_MS);
            pBehavior->ucAffinity = Local_AddBounded(
                pBehavior->ucAffinity, 2U);
            pBehavior->ulComfortCooldownUntilMs = pEvent->ulNowMs +
                PET_BEHAVIOR_COMFORT_COOLDOWN_MS;
            pBehavior->bSaveRequired = true;
        }
        break;

    case PET_BEHAVIOR_EVENT_MOTION:
        if (Local_TimeReached(pEvent->ulNowMs,
                              pBehavior->ulMotionCooldownUntilMs))
        {
            Local_SetReaction(pBehavior, PET_BEHAVIOR_REACTION_CURIOUS,
                              pEvent->ulNowMs, PET_BEHAVIOR_CURIOUS_MS);
            pBehavior->ucArousal = Local_AddBounded(
                pBehavior->ucArousal, 8U);
            pBehavior->ulMotionCooldownUntilMs = pEvent->ulNowMs +
                PET_BEHAVIOR_MOTION_COOLDOWN_MS;
        }
        break;

    case PET_BEHAVIOR_EVENT_IMPACT:
        if (Local_TimeReached(pEvent->ulNowMs,
                              pBehavior->ulMotionCooldownUntilMs))
        {
            Local_SetReaction(pBehavior, PET_BEHAVIOR_REACTION_STARTLED,
                              pEvent->ulNowMs, PET_BEHAVIOR_STARTLED_MS);
            pBehavior->ucArousal = Local_AddBounded(
                pBehavior->ucArousal, 15U);
            pBehavior->ulMotionCooldownUntilMs = pEvent->ulNowMs +
                PET_BEHAVIOR_MOTION_COOLDOWN_MS;
        }
        break;

    case PET_BEHAVIOR_EVENT_AGENT_STATE:
        if (PET_BEHAVIOR_AGENT_ERROR < pEvent->ucValue)
        {
            return false;
        }
        ucPreviousAgentState = pBehavior->ucAgentState;
        pBehavior->ucAgentState = pEvent->ucValue;
        if (ucPreviousAgentState != pBehavior->ucAgentState)
        {
            pBehavior->ulGeneration++;
            if ((PET_BEHAVIOR_AGENT_COMPLETED == pBehavior->ucAgentState) &&
                (PET_BEHAVIOR_AGENT_COMPLETED != ucPreviousAgentState))
            {
                Local_SetReaction(pBehavior,
                                  PET_BEHAVIOR_REACTION_CELEBRATE,
                                  pEvent->ulNowMs,
                                  PET_BEHAVIOR_CELEBRATE_MS);
            }
        }
        break;

    default:
        return false;
    }

    return true;
}

/* Advance reaction expiry, long-term decay, and autonomous base mood rules. */
bool PETBEHAVIOR_Update(PET_BEHAVIOR *pBehavior,
                        uint32_t ulNowMs,
                        uint32_t ulEpochSeconds)
{
    uint32_t ulElapsedSeconds;
    uint32_t ulIdleSeconds;
    uint32_t ulStepCount;
    PET_BEHAVIOR_MOOD eCandidate;

    if (NULL == pBehavior)
    {
        return false;
    }

    if ((PET_BEHAVIOR_REACTION_NONE != pBehavior->eReaction) &&
        Local_TimeReached(ulNowMs, pBehavior->ulReactionUntilMs))
    {
        pBehavior->eReaction = PET_BEHAVIOR_REACTION_NONE;
        pBehavior->ulGeneration++;
    }

    if ((0U != ulEpochSeconds) &&
        (0U == pBehavior->ulLastInteractionEpoch))
    {
        pBehavior->ulLastInteractionEpoch = ulEpochSeconds;
        pBehavior->bSaveRequired = true;
    }
    if ((0U != ulEpochSeconds) &&
        (0U == pBehavior->ulLastArousalUpdateEpoch))
    {
        pBehavior->ulLastArousalUpdateEpoch = ulEpochSeconds;
    }
    if ((0U != ulEpochSeconds) &&
        (0U == pBehavior->ulLastEnergyUpdateEpoch))
    {
        pBehavior->ulLastEnergyUpdateEpoch = ulEpochSeconds;
    }

    if ((0U != ulEpochSeconds) &&
        (0U != pBehavior->ulLastArousalUpdateEpoch) &&
        (ulEpochSeconds > pBehavior->ulLastArousalUpdateEpoch))
    {
        ulElapsedSeconds = ulEpochSeconds -
            pBehavior->ulLastArousalUpdateEpoch;
        ulStepCount = ulElapsedSeconds /
            PET_BEHAVIOR_AROUSAL_DECAY_SECONDS;
        if (0U != ulStepCount)
        {
            pBehavior->ucArousal = Local_SubtractBounded(
                pBehavior->ucArousal, ulStepCount);
            pBehavior->ulLastArousalUpdateEpoch +=
                ulStepCount * PET_BEHAVIOR_AROUSAL_DECAY_SECONDS;
            pBehavior->bSaveRequired = true;
        }
    }
    else if ((0U != ulEpochSeconds) &&
             (ulEpochSeconds < pBehavior->ulLastArousalUpdateEpoch))
    {
        pBehavior->ulLastArousalUpdateEpoch = ulEpochSeconds;
    }

    if ((0U != ulEpochSeconds) &&
        (0U != pBehavior->ulLastEnergyUpdateEpoch) &&
        (ulEpochSeconds > pBehavior->ulLastEnergyUpdateEpoch))
    {
        ulElapsedSeconds = ulEpochSeconds -
            pBehavior->ulLastEnergyUpdateEpoch;
        ulStepCount = ulElapsedSeconds / PET_BEHAVIOR_ENERGY_DECAY_SECONDS;
        if (0U != ulStepCount)
        {
            if (Local_IsNight(ulEpochSeconds))
            {
                pBehavior->ucEnergy = Local_AddBounded(
                    pBehavior->ucEnergy, ulStepCount);
            }
            else
            {
                pBehavior->ucEnergy = Local_SubtractBounded(
                    pBehavior->ucEnergy, ulStepCount);
            }
            pBehavior->ulLastEnergyUpdateEpoch +=
                ulStepCount * PET_BEHAVIOR_ENERGY_DECAY_SECONDS;
            pBehavior->bSaveRequired = true;
        }
    }
    else if ((0U != ulEpochSeconds) &&
             (ulEpochSeconds < pBehavior->ulLastEnergyUpdateEpoch))
    {
        pBehavior->ulLastEnergyUpdateEpoch = ulEpochSeconds;
    }

    eCandidate = pBehavior->eMood;
    ulIdleSeconds = 0U;
    if ((0U != ulEpochSeconds) &&
        (0U != pBehavior->ulLastInteractionEpoch) &&
        (ulEpochSeconds >= pBehavior->ulLastInteractionEpoch))
    {
        ulIdleSeconds = ulEpochSeconds - pBehavior->ulLastInteractionEpoch;
    }
    if (PET_BEHAVIOR_LONELY_AFTER_SECONDS <= ulIdleSeconds)
    {
        eCandidate = PET_BEHAVIOR_MOOD_LONELY;
    }
    else if (Local_IsNight(ulEpochSeconds) ||
             (PET_BEHAVIOR_SLEEP_AFTER_SECONDS <= ulIdleSeconds) ||
             (20U >= pBehavior->ucEnergy))
    {
        eCandidate = PET_BEHAVIOR_MOOD_SLEEPY;
    }
    else if (60U <= pBehavior->ucArousal)
    {
        eCandidate = PET_BEHAVIOR_MOOD_CURIOUS;
    }
    else if ((300U >= ulIdleSeconds) && (60U <= pBehavior->ucAffinity))
    {
        eCandidate = PET_BEHAVIOR_MOOD_HAPPY;
    }
    else if ((PET_BEHAVIOR_MOOD_HAPPY == pBehavior->eMood) ||
             (PET_BEHAVIOR_MOOD_SLEEPY == pBehavior->eMood) ||
             (PET_BEHAVIOR_MOOD_LONELY == pBehavior->eMood))
    {
        eCandidate = PET_BEHAVIOR_MOOD_CALM;
    }
    else if (Local_TimeReached(ulNowMs, pBehavior->ulNextIdleDecisionMs))
    {
        eCandidate = ((Local_Random(pBehavior) % 100U) < 30U) ?
            PET_BEHAVIOR_MOOD_CURIOUS : PET_BEHAVIOR_MOOD_CALM;
        pBehavior->ulNextIdleDecisionMs = ulNowMs +
            PET_BEHAVIOR_IDLE_MIN_MS +
            (Local_Random(pBehavior) % PET_BEHAVIOR_IDLE_SPAN_MS);
    }
    if ((eCandidate != pBehavior->eMood) &&
        (Local_ElapsedAtLeast(ulNowMs,
                              pBehavior->ulMoodEnteredMs,
                              PET_BEHAVIOR_MIN_MOOD_MS) ||
         (PET_BEHAVIOR_MOOD_LONELY == eCandidate) ||
         (PET_BEHAVIOR_MOOD_SLEEPY == eCandidate)))
    {
        Local_SetMood(pBehavior, eCandidate, ulNowMs);
    }

    return true;
}

/* Copy a consistent UI-facing snapshot from the model. */
bool PETBEHAVIOR_GetSnapshot(const PET_BEHAVIOR *pBehavior,
                             PET_BEHAVIOR_SNAPSHOT *pSnapshot)
{
    if ((NULL == pBehavior) || (NULL == pSnapshot))
    {
        return false;
    }

    pSnapshot->eMood = pBehavior->eMood;
    pSnapshot->eReaction = pBehavior->eReaction;
    pSnapshot->eVisualState = Local_EffectiveState(pBehavior);
    pSnapshot->ulGeneration = pBehavior->ulGeneration;
    pSnapshot->ulLastInteractionEpoch =
        pBehavior->ulLastInteractionEpoch;
    pSnapshot->ucAgentState = pBehavior->ucAgentState;
    pSnapshot->ucAffinity = pBehavior->ucAffinity;
    pSnapshot->ucEnergy = pBehavior->ucEnergy;
    pSnapshot->ucArousal = pBehavior->ucArousal;
    pSnapshot->bSaveRequired = pBehavior->bSaveRequired;

    return true;
}

/* Copy the versioned subset that is safe to persist. */
bool PETBEHAVIOR_GetPersisted(const PET_BEHAVIOR *pBehavior,
                              PET_BEHAVIOR_PERSISTED *pPersisted)
{
    if ((NULL == pBehavior) || (NULL == pPersisted))
    {
        return false;
    }

    pPersisted->ulVersion = PET_BEHAVIOR_PERSIST_VERSION;
    pPersisted->ulLastInteractionEpoch =
        pBehavior->ulLastInteractionEpoch;
    pPersisted->ulRandomState = pBehavior->ulRandomState;
    pPersisted->ucAffinity = pBehavior->ucAffinity;
    pPersisted->ucEnergy = pBehavior->ucEnergy;
    pPersisted->ucArousal = pBehavior->ucArousal;

    return true;
}

/* Clear the dirty flag after a matching persistence transaction succeeds. */
void PETBEHAVIOR_MarkSaved(PET_BEHAVIOR *pBehavior)
{
    if (NULL != pBehavior)
    {
        pBehavior->bSaveRequired = false;
    }

    return;
}

/* Return the stable English label used by the compact Pet page header. */
const char *PETBEHAVIOR_VisualStateName(PET_BEHAVIOR_VISUAL_STATE eState)
{
    static const char *l_aNames[PET_BEHAVIOR_VISUAL_COUNT] =
    {
        "Calm", "Happy", "Curious", "Sleepy", "Lonely", "Celebrate",
        "Startled", "Annoyed", "Working", "Needs input", "Error"
    };

    if (PET_BEHAVIOR_VISUAL_COUNT <= eState)
    {
        return l_aNames[PET_BEHAVIOR_VISUAL_CALM];
    }

    return l_aNames[eState];
}
