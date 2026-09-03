#include "momo_agent_squad.h"

#include <stdio.h>
#include <string.h>

#define MOMO_AGENT_SQUAD_UNKNOWN_AGE (UINT16_MAX)
#define MOMO_AGENT_SQUAD_SECONDS_PER_MINUTE (60U)
#define MOMO_AGENT_SQUAD_SECONDS_PER_HOUR (3600U)

_Static_assert(
    sizeof(MOMO_AGENT_SQUAD_VIEW) < 128U,
    "Momo Agent squad view exceeds its resource budget");

/***************************
 * Local_StatePriority: 返回状态的固定显示优先级
 * 参数：
 *   - ucState: Agent Pet 状态
 * 返回值：数值越小优先级越高；非法状态返回最低优先级之后
 ***************************/
static uint8_t Local_StatePriority(uint8_t ucState)
{
    static const uint8_t l_aPriorities[MOMO_AGENT_SQUAD_STATE_COUNT] =
    {
        4U,
        3U,
        1U,
        2U,
        0U
    };

    if (AGENTPET_STATE_ERROR < ucState)
    {
        return MOMO_AGENT_SQUAD_STATE_COUNT;
    }

    return l_aPriorities[ucState];
}

/***************************
 * Local_ComesBefore: 判断左侧会话是否应排在右侧之前
 * 参数：
 *   - pLeft: 左侧会话，只读
 *   - pRight: 右侧会话，只读
 * 返回值：左侧优先时返回 true；相同排序键返回 false 以保持稳定性
 ***************************/
static bool Local_ComesBefore(
    const AGENTPET_SESSION *pLeft,
    const AGENTPET_SESSION *pRight)
{
    uint8_t ucLeftPriority;
    uint8_t ucRightPriority;

    if ((NULL == pLeft) || (NULL == pRight))
    {
        return false;
    }
    ucLeftPriority = Local_StatePriority(pLeft->ucState);
    ucRightPriority = Local_StatePriority(pRight->ucState);
    if (ucLeftPriority != ucRightPriority)
    {
        return (ucLeftPriority < ucRightPriority);
    }
    if (pLeft->usAgeSeconds == pRight->usAgeSeconds)
    {
        return false;
    }
    if (MOMO_AGENT_SQUAD_UNKNOWN_AGE == pLeft->usAgeSeconds)
    {
        return false;
    }
    if (MOMO_AGENT_SQUAD_UNKNOWN_AGE == pRight->usAgeSeconds)
    {
        return true;
    }

    return (pLeft->usAgeSeconds < pRight->usAgeSeconds);
}

/***************************
 * Local_IdentityMatches: 比较只读会话和本地临时身份
 * 参数：
 *   - pSession: Agent 会话
 *   - pIdentity: 本地临时身份
 * 返回值：三个展示字段都相同且身份有效时返回 true
 ***************************/
static bool Local_IdentityMatches(
    const AGENTPET_SESSION *pSession,
    const MOMO_AGENT_SQUAD_IDENTITY *pIdentity)
{
    if ((NULL == pSession) || (NULL == pIdentity) || !pIdentity->bValid)
    {
        return false;
    }

    return (pSession->ulTaskHash == pIdentity->ulTaskHash) &&
        (pSession->ucProvider == pIdentity->ucProvider) &&
        (pSession->ucSource == pIdentity->ucSource);
}

/***************************
 * Local_FormatSucceeded: 验证 snprintf 结果并保证缓冲区终止
 * 参数：
 *   - lResult: snprintf 返回值
 *   - pBuffer: 输出缓冲区
 *   - ulBufferSize: 输出缓冲区大小
 * 返回值：完整写入返回 true，否则返回 false
 ***************************/
static bool Local_FormatSucceeded(
    int lResult,
    char *pBuffer,
    size_t ulBufferSize)
{
    if ((NULL == pBuffer) || (0U == ulBufferSize))
    {
        return false;
    }
    pBuffer[ulBufferSize - 1U] = '\0';

    return (0 <= lResult) && ((size_t)lResult < ulBufferSize);
}

/***************************
 * MOMOAGENTSQUAD_BuildView: 校验、统计并稳定排序快照
 * 参数：
 *   - pSnapshot: 已校验快照的只读副本
 *   - pPreviousIdentity: 可选的上一次本地选择
 *   - pView: 输出固定容量视图
 * 返回值：输入合法并成功生成视图时返回 true
 ***************************/
