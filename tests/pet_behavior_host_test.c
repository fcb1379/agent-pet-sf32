#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pet_behavior.h"
#include "pet_state_assets.h"

static PET_BEHAVIOR_EVENT TEST_Event(PET_BEHAVIOR_EVENT_TYPE eType,
                                     uint32_t ulNowMs,
                                     uint32_t ulEpochSeconds,
                                     uint8_t ucValue)
{
    PET_BEHAVIOR_EVENT tEvent;

    tEvent.eType = eType;
    tEvent.ulNowMs = ulNowMs;
    tEvent.ulEpochSeconds = ulEpochSeconds;
    tEvent.ucValue = ucValue;
    return tEvent;
}

static PET_BEHAVIOR_SNAPSHOT TEST_Snapshot(const PET_BEHAVIOR *pBehavior)
{
    PET_BEHAVIOR_SNAPSHOT tSnapshot;

    (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
    assert(PETBEHAVIOR_GetSnapshot(pBehavior, &tSnapshot));
    return tSnapshot;
}

static void TEST_TapReactions(void)
{
    PET_BEHAVIOR tBehavior;
    PET_BEHAVIOR_EVENT tEvent;
    PET_BEHAVIOR_SNAPSHOT tSnapshot;
    uint8_t ucIndex;

    assert(PETBEHAVIOR_Init(&tBehavior, NULL, 10000U, 200000U, 1U));
    tEvent = TEST_Event(PET_BEHAVIOR_EVENT_TAP, 11000U, 200001U, 0U);
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    tSnapshot = TEST_Snapshot(&tBehavior);
    assert(PET_BEHAVIOR_VISUAL_HAPPY == tSnapshot.eVisualState);
    assert(PET_BEHAVIOR_AFFINITY_DEFAULT + 1U == tSnapshot.ucAffinity);

    for (ucIndex = 0U; ucIndex < 2U; ucIndex++)
    {
        tEvent.ulNowMs += 300U;
        assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    }
    assert(PET_BEHAVIOR_VISUAL_CELEBRATE ==
           TEST_Snapshot(&tBehavior).eVisualState);

    for (ucIndex = 0U; ucIndex < 3U; ucIndex++)
    {
        tEvent.ulNowMs += 200U;
        assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
        if (2U > ucIndex)
        {
            assert(PET_BEHAVIOR_VISUAL_CELEBRATE ==
                   TEST_Snapshot(&tBehavior).eVisualState);
        }
    }
    assert(PET_BEHAVIOR_VISUAL_ANNOYED ==
           TEST_Snapshot(&tBehavior).eVisualState);
}

static void TEST_Cooldowns(void)
{
    PET_BEHAVIOR tBehavior;
    PET_BEHAVIOR_EVENT tEvent;
    PET_BEHAVIOR_SNAPSHOT tSnapshot;

    assert(PETBEHAVIOR_Init(&tBehavior, NULL, 1000U, 300000U, 2U));
    tEvent = TEST_Event(PET_BEHAVIOR_EVENT_COMFORT, 21000U, 300001U, 0U);
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    tSnapshot = TEST_Snapshot(&tBehavior);
    assert(PET_BEHAVIOR_AFFINITY_DEFAULT + 2U == tSnapshot.ucAffinity);
    tEvent.ulNowMs += 1000U;
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(tSnapshot.ucAffinity == TEST_Snapshot(&tBehavior).ucAffinity);

    tEvent = TEST_Event(PET_BEHAVIOR_EVENT_MOTION, 30000U, 300002U, 0U);
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PET_BEHAVIOR_VISUAL_CURIOUS ==
           TEST_Snapshot(&tBehavior).eVisualState);
    tEvent = TEST_Event(PET_BEHAVIOR_EVENT_IMPACT, 30500U, 300002U, 0U);
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PET_BEHAVIOR_VISUAL_CURIOUS ==
           TEST_Snapshot(&tBehavior).eVisualState);
    tEvent.ulNowMs = 33100U;
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PET_BEHAVIOR_VISUAL_STARTLED ==
           TEST_Snapshot(&tBehavior).eVisualState);
}

