#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "momo_agent_squad.h"

static uint32_t l_ulRandomState = 0x4D4F4D4FUL;

static uint32_t TEST_NextRandom(void)
{
    l_ulRandomState = (l_ulRandomState * 1664525UL) + 1013904223UL;

    return l_ulRandomState;
}

static uint8_t TEST_StatePriority(uint8_t ucState)
{
    static const uint8_t l_aPriorities[MOMO_AGENT_SQUAD_STATE_COUNT] =
    {
        4U,
        3U,
        1U,
        2U,
        0U
    };

    assert(AGENTPET_STATE_ERROR >= ucState);

    return l_aPriorities[ucState];
}

static void TEST_SetSession(
    AGENTPET_SNAPSHOT *pSnapshot,
    uint8_t ucIndex,
    uint8_t ucState,
    uint16_t usAgeSeconds,
    uint32_t ulTaskHash)
{
    assert(NULL != pSnapshot);
    assert(AGENTPET_MAX_SESSION_COUNT > ucIndex);
    pSnapshot->aSessions[ucIndex].ucState = ucState;
    pSnapshot->aSessions[ucIndex].ucProvider = ucIndex % 3U;
    pSnapshot->aSessions[ucIndex].ucSource = ucIndex % 4U;
    pSnapshot->aSessions[ucIndex].ucFlags = 0U;
    pSnapshot->aSessions[ucIndex].ulTaskHash = ulTaskHash;
    pSnapshot->aSessions[ucIndex].usAgeSeconds = usAgeSeconds;

    return;
}