bool MOMOAGENTSQUAD_BuildView(
    const AGENTPET_SNAPSHOT *pSnapshot,
    const MOMO_AGENT_SQUAD_IDENTITY *pPreviousIdentity,
    MOMO_AGENT_SQUAD_VIEW *pView)
{
    uint8_t aSortedIndices[AGENTPET_MAX_SESSION_COUNT];
    uint8_t ucIndex;
    uint8_t ucInsertPosition;
    uint8_t ucPreviousPosition;
    uint8_t ucVisibleIndex;
    bool bPreviousFound;

    if (NULL == pView)
    {
        return false;
    }
    (void)memset(pView, 0, sizeof(*pView));
    if ((NULL == pSnapshot) ||
        (AGENTPET_MAX_SESSION_COUNT < pSnapshot->ucSessionCount) ||
        (AGENTPET_STATE_ERROR < pSnapshot->ucAggregateState))
    {
        return false;
    }
    for (ucIndex = 0U; ucIndex < pSnapshot->ucSessionCount; ucIndex++)
    {
        if (AGENTPET_STATE_ERROR < pSnapshot->aSessions[ucIndex].ucState)
        {
            (void)memset(pView, 0, sizeof(*pView));
            return false;
        }
        pView->aStateCounts[pSnapshot->aSessions[ucIndex].ucState]++;
        ucInsertPosition = ucIndex;
        while ((0U < ucInsertPosition) &&
            Local_ComesBefore(
                &pSnapshot->aSessions[ucIndex],
                &pSnapshot->aSessions[aSortedIndices[ucInsertPosition - 1U]]))
        {
            aSortedIndices[ucInsertPosition] =
                aSortedIndices[ucInsertPosition - 1U];
            ucInsertPosition--;
        }
        aSortedIndices[ucInsertPosition] = ucIndex;
    }

    pView->ucVisibleCount = pSnapshot->ucSessionCount;
    if (MOMO_AGENT_SQUAD_VISIBLE_MAX < pView->ucVisibleCount)
    {
        pView->ucVisibleCount = MOMO_AGENT_SQUAD_VISIBLE_MAX;
    }
    pView->ucHiddenCount = pSnapshot->ucSessionCount - pView->ucVisibleCount;
    for (ucIndex = 0U; ucIndex < pView->ucVisibleCount; ucIndex++)
    {
        pView->aVisibleIndices[ucIndex] = aSortedIndices[ucIndex];
    }

    bPreviousFound = false;
    ucPreviousPosition = 0U;
    for (ucVisibleIndex = 0U;
         ucVisibleIndex < pView->ucVisibleCount;
         ucVisibleIndex++)
    {
        ucIndex = pView->aVisibleIndices[ucVisibleIndex];
        if (Local_IdentityMatches(
                &pSnapshot->aSessions[ucIndex], pPreviousIdentity))
        {
            ucPreviousPosition = ucVisibleIndex;
            bPreviousFound = true;
            break;
        }
    }
    if (bPreviousFound && (0U < pView->ucVisibleCount))
    {
        const AGENTPET_SESSION *pFirstSession;
        const AGENTPET_SESSION *pPreviousSession;

        pFirstSession = &pSnapshot->aSessions[pView->aVisibleIndices[0U]];
        pPreviousSession = &pSnapshot->aSessions[
            pView->aVisibleIndices[ucPreviousPosition]];
        if (Local_StatePriority(pFirstSession->ucState) >=
            Local_StatePriority(pPreviousSession->ucState))
        {
            pView->ucSelectedPosition = ucPreviousPosition;
        }
    }

    return true;
}

/***************************
 * MOMOAGENTSQUAD_Next: 计算自动轮播的下一位置
 * 参数：
 *   - pView: 当前只读视图
 * 返回值：下一合法位置；空视图或非法视图返回0
 ***************************/
uint8_t MOMOAGENTSQUAD_Next(const MOMO_AGENT_SQUAD_VIEW *pView)
{
    if ((NULL == pView) || (0U == pView->ucVisibleCount) ||
        (MOMO_AGENT_SQUAD_VISIBLE_MAX < pView->ucVisibleCount) ||
        (pView->ucVisibleCount <= pView->ucSelectedPosition))
    {
        return 0U;
    }

    return (uint8_t)((pView->ucSelectedPosition + 1U) %
        pView->ucVisibleCount);
}

/***************************
 * MOMOAGENTSQUAD_GetSession: 取得代表位置对应的只读会话
 * 参数：
 *   - pSnapshot: 当前快照
 *   - pView: 当前视图
 *   - ucPosition: 代表项位置
 * 返回值：边界全部合法时返回会话指针，否则返回NULL
 ***************************/
const AGENTPET_SESSION *MOMOAGENTSQUAD_GetSession(
    const AGENTPET_SNAPSHOT *pSnapshot,
    const MOMO_AGENT_SQUAD_VIEW *pView,
    uint8_t ucPosition)
{
    uint8_t ucSnapshotIndex;

    if ((NULL == pSnapshot) || (NULL == pView) ||
        (AGENTPET_MAX_SESSION_COUNT < pSnapshot->ucSessionCount) ||
        (MOMO_AGENT_SQUAD_VISIBLE_MAX < pView->ucVisibleCount) ||
        (pView->ucVisibleCount <= ucPosition))
    {
        return NULL;
    }
    ucSnapshotIndex = pView->aVisibleIndices[ucPosition];
    if (pSnapshot->ucSessionCount <= ucSnapshotIndex)
    {
        return NULL;
    }

    return &pSnapshot->aSessions[ucSnapshotIndex];
}

