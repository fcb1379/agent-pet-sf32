#include <assert.h>
#include <stdio.h>

#include "pet_interaction_arbiter.h"

static PET_INTERACTION_DECISION TEST_Resolve(
    bool bRemote, bool bMemory, bool bStroke, bool bBall)
{
    PET_INTERACTION_INPUT tInput;
    PET_INTERACTION_DECISION tDecision;

    tInput.bHighPriorityRemote = bRemote;
    tInput.bMemoryPanelOpen = bMemory;
    tInput.bStrokeOwnsTouch = bStroke;
    tInput.bBallOwnsVisual = bBall;
    PETINTERACTION_Resolve(&tInput, &tDecision);
    return tDecision;
}

static void TEST_DefaultAllowsBoth(void)
{
    PET_INTERACTION_DECISION tDecision;

    tDecision = TEST_Resolve(false, false, false, false);
    assert(tDecision.bAllowStroke);
    assert(tDecision.bAllowBall);
    assert(!tDecision.bFreezeBehaviorVisual);
    return;
}

static void TEST_MemoryCancelsLocalInteractions(void)
{
    PET_INTERACTION_DECISION tDecision;

    tDecision = TEST_Resolve(false, true, true, true);
    assert(!tDecision.bAllowStroke);
    assert(!tDecision.bAllowBall);
    assert(tDecision.bCancelStroke);
    assert(tDecision.bCancelBall);
    assert(tDecision.bFreezeBehaviorVisual);
    return;
}

static void TEST_BallAndStrokeAreExclusive(void)
{
    PET_INTERACTION_DECISION tDecision;

    tDecision = TEST_Resolve(false, false, false, true);
    assert(tDecision.bAllowBall);
    assert(!tDecision.bAllowStroke);
    assert(tDecision.bCancelStroke);
    assert(tDecision.bFreezeBehaviorVisual);

    tDecision = TEST_Resolve(false, false, true, false);
    assert(tDecision.bAllowStroke);
    assert(!tDecision.bAllowBall);
    assert(tDecision.bCancelBall);
    assert(tDecision.bFreezeBehaviorVisual);
    return;
}

static void TEST_RemoteAlwaysPreempts(void)
{
    PET_INTERACTION_DECISION tDecision;

    tDecision = TEST_Resolve(true, true, true, true);
    assert(!tDecision.bAllowStroke);
    assert(!tDecision.bAllowBall);
    assert(tDecision.bCancelStroke);
    assert(tDecision.bCancelBall);
    assert(tDecision.bCloseMemoryPanel);
    assert(!tDecision.bFreezeBehaviorVisual);
    return;
}

int main(void)
{
    PET_INTERACTION_DECISION tDecision;

    PETINTERACTION_Resolve(NULL, &tDecision);
    assert(!tDecision.bAllowStroke);
    PETINTERACTION_Resolve(NULL, NULL);
    TEST_DefaultAllowsBoth();
    TEST_MemoryCancelsLocalInteractions();
    TEST_BallAndStrokeAreExclusive();
    TEST_RemoteAlwaysPreempts();
    puts("pet interaction arbiter host tests passed");
    return 0;
}