static void TEST_TimeAndAgentPriority(void)
{
    PET_BEHAVIOR tBehavior;
    PET_BEHAVIOR_PERSISTED tPersisted;
    PET_BEHAVIOR_EVENT tEvent;

    (void)memset(&tPersisted, 0, sizeof(tPersisted));
    tPersisted.ulVersion = PET_BEHAVIOR_PERSIST_VERSION;
    tPersisted.ulLastInteractionEpoch = 100000U;
    tPersisted.ulRandomState = 9U;
    tPersisted.ucAffinity = 40U;
    tPersisted.ucEnergy = 60U;
    tPersisted.ucArousal = 10U;
    assert(PETBEHAVIOR_Init(&tBehavior, &tPersisted, 10000U,
                            100000U + 90000U, 3U));
    assert(PET_BEHAVIOR_VISUAL_LONELY ==
           TEST_Snapshot(&tBehavior).eVisualState);

    tEvent = TEST_Event(PET_BEHAVIOR_EVENT_AGENT_STATE,
                        11000U, 190000U, 2U);
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PET_BEHAVIOR_VISUAL_NEEDS_INPUT ==
           TEST_Snapshot(&tBehavior).eVisualState);
    tEvent.ucValue = 0U;
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PET_BEHAVIOR_VISUAL_LONELY ==
           TEST_Snapshot(&tBehavior).eVisualState);
}

static void TEST_PersistenceAndBounds(void)
{
    PET_BEHAVIOR tBehavior;
    PET_BEHAVIOR_PERSISTED tPersisted;
    PET_BEHAVIOR_EVENT tEvent;

    assert(256U >= sizeof(PET_BEHAVIOR));
    assert(!PETBEHAVIOR_Init(NULL, NULL, 0U, 0U, 0U));
    assert(PETBEHAVIOR_Init(&tBehavior, NULL, 0U, 0U, 4U));
    tEvent = TEST_Event(PET_BEHAVIOR_EVENT_AGENT_STATE, 0U, 0U, 9U);
    assert(!PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PETBEHAVIOR_GetPersisted(&tBehavior, &tPersisted));
    assert(PET_BEHAVIOR_PERSIST_VERSION == tPersisted.ulVersion);
    PETBEHAVIOR_MarkSaved(&tBehavior);
    assert(!TEST_Snapshot(&tBehavior).bSaveRequired);
    assert(!PETBEHAVIOR_GetSnapshot(NULL, NULL));
    assert(!PETBEHAVIOR_GetPersisted(NULL, NULL));

    (void)memset(&tPersisted, 0, sizeof(tPersisted));
    tPersisted.ulVersion = PET_BEHAVIOR_PERSIST_VERSION;
    tPersisted.ucAffinity = 101U;
    tPersisted.ucEnergy = 1U;
    tPersisted.ucArousal = 1U;
    assert(PETBEHAVIOR_Init(&tBehavior, &tPersisted, 10U, 500000U, 4U));
    assert(PET_BEHAVIOR_AFFINITY_DEFAULT ==
           TEST_Snapshot(&tBehavior).ucAffinity);
    assert(TEST_Snapshot(&tBehavior).bSaveRequired);
}