/***************************
 * MOMOAGENTSQUAD_GetIdentity: 从会话生成本地临时身份
 * 参数：
 *   - pSession: 当前会话
 *   - pIdentity: 输出身份
 * 返回值：参数有效时返回 true
 ***************************/
bool MOMOAGENTSQUAD_GetIdentity(
    const AGENTPET_SESSION *pSession,
    MOMO_AGENT_SQUAD_IDENTITY *pIdentity)
{
    if (NULL == pIdentity)
    {
        return false;
    }
    (void)memset(pIdentity, 0, sizeof(*pIdentity));
    if (NULL == pSession)
    {
        return false;
    }
    pIdentity->ulTaskHash = pSession->ulTaskHash;
    pIdentity->ucProvider = pSession->ucProvider;
    pIdentity->ucSource = pSession->ucSource;
    pIdentity->bValid = true;

    return true;
}

/***************************
 * MOMOAGENTSQUAD_FormatSummary: 格式化固定长度五状态摘要
 * 参数：
 *   - pView: 当前视图
 *   - bConnected: BLE 是否在线
 *   - pBuffer: 输出缓冲区
 *   - ulBufferSize: 输出缓冲区大小
 * 返回值：完整写入返回 true
 ***************************/
bool MOMOAGENTSQUAD_FormatSummary(
    const MOMO_AGENT_SQUAD_VIEW *pView,
    bool bConnected,
    char *pBuffer,
    size_t ulBufferSize)
{
    int lResult;

    if ((NULL == pView) || (NULL == pBuffer) || (0U == ulBufferSize))
    {
        return false;
    }
    lResult = snprintf(
        pBuffer,
        ulBufferSize,
        "%sE%u N%u R%u D%u I%u",
        bConnected ? "" : "Off | ",
        pView->aStateCounts[AGENTPET_STATE_ERROR],
        pView->aStateCounts[AGENTPET_STATE_NEEDS_INPUT],
        pView->aStateCounts[AGENTPET_STATE_RUNNING],
        pView->aStateCounts[AGENTPET_STATE_COMPLETED],
        pView->aStateCounts[AGENTPET_STATE_IDLE]);

    return Local_FormatSucceeded(lResult, pBuffer, ulBufferSize);
}

/***************************
 * MOMOAGENTSQUAD_FormatDetail: 格式化匿名只读会话详情
 * 参数：
 *   - pSession: 当前会话
 *   - pBuffer: 输出缓冲区
 *   - ulBufferSize: 输出缓冲区大小
 * 返回值：输入合法且完整写入时返回 true
 ***************************/
bool MOMOAGENTSQUAD_FormatDetail(
    const AGENTPET_SESSION *pSession,
    char *pBuffer,
    size_t ulBufferSize)
{
    static const char *l_aProviderNames[] = {"Agent", "Codex", "Claude"};
    static const char *l_aStateNames[MOMO_AGENT_SQUAD_STATE_COUNT] =
    {
        "Idle",
        "Running",
        "Needs input",
        "Completed",
        "Error"
    };
    const char *pProvider;
    char aAge[12];
    int lAgeResult;
    int lResult;

    if ((NULL == pSession) || (NULL == pBuffer) || (0U == ulBufferSize) ||
        (AGENTPET_STATE_ERROR < pSession->ucState))
    {
        return false;
    }
    pProvider = (2U < pSession->ucProvider) ?
        l_aProviderNames[0U] : l_aProviderNames[pSession->ucProvider];
    if (MOMO_AGENT_SQUAD_UNKNOWN_AGE == pSession->usAgeSeconds)
    {
        lAgeResult = snprintf(aAge, sizeof(aAge), "unknown");
    }
    else if (MOMO_AGENT_SQUAD_SECONDS_PER_MINUTE > pSession->usAgeSeconds)
    {
        lAgeResult = snprintf(aAge, sizeof(aAge), "<1m");
    }
    else if (MOMO_AGENT_SQUAD_SECONDS_PER_HOUR > pSession->usAgeSeconds)
    {
        lAgeResult = snprintf(
            aAge,
            sizeof(aAge),
            "%um",
            (unsigned int)(pSession->usAgeSeconds /
                MOMO_AGENT_SQUAD_SECONDS_PER_MINUTE));
    }
    else
    {
        lAgeResult = snprintf(
            aAge,
            sizeof(aAge),
            "%uh",
            (unsigned int)(pSession->usAgeSeconds /
                MOMO_AGENT_SQUAD_SECONDS_PER_HOUR));
    }
    if (!Local_FormatSucceeded(lAgeResult, aAge, sizeof(aAge)))
    {
        return false;
    }
    lResult = snprintf(
        pBuffer,
        ulBufferSize,
        "%s #%04lX %s%s %s",
        pProvider,
        (unsigned long)(pSession->ulTaskHash & 0xFFFFUL),
        l_aStateNames[pSession->ucState],
        (0U != (pSession->ucFlags & AGENTPET_TASK_FLAG_APPROVAL)) ?
            " !" : "",
        aAge);

    return Local_FormatSucceeded(lResult, pBuffer, ulBufferSize);
}
