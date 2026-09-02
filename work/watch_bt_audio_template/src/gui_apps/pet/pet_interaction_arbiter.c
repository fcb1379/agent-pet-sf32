#include "pet_interaction_arbiter.h"

#include <string.h>

/***************************
 * PETINTERACTION_Resolve: 计算回忆、抚摸、玩球和远端状态的统一仲裁结果
 * 参数：
 *   - pInput: 当前互动所有权与远端优先级快照，仅输入
 *   - pDecision: 输出决策
 * 返回值：无
 ***************************/
void PETINTERACTION_Resolve(const PET_INTERACTION_INPUT *pInput,
                            PET_INTERACTION_DECISION *pDecision)
{
    if (NULL == pDecision)
    {
        return;
    }
    (void)memset(pDecision, 0, sizeof(*pDecision));
    if (NULL == pInput)
    {
        return;
    }

    if (pInput->bHighPriorityRemote)
    {
        pDecision->bCancelStroke = true;
        pDecision->bCancelBall = true;
        pDecision->bCloseMemoryPanel = pInput->bMemoryPanelOpen;
        return;
    }
    if (pInput->bMemoryPanelOpen)
    {
        pDecision->bCancelStroke = true;
        pDecision->bCancelBall = true;
        pDecision->bFreezeBehaviorVisual = true;
        return;
    }
    if (pInput->bBallOwnsVisual)
    {
        pDecision->bAllowBall = true;
        pDecision->bCancelStroke = true;
        pDecision->bFreezeBehaviorVisual = true;
        return;
    }
    if (pInput->bStrokeOwnsTouch)
    {
        pDecision->bAllowStroke = true;
        pDecision->bCancelBall = true;
        pDecision->bFreezeBehaviorVisual = true;
        return;
    }

    pDecision->bAllowStroke = true;
    pDecision->bAllowBall = true;
    return;
}