static void TEST_CountShapes(void)
{
    static const uint8_t l_aCounts[] = {0U, 1U, 5U, 6U, 12U};
    AGENTPET_SNAPSHOT tSnapshot;
    MOMO_AGENT_SQUAD_VIEW tView;
    uint8_t ucCase;
    uint8_t ucIndex;

    for (ucCase = 0U; ucCase < sizeof(l_aCounts); ucCase++)
    {
        (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
        tSnapshot.ucSessionCount = l_aCounts[ucCase];
        for (ucIndex = 0U; ucIndex < tSnapshot.ucSessionCount; ucIndex++)
        {
            TEST_SetSession(
                &tSnapshot,
                ucIndex,
                AGENTPET_STATE_RUNNING,
                ucIndex,
                (uint32_t)ucIndex + 1U);
        }
        assert(MOMOAGENTSQUAD_BuildView(&tSnapshot, NULL, &tView));
        assert(((tSnapshot.ucSessionCount < MOMO_AGENT_SQUAD_VISIBLE_MAX) ?
            tSnapshot.ucSessionCount : MOMO_AGENT_SQUAD_VISIBLE_MAX) ==
            tView.ucVisibleCount);
        assert((tSnapshot.ucSessionCount - tView.ucVisibleCount) ==
            tView.ucHiddenCount);
        assert(tSnapshot.ucSessionCount ==
            tView.aStateCounts[AGENTPET_STATE_RUNNING]);
    }

    return;
}

static void TEST_PriorityAgeAndStability(void)
{
    AGENTPET_SNAPSHOT tSnapshot;
    MOMO_AGENT_SQUAD_VIEW tView;

    (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
    tSnapshot.ucAggregateState = AGENTPET_STATE_ERROR;
    tSnapshot.ucSessionCount = 8U;
    TEST_SetSession(&tSnapshot, 0U, AGENTPET_STATE_IDLE, 1U, 0U);
    TEST_SetSession(&tSnapshot, 1U, AGENTPET_STATE_RUNNING, 1U, 1U);
    TEST_SetSession(&tSnapshot, 2U, AGENTPET_STATE_COMPLETED, UINT16_MAX, 2U);
    TEST_SetSession(&tSnapshot, 3U, AGENTPET_STATE_NEEDS_INPUT, 4U, 3U);
    TEST_SetSession(&tSnapshot, 4U, AGENTPET_STATE_ERROR, 9U, 4U);
    TEST_SetSession(&tSnapshot, 5U, AGENTPET_STATE_ERROR, 2U, 5U);
    TEST_SetSession(&tSnapshot, 6U, AGENTPET_STATE_COMPLETED, 3U, 6U);
    TEST_SetSession(&tSnapshot, 7U, AGENTPET_STATE_COMPLETED, 3U, 7U);
    assert(MOMOAGENTSQUAD_BuildView(&tSnapshot, NULL, &tView));
    assert(5U == tView.aVisibleIndices[0U]);
    assert(4U == tView.aVisibleIndices[1U]);
    assert(3U == tView.aVisibleIndices[2U]);
    assert(6U == tView.aVisibleIndices[3U]);
    assert(7U == tView.aVisibleIndices[4U]);
    assert(3U == tView.ucHiddenCount);

    return;
}

static void TEST_SelectionAndPreemption(void)
{
    AGENTPET_SNAPSHOT tSnapshot;
    MOMO_AGENT_SQUAD_IDENTITY tIdentity;
    MOMO_AGENT_SQUAD_VIEW tView;
    const AGENTPET_SESSION *pSession;

    (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
    tSnapshot.ucAggregateState = AGENTPET_STATE_RUNNING;
    tSnapshot.ucSessionCount = 3U;
    TEST_SetSession(&tSnapshot, 0U, AGENTPET_STATE_RUNNING, 2U, 0x10UL);
    TEST_SetSession(&tSnapshot, 1U, AGENTPET_STATE_RUNNING, 3U, 0x11UL);
    TEST_SetSession(&tSnapshot, 2U, AGENTPET_STATE_IDLE, 1U, 0x12UL);
    assert(MOMOAGENTSQUAD_GetIdentity(&tSnapshot.aSessions[1U], &tIdentity));
    assert(MOMOAGENTSQUAD_BuildView(&tSnapshot, &tIdentity, &tView));
    assert(1U == tView.ucSelectedPosition);
    assert(2U == MOMOAGENTSQUAD_Next(&tView));

    TEST_SetSession(&tSnapshot, 2U, AGENTPET_STATE_ERROR, 1U, 0x12UL);
    tSnapshot.ucAggregateState = AGENTPET_STATE_ERROR;
    assert(MOMOAGENTSQUAD_BuildView(&tSnapshot, &tIdentity, &tView));
    assert(0U == tView.ucSelectedPosition);
    pSession = MOMOAGENTSQUAD_GetSession(&tSnapshot, &tView, 0U);
    assert(NULL != pSession);
    assert(AGENTPET_STATE_ERROR == pSession->ucState);

    tIdentity.ulTaskHash = 0xFFFFFFFFUL;
    assert(MOMOAGENTSQUAD_BuildView(&tSnapshot, &tIdentity, &tView));
    assert(0U == tView.ucSelectedPosition);

    return;
}

static void TEST_Formatting(void)
{
    AGENTPET_SNAPSHOT tSnapshot;
    MOMO_AGENT_SQUAD_VIEW tView;
    char aBuffer[64];
    char aSmallBuffer[4];

    (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
    tSnapshot.ucAggregateState = AGENTPET_STATE_NEEDS_INPUT;
    tSnapshot.ucSessionCount = 2U;
    TEST_SetSession(
        &tSnapshot, 0U, AGENTPET_STATE_NEEDS_INPUT, 180U, 0x1234UL);
    tSnapshot.aSessions[0U].ucProvider = 2U;
    tSnapshot.aSessions[0U].ucFlags = AGENTPET_TASK_FLAG_APPROVAL;
    TEST_SetSession(
        &tSnapshot, 1U, AGENTPET_STATE_IDLE, UINT16_MAX, 0x5678UL);
    assert(MOMOAGENTSQUAD_BuildView(&tSnapshot, NULL, &tView));
    assert(MOMOAGENTSQUAD_FormatSummary(
        &tView, false, aBuffer, sizeof(aBuffer)));
    assert(0 == strcmp("Off | E0 N1 R0 D0 I1", aBuffer));
    assert(MOMOAGENTSQUAD_FormatDetail(
        &tSnapshot.aSessions[0U], aBuffer, sizeof(aBuffer)));
    assert(0 == strcmp("Claude #1234 Needs input ! 3m", aBuffer));
    assert(MOMOAGENTSQUAD_FormatDetail(
        &tSnapshot.aSessions[1U], aBuffer, sizeof(aBuffer)));
    assert(NULL != strstr(aBuffer, "unknown"));
    assert(!MOMOAGENTSQUAD_FormatDetail(
        &tSnapshot.aSessions[0U], aSmallBuffer, sizeof(aSmallBuffer)));
    assert('\0' == aSmallBuffer[sizeof(aSmallBuffer) - 1U]);

    return;
}

static void TEST_InvalidInputs(void)
{
    AGENTPET_SNAPSHOT tSnapshot;
    MOMO_AGENT_SQUAD_IDENTITY tIdentity;
    MOMO_AGENT_SQUAD_VIEW tView;
    char aBuffer[16];

    (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
    (void)memset(&tView, 0xA5, sizeof(tView));
    assert(!MOMOAGENTSQUAD_BuildView(NULL, NULL, &tView));
    assert(0U == tView.ucVisibleCount);
    assert(!MOMOAGENTSQUAD_BuildView(&tSnapshot, NULL, NULL));
    tSnapshot.ucSessionCount = AGENTPET_MAX_SESSION_COUNT + 1U;
    assert(!MOMOAGENTSQUAD_BuildView(&tSnapshot, NULL, &tView));
    tSnapshot.ucSessionCount = 1U;
    tSnapshot.ucAggregateState = AGENTPET_STATE_ERROR + 1U;
    assert(!MOMOAGENTSQUAD_BuildView(&tSnapshot, NULL, &tView));
    tSnapshot.ucAggregateState = AGENTPET_STATE_IDLE;
    tSnapshot.aSessions[0U].ucState = AGENTPET_STATE_ERROR + 1U;
    assert(!MOMOAGENTSQUAD_BuildView(&tSnapshot, NULL, &tView));
    assert(!MOMOAGENTSQUAD_GetIdentity(NULL, &tIdentity));
    assert(!tIdentity.bValid);
    assert(!MOMOAGENTSQUAD_GetIdentity(&tSnapshot.aSessions[0U], NULL));
    assert(NULL == MOMOAGENTSQUAD_GetSession(NULL, &tView, 0U));
    assert(!MOMOAGENTSQUAD_FormatSummary(NULL, true, aBuffer, sizeof(aBuffer)));
    assert(!MOMOAGENTSQUAD_FormatDetail(NULL, aBuffer, sizeof(aBuffer)));
    assert(0U == MOMOAGENTSQUAD_Next(NULL));
    tView.ucVisibleCount = MOMO_AGENT_SQUAD_VISIBLE_MAX + 1U;
    assert(NULL == MOMOAGENTSQUAD_GetSession(&tSnapshot, &tView, 0U));
    assert(0U == MOMOAGENTSQUAD_Next(&tView));

    return;
}

static void TEST_DeterministicStress(void)
{
    AGENTPET_SNAPSHOT tBefore;
    AGENTPET_SNAPSHOT tSnapshot;
    MOMO_AGENT_SQUAD_VIEW tView;
    const AGENTPET_SESSION *pCurrent;
    const AGENTPET_SESSION *pPrevious;
    uint16_t usCycle;
    uint8_t aExpectedCounts[MOMO_AGENT_SQUAD_STATE_COUNT];
    uint8_t ucIndex;
    uint8_t ucPosition;
    uint8_t ucPreviousPriority;
    uint8_t ucCurrentPriority;
    uint16_t usPreviousAge;
    uint16_t usCurrentAge;

    for (usCycle = 0U; usCycle < 1000U; usCycle++)
    {
        (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
        (void)memset(aExpectedCounts, 0, sizeof(aExpectedCounts));
        tSnapshot.ucSessionCount =
            (uint8_t)(TEST_NextRandom() %
                (AGENTPET_MAX_SESSION_COUNT + 1U));
        tSnapshot.ucAggregateState =
            (uint8_t)(TEST_NextRandom() % MOMO_AGENT_SQUAD_STATE_COUNT);
        for (ucIndex = 0U; ucIndex < tSnapshot.ucSessionCount; ucIndex++)
        {
            uint8_t ucState;
            uint16_t usAge;

            ucState = (uint8_t)(
                TEST_NextRandom() % MOMO_AGENT_SQUAD_STATE_COUNT);
            usAge = (0U == (TEST_NextRandom() & 0x0FUL)) ?
                UINT16_MAX : (uint16_t)(TEST_NextRandom() % UINT16_MAX);
            TEST_SetSession(
                &tSnapshot, ucIndex, ucState, usAge, TEST_NextRandom());
            aExpectedCounts[ucState]++;
        }
        (void)memcpy(&tBefore, &tSnapshot, sizeof(tBefore));
        assert(MOMOAGENTSQUAD_BuildView(&tSnapshot, NULL, &tView));
        assert(0 == memcmp(&tBefore, &tSnapshot, sizeof(tSnapshot)));
        assert(0 == memcmp(
            aExpectedCounts,
            tView.aStateCounts,
            sizeof(aExpectedCounts)));
        for (ucPosition = 0U;
             ucPosition < tView.ucVisibleCount;
             ucPosition++)
        {
            uint8_t ucOtherPosition;

            assert(tSnapshot.ucSessionCount >
                tView.aVisibleIndices[ucPosition]);
            for (ucOtherPosition = (uint8_t)(ucPosition + 1U);
                 ucOtherPosition < tView.ucVisibleCount;
                 ucOtherPosition++)
            {
                assert(tView.aVisibleIndices[ucPosition] !=
                    tView.aVisibleIndices[ucOtherPosition]);
            }
            if (0U == ucPosition)
            {
                continue;
            }
            pPrevious = MOMOAGENTSQUAD_GetSession(
                &tSnapshot, &tView, (uint8_t)(ucPosition - 1U));
            pCurrent = MOMOAGENTSQUAD_GetSession(
                &tSnapshot, &tView, ucPosition);
            assert((NULL != pPrevious) && (NULL != pCurrent));
            ucPreviousPriority = TEST_StatePriority(pPrevious->ucState);
            ucCurrentPriority = TEST_StatePriority(pCurrent->ucState);
            assert(ucPreviousPriority <= ucCurrentPriority);
            if (ucPreviousPriority == ucCurrentPriority)
            {
                usPreviousAge = pPrevious->usAgeSeconds;
                usCurrentAge = pCurrent->usAgeSeconds;
                assert((usPreviousAge <= usCurrentAge) ||
                    (UINT16_MAX == usCurrentAge));
            }
        }
    }

    return;
}

int main(void)
{
    assert(sizeof(MOMO_AGENT_SQUAD_VIEW) < 128U);
    assert((sizeof(MOMO_AGENT_SQUAD_VIEW) +
        sizeof(MOMO_AGENT_SQUAD_IDENTITY) + sizeof(uint32_t)) < 128U);
    TEST_CountShapes();
    TEST_PriorityAgeAndStability();
    TEST_SelectionAndPreemption();
    TEST_Formatting();
    TEST_InvalidInputs();
    TEST_DeterministicStress();
    (void)printf(
        "Momo Agent squad host tests passed "
        "(view=%lu bytes, identity=%lu bytes, random=1000).\n",
        (unsigned long)sizeof(MOMO_AGENT_SQUAD_VIEW),
        (unsigned long)sizeof(MOMO_AGENT_SQUAD_IDENTITY));

    return 0;
}
