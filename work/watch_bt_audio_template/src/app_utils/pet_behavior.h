#ifndef PET_BEHAVIOR_H
#define PET_BEHAVIOR_H

#include <stdbool.h>
#include <stdint.h>

#define PET_BEHAVIOR_PERSIST_VERSION (1U)
#define PET_BEHAVIOR_AFFINITY_DEFAULT (50U)
#define PET_BEHAVIOR_ENERGY_DEFAULT (70U)
#define PET_BEHAVIOR_AROUSAL_DEFAULT (20U)

typedef enum _PET_BEHAVIOR_MOOD
{
    PET_BEHAVIOR_MOOD_CALM = 0,
    PET_BEHAVIOR_MOOD_HAPPY,
    PET_BEHAVIOR_MOOD_CURIOUS,
    PET_BEHAVIOR_MOOD_SLEEPY,
    PET_BEHAVIOR_MOOD_LONELY,
    PET_BEHAVIOR_MOOD_COUNT
} PET_BEHAVIOR_MOOD;

typedef enum _PET_BEHAVIOR_REACTION
{
    PET_BEHAVIOR_REACTION_NONE = 0,
    PET_BEHAVIOR_REACTION_HAPPY,
    PET_BEHAVIOR_REACTION_CURIOUS,
    PET_BEHAVIOR_REACTION_CELEBRATE,
    PET_BEHAVIOR_REACTION_STARTLED,
    PET_BEHAVIOR_REACTION_ANNOYED,
    PET_BEHAVIOR_REACTION_COUNT
} PET_BEHAVIOR_REACTION;

typedef enum _PET_BEHAVIOR_VISUAL_STATE
{
    PET_BEHAVIOR_VISUAL_CALM = 0,
    PET_BEHAVIOR_VISUAL_HAPPY,
    PET_BEHAVIOR_VISUAL_CURIOUS,
    PET_BEHAVIOR_VISUAL_SLEEPY,
    PET_BEHAVIOR_VISUAL_LONELY,
    PET_BEHAVIOR_VISUAL_CELEBRATE,
    PET_BEHAVIOR_VISUAL_STARTLED,
    PET_BEHAVIOR_VISUAL_ANNOYED,
    PET_BEHAVIOR_VISUAL_WORKING,
    PET_BEHAVIOR_VISUAL_NEEDS_INPUT,
    PET_BEHAVIOR_VISUAL_ERROR,
    PET_BEHAVIOR_VISUAL_COUNT
} PET_BEHAVIOR_VISUAL_STATE;

typedef enum _PET_BEHAVIOR_EVENT_TYPE
{
    PET_BEHAVIOR_EVENT_TAP = 0,
    PET_BEHAVIOR_EVENT_COMFORT,
    PET_BEHAVIOR_EVENT_MOTION,
    PET_BEHAVIOR_EVENT_IMPACT,
    PET_BEHAVIOR_EVENT_AGENT_STATE,
    PET_BEHAVIOR_EVENT_COUNT
} PET_BEHAVIOR_EVENT_TYPE;

/* PET_BEHAVIOR_PERSISTED: versioned long-term values stored across page sessions.
 * Members are bounded to 0..100 except the schema, epoch, and PRNG state.
 */
typedef struct _PET_BEHAVIOR_PERSISTED
{
    uint32_t ulVersion;
    uint32_t ulLastInteractionEpoch;
    uint32_t ulRandomState;
    uint8_t ucAffinity;
    uint8_t ucEnergy;
    uint8_t ucArousal;
} PET_BEHAVIOR_PERSISTED;

/* PET_BEHAVIOR_EVENT: one timestamped input accepted by the behavior model.
 * ucValue is used only for Agent state values 0..4.
 */
typedef struct _PET_BEHAVIOR_EVENT
{
    PET_BEHAVIOR_EVENT_TYPE eType;
    uint32_t ulNowMs;
    uint32_t ulEpochSeconds;
    uint8_t ucValue;
} PET_BEHAVIOR_EVENT;

/* PET_BEHAVIOR_SNAPSHOT: immutable view data copied for UI and diagnostics. */
typedef struct _PET_BEHAVIOR_SNAPSHOT
{
    PET_BEHAVIOR_MOOD eMood;
    PET_BEHAVIOR_REACTION eReaction;
    PET_BEHAVIOR_VISUAL_STATE eVisualState;
    uint32_t ulGeneration;
    uint32_t ulLastInteractionEpoch;
    uint8_t ucAgentState;
    uint8_t ucAffinity;
    uint8_t ucEnergy;
    uint8_t ucArousal;
    bool bSaveRequired;
} PET_BEHAVIOR_SNAPSHOT;

/* PET_BEHAVIOR: fixed-memory state machine owned by the Pet page.
 * All millisecond deadlines use wrap-safe unsigned arithmetic. Long-term
 * attributes are bounded to 0..100 and never require heap allocation.
 */
typedef struct _PET_BEHAVIOR
{
    PET_BEHAVIOR_MOOD eMood;
    PET_BEHAVIOR_REACTION eReaction;
    uint32_t ulGeneration;
    uint32_t ulLastInteractionEpoch;
    uint32_t ulLastArousalUpdateEpoch;
    uint32_t ulLastEnergyUpdateEpoch;
    uint32_t ulRandomState;
    uint32_t ulMoodEnteredMs;
    uint32_t ulReactionUntilMs;
    uint32_t ulNextIdleDecisionMs;
    uint32_t ulTapWindowStartMs;
    uint32_t ulClickCooldownUntilMs;
    uint32_t ulComfortCooldownUntilMs;
    uint32_t ulMotionCooldownUntilMs;
    uint32_t ulLastAffinityRewardMs;
    uint8_t ucTapCount;
    uint8_t ucAgentState;
    uint8_t ucAffinity;
    uint8_t ucEnergy;
    uint8_t ucArousal;
    bool bSaveRequired;
} PET_BEHAVIOR;

bool PETBEHAVIOR_Init(PET_BEHAVIOR *pBehavior,
                      const PET_BEHAVIOR_PERSISTED *pPersisted,
                      uint32_t ulNowMs,
                      uint32_t ulEpochSeconds,
                      uint32_t ulRandomSeed);
bool PETBEHAVIOR_ProcessEvent(PET_BEHAVIOR *pBehavior,
                             const PET_BEHAVIOR_EVENT *pEvent);
bool PETBEHAVIOR_Update(PET_BEHAVIOR *pBehavior,
                        uint32_t ulNowMs,
                        uint32_t ulEpochSeconds);
bool PETBEHAVIOR_GetSnapshot(const PET_BEHAVIOR *pBehavior,
                             PET_BEHAVIOR_SNAPSHOT *pSnapshot);
bool PETBEHAVIOR_GetPersisted(const PET_BEHAVIOR *pBehavior,
                              PET_BEHAVIOR_PERSISTED *pPersisted);
void PETBEHAVIOR_MarkSaved(PET_BEHAVIOR *pBehavior);
const char *PETBEHAVIOR_VisualStateName(PET_BEHAVIOR_VISUAL_STATE eState);

#endif /* PET_BEHAVIOR_H */
