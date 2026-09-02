#ifndef PET_INTERACTION_ARBITER_H
#define PET_INTERACTION_ARBITER_H

#include <stdbool.h>

/* PET_INTERACTION_INPUT: 本地互动仲裁的一次无副作用输入快照。 */
typedef struct _PET_INTERACTION_INPUT
{
    bool bHighPriorityRemote;
    bool bMemoryPanelOpen;
    bool bStrokeOwnsTouch;
    bool bBallOwnsVisual;
} PET_INTERACTION_INPUT;

/* PET_INTERACTION_DECISION: 同一快照对应的互斥、抢占和视觉冻结决策。 */
typedef struct _PET_INTERACTION_DECISION
{
    bool bAllowStroke;
    bool bAllowBall;
    bool bCancelStroke;
    bool bCancelBall;
    bool bCloseMemoryPanel;
    bool bFreezeBehaviorVisual;
} PET_INTERACTION_DECISION;

void PETINTERACTION_Resolve(const PET_INTERACTION_INPUT *pInput,
                            PET_INTERACTION_DECISION *pDecision);

#endif /* PET_INTERACTION_ARBITER_H */