static void TEST_FirstTapAndTickWrap(void)
{
    PET_BEHAVIOR tBehavior;
    PET_BEHAVIOR_EVENT tEvent;
    PET_BEHAVIOR_SNAPSHOT tSnapshot;

    assert(PETBEHAVIOR_Init(&tBehavior, NULL, 10U, 600000U, 5U));
    tEvent = TEST_Event(PET_BEHAVIOR_EVENT_TAP, 20U, 600001U, 0U);
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PET_BEHAVIOR_AFFINITY_DEFAULT + 1U ==
           TEST_Snapshot(&tBehavior).ucAffinity);

    assert(PETBEHAVIOR_Init(&tBehavior, NULL, UINT32_MAX - 500U,
                            700000U, 6U));
    tEvent = TEST_Event(PET_BEHAVIOR_EVENT_TAP, UINT32_MAX - 100U,
                        700001U, 0U);
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PETBEHAVIOR_Update(&tBehavior, 1000U, 700002U));
    assert(PET_BEHAVIOR_REACTION_HAPPY ==
           TEST_Snapshot(&tBehavior).eReaction);
    assert(PETBEHAVIOR_Update(&tBehavior, 4000U, 700005U));
    tSnapshot = TEST_Snapshot(&tBehavior);
    assert(PET_BEHAVIOR_REACTION_NONE == tSnapshot.eReaction);
}

static void TEST_AgentTransitionAndAssets(void)
{
    PET_BEHAVIOR tBehavior;
    PET_BEHAVIOR_EVENT tEvent;
    const PET_STATE_ASSET *pAsset;
    uint8_t ucIndex;

    assert(PETBEHAVIOR_Init(&tBehavior, NULL, 10000U, 820800U, 7U));
    tEvent = TEST_Event(PET_BEHAVIOR_EVENT_AGENT_STATE,
                        11000U, 820801U, 3U);
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PET_BEHAVIOR_VISUAL_CELEBRATE ==
           TEST_Snapshot(&tBehavior).eVisualState);
    tEvent.ucValue = 0U;
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PETBEHAVIOR_Update(&tBehavior, 16000U, 820806U));
    assert(PET_BEHAVIOR_VISUAL_CALM ==
           TEST_Snapshot(&tBehavior).eVisualState);

    for (ucIndex = 0U; ucIndex < (uint8_t)PET_BEHAVIOR_VISUAL_COUNT;
         ucIndex++)
    {
        pAsset = PETSTATEASSET_Get((PET_BEHAVIOR_VISUAL_STATE)ucIndex);
        assert(NULL != pAsset);
        assert((PET_BEHAVIOR_VISUAL_STATE)ucIndex == pAsset->eState);
        assert(0 == strncmp(pAsset->pPath, "/:/pet/", 7U));
        assert(NULL != strstr(pAsset->pPath, ".gif"));
    }
    assert(NULL == PETSTATEASSET_Get(PET_BEHAVIOR_VISUAL_COUNT));
    assert((uint8_t)PET_BEHAVIOR_VISUAL_COUNT == PETSTATEASSET_Count());
}

static void TEST_DecayAndDeterministicIdle(void)
{
    PET_BEHAVIOR tBehaviorA;
    PET_BEHAVIOR tBehaviorB;
    PET_BEHAVIOR_SNAPSHOT tSnapshotA;
    PET_BEHAVIOR_SNAPSHOT tSnapshotB;
    uint32_t ulDecisionMs;
    uint32_t ulEpoch;
    uint8_t ucIndex;

    ulEpoch = 10U * 86400U + 12U * 3600U;
    assert(PETBEHAVIOR_Init(&tBehaviorA, NULL, 1000U, ulEpoch, 77U));
    assert(PETBEHAVIOR_Init(&tBehaviorB, NULL, 1000U, ulEpoch, 77U));
    ulDecisionMs = tBehaviorA.ulNextIdleDecisionMs;
    assert(ulDecisionMs == tBehaviorB.ulNextIdleDecisionMs);
    for (ucIndex = 1U; ucIndex <= 60U; ucIndex++)
    {
        assert(PETBEHAVIOR_Update(&tBehaviorA, 1000U + ucIndex * 1000U,
                                  ulEpoch + ucIndex));
    }
    tSnapshotA = TEST_Snapshot(&tBehaviorA);
    assert(PET_BEHAVIOR_AROUSAL_DEFAULT - 1U == tSnapshotA.ucArousal);
    assert(PETBEHAVIOR_Update(&tBehaviorA, ulDecisionMs, ulEpoch + 1800U));
    assert(PETBEHAVIOR_Update(&tBehaviorB, ulDecisionMs, ulEpoch + 1800U));
    tSnapshotA = TEST_Snapshot(&tBehaviorA);
    tSnapshotB = TEST_Snapshot(&tBehaviorB);
    assert(tSnapshotA.eMood == tSnapshotB.eMood);
    assert(tBehaviorA.ulRandomState == tBehaviorB.ulRandomState);
    assert(PET_BEHAVIOR_ENERGY_DEFAULT - 1U == tSnapshotA.ucEnergy);
}

