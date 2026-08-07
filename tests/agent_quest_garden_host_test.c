#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "agent_quest_garden.h"

static void TEST_SetSession(AGENTPET_SNAPSHOT *pSnapshot, uint8_t ucIndex,
                            uint32_t ulTaskHash, uint8_t ucState)
{
    assert(NULL != pSnapshot);
    assert(AGENTPET_MAX_SESSION_COUNT > ucIndex);

    pSnapshot->aSessions[ucIndex].ulTaskHash = ulTaskHash;
    pSnapshot->aSessions[ucIndex].ucState = ucState;
    return;
}

static void TEST_TransitionAndDeduplicate(void)
{
    QUEST_GARDEN tGarden;
    QUEST_GARDEN_RESULT tResult;
    QUEST_GARDEN_VIEW tView;
    AGENTPET_SNAPSHOT tSnapshot;

    (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
    assert(QUESTGARDEN_Init(&tGarden, NULL));
    tSnapshot.ucSessionCount = 1U;
    TEST_SetSession(&tSnapshot, 0U, 0x1234UL, AGENTPET_STATE_COMPLETED);
    assert(QUESTGARDEN_ProcessSnapshot(&tGarden, 20000U, &tSnapshot, &tResult));
    assert(tResult.bBaselineCreated);
    assert(0U == tResult.ucNewSeedCount);

    assert(QUESTGARDEN_ProcessSnapshot(&tGarden, 20000U, &tSnapshot, &tResult));
    assert(0U == tResult.ucNewSeedCount);
    TEST_SetSession(&tSnapshot, 0U, 0x1234UL, AGENTPET_STATE_RUNNING);
    assert(QUESTGARDEN_ProcessSnapshot(&tGarden, 20000U, &tSnapshot, &tResult));
    TEST_SetSession(&tSnapshot, 0U, 0x1234UL, AGENTPET_STATE_COMPLETED);
    assert(QUESTGARDEN_ProcessSnapshot(&tGarden, 20000U, &tSnapshot, &tResult));
    assert(1U == tResult.ucNewSeedCount);
    assert(QUESTGARDEN_ProcessSnapshot(&tGarden, 20000U, &tSnapshot, &tResult));
    assert(0U == tResult.ucNewSeedCount);
    assert(QUESTGARDEN_GetView(&tGarden, &tView));
    assert(1U == tView.ulTodayCompleted);
    assert(1U == tView.ulPending);
}

static void TEST_PendingCollectionAndOverflow(void)
{
    QUEST_GARDEN tGarden;
    QUEST_GARDEN_RESULT tResult;
    QUEST_GARDEN_VIEW tView;
    AGENTPET_SNAPSHOT tSnapshot;
    uint8_t ucIndex;

    (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
    assert(QUESTGARDEN_Init(&tGarden, NULL));
    tSnapshot.ucSessionCount = AGENTPET_MAX_SESSION_COUNT;
    for (ucIndex = 0U; ucIndex < AGENTPET_MAX_SESSION_COUNT; ucIndex++)
    {
        TEST_SetSession(&tSnapshot, ucIndex, (uint32_t)ucIndex + 1U,
                        AGENTPET_STATE_RUNNING);
    }
    assert(QUESTGARDEN_ProcessSnapshot(&tGarden, 21000U, &tSnapshot, &tResult));
    for (ucIndex = 0U; ucIndex < AGENTPET_MAX_SESSION_COUNT; ucIndex++)
    {
        TEST_SetSession(&tSnapshot, ucIndex, (uint32_t)ucIndex + 1U,
                        AGENTPET_STATE_COMPLETED);
    }
    assert(QUESTGARDEN_ProcessSnapshot(&tGarden, 21000U, &tSnapshot, &tResult));
    assert(QUEST_GARDEN_MAX_PENDING == tResult.ucNewSeedCount);
    assert(QUESTGARDEN_GetView(&tGarden, &tView));
    assert(QUEST_GARDEN_MAX_PENDING == tView.ulPending);
    assert((AGENTPET_MAX_SESSION_COUNT - QUEST_GARDEN_MAX_PENDING) ==
           tView.ulOverflow);

    for (ucIndex = 0U; ucIndex < QUEST_GARDEN_MAX_LEAVES; ucIndex++)
    {
        assert(QUESTGARDEN_Collect(&tGarden, &tResult));
        assert(tResult.bChanged);
    }
    assert(QUESTGARDEN_GetView(&tGarden, &tView));
    assert(QUEST_GARDEN_MAX_LEAVES == tView.ucVisibleLeaves);
    assert(tView.bFlowerVisible);
    assert(3U == tView.ulPending);
}

static void TEST_DayRolloverAndPersistence(void)
{
    QUEST_GARDEN tGarden;
    QUEST_GARDEN_RESULT tResult;
    QUEST_GARDEN_VIEW tView;
    QUEST_GARDEN_PERSISTED tPersisted;

    (void)memset(&tPersisted, 0, sizeof(tPersisted));
    tPersisted.ulVersion = QUEST_GARDEN_PERSIST_VERSION;
    tPersisted.ulDay = 22000U;
    tPersisted.ulTodayCompleted = 3U;
    tPersisted.ulTodayCollected = 2U;
    tPersisted.ulPending = 1U;
    tPersisted.ulStreak = 4U;
    assert(QUESTGARDEN_Init(&tGarden, &tPersisted));
    assert(QUESTGARDEN_Rollover(&tGarden, 22001U, &tResult));
    assert(tResult.bSaveRequired);
    assert(QUESTGARDEN_GetView(&tGarden, &tView));
    assert(5U == tView.ulStreak);
    assert(0U == tView.ulTodayCollected);
    assert(1U == tView.ulPending);

    assert(QUESTGARDEN_Rollover(&tGarden, 21999U, &tResult));
    assert(!tResult.bChanged);
    assert(QUESTGARDEN_Rollover(&tGarden, 22004U, &tResult));
    assert(QUESTGARDEN_GetView(&tGarden, &tView));
    assert(0U == tView.ulStreak);

    tPersisted.ulVersion = QUEST_GARDEN_PERSIST_VERSION + 1U;
    tPersisted.ulPending = UINT32_MAX;
    assert(QUESTGARDEN_Init(&tGarden, &tPersisted));
    assert(QUESTGARDEN_GetView(&tGarden, &tView));
    assert(0U == tView.ulPending);
}

static void TEST_ParameterAndCountBounds(void)
{
    QUEST_GARDEN tGarden;
    QUEST_GARDEN_RESULT tResult;
    QUEST_GARDEN_VIEW tView;
    QUEST_GARDEN_PERSISTED tPersisted;
    AGENTPET_SNAPSHOT tSnapshot;

    assert(!QUESTGARDEN_Init(NULL, NULL));
    assert(QUESTGARDEN_Init(&tGarden, NULL));
    assert(!QUESTGARDEN_GetView(NULL, &tView));
    assert(!QUESTGARDEN_GetView(&tGarden, NULL));
    (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
    tSnapshot.ucSessionCount = AGENTPET_MAX_SESSION_COUNT + 1U;
    assert(!QUESTGARDEN_ProcessSnapshot(
        &tGarden, 1U, &tSnapshot, &tResult));

    (void)memset(&tPersisted, 0, sizeof(tPersisted));
    tPersisted.ulVersion = QUEST_GARDEN_PERSIST_VERSION;
    tPersisted.ulTodayCollected = UINT32_MAX;
    tPersisted.ulPending = 1U;
    assert(QUESTGARDEN_Init(&tGarden, &tPersisted));
    assert(QUESTGARDEN_Collect(&tGarden, &tResult));
    assert(QUESTGARDEN_GetView(&tGarden, &tView));
    assert(UINT32_MAX == tView.ulTodayCollected);
    assert(0U == tView.ulPending);
}

static void TEST_DeterministicStress(void)
{
    QUEST_GARDEN tGarden;
    QUEST_GARDEN_RESULT tResult;
    QUEST_GARDEN_VIEW tView;
    QUEST_GARDEN_PERSISTED tPersisted;
    AGENTPET_SNAPSHOT tSnapshot;
    uint16_t usCycle;

    (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
    assert(QUESTGARDEN_Init(&tGarden, NULL));
    tSnapshot.ucSessionCount = 1U;
    TEST_SetSession(&tSnapshot, 0U, 0xABCDEF01UL, AGENTPET_STATE_RUNNING);
    assert(QUESTGARDEN_ProcessSnapshot(&tGarden, 23000U, &tSnapshot, &tResult));
    for (usCycle = 0U; usCycle < 200U; usCycle++)
    {
        TEST_SetSession(&tSnapshot, 0U, 0xABCDEF01UL,
                        AGENTPET_STATE_COMPLETED);
        assert(QUESTGARDEN_ProcessSnapshot(
            &tGarden, 23000U, &tSnapshot, &tResult));
        assert(1U == tResult.ucNewSeedCount);
        assert(QUESTGARDEN_Collect(&tGarden, &tResult));
        TEST_SetSession(&tSnapshot, 0U, 0xABCDEF01UL,
                        AGENTPET_STATE_RUNNING);
        assert(QUESTGARDEN_ProcessSnapshot(
            &tGarden, 23000U, &tSnapshot, &tResult));
    }
    assert(QUESTGARDEN_GetView(&tGarden, &tView));
    assert(200U == tView.ulTodayCompleted);
    assert(200U == tView.ulTodayCollected);
    assert(0U == tView.ulPending);
    assert(QUESTGARDEN_GetPersisted(&tGarden, &tPersisted));
    assert(QUESTGARDEN_Init(&tGarden, &tPersisted));
    assert(QUESTGARDEN_GetView(&tGarden, &tView));
    assert(200U == tView.ulTodayCompleted);
    assert(200U == tView.ulTodayCollected);
}

int main(void)
{
    TEST_TransitionAndDeduplicate();
    TEST_PendingCollectionAndOverflow();
    TEST_DayRolloverAndPersistence();
    TEST_ParameterAndCountBounds();
    TEST_DeterministicStress();
    (void)printf("PASS agent_quest_garden_host_test\n");
    return 0;
}