static void TEST_LateRtcSynchronization(void)
{
    PET_BEHAVIOR tBehavior;
    PET_BEHAVIOR_SNAPSHOT tSnapshot;

    assert(PETBEHAVIOR_Init(&tBehavior, NULL, 0U, 0U, 88U));
    assert(PETBEHAVIOR_Update(&tBehavior, 1000U, 900000U));
    tSnapshot = TEST_Snapshot(&tBehavior);
    assert(900000U == tSnapshot.ulLastInteractionEpoch);
    assert(PET_BEHAVIOR_AROUSAL_DEFAULT == tSnapshot.ucArousal);
    assert(PET_BEHAVIOR_ENERGY_DEFAULT == tSnapshot.ucEnergy);
    assert(PETBEHAVIOR_Update(&tBehavior, 61000U, 900060U));
    assert(PET_BEHAVIOR_AROUSAL_DEFAULT - 1U ==
           TEST_Snapshot(&tBehavior).ucArousal);
}

static void TEST_LongTermMoodInputs(void)
{
    PET_BEHAVIOR tBehavior;
    PET_BEHAVIOR_PERSISTED tPersisted;
    PET_BEHAVIOR_EVENT tEvent;

    (void)memset(&tPersisted, 0, sizeof(tPersisted));
    tPersisted.ulVersion = PET_BEHAVIOR_PERSIST_VERSION;
    tPersisted.ulLastInteractionEpoch = 993600U;
    tPersisted.ulRandomState = 99U;
    tPersisted.ucAffinity = 50U;
    tPersisted.ucEnergy = 70U;
    tPersisted.ucArousal = 60U;
    assert(PETBEHAVIOR_Init(&tBehavior, &tPersisted, 1000U,
                            993600U, 99U));
    assert(PETBEHAVIOR_Update(&tBehavior, 9000U, 993608U));
    assert(PET_BEHAVIOR_MOOD_CURIOUS ==
           TEST_Snapshot(&tBehavior).eMood);

    tPersisted.ulLastInteractionEpoch = 800000U;
    tPersisted.ucArousal = 10U;
    assert(PETBEHAVIOR_Init(&tBehavior, &tPersisted, 10000U,
                            993600U, 99U));
    assert(PET_BEHAVIOR_MOOD_LONELY == TEST_Snapshot(&tBehavior).eMood);
    tEvent = TEST_Event(PET_BEHAVIOR_EVENT_TAP, 11000U, 993601U, 0U);
    assert(PETBEHAVIOR_ProcessEvent(&tBehavior, &tEvent));
    assert(PETBEHAVIOR_Update(&tBehavior, 19000U, 993609U));
    assert(PET_BEHAVIOR_MOOD_CALM == TEST_Snapshot(&tBehavior).eMood);
}

int main(void)
{
    TEST_TapReactions();
    TEST_Cooldowns();
    TEST_TimeAndAgentPriority();
    TEST_PersistenceAndBounds();
    TEST_FirstTapAndTickWrap();
    TEST_AgentTransitionAndAssets();
    TEST_DecayAndDeterministicIdle();
    TEST_LateRtcSynchronization();
    TEST_LongTermMoodInputs();
    (void)printf("PASS pet_behavior_host_test\n");
    return 0;
}
