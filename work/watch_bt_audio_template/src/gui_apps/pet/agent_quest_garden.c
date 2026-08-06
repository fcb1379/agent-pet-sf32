#include "agent_quest_garden.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#if defined(_MSC_VER)
typedef char QUEST_GARDEN_STATIC_STATE_WITHIN_BUDGET[
    (sizeof(QUEST_GARDEN) <= 192U) ? 1 : -1];
#else
_Static_assert(sizeof(QUEST_GARDEN) <= 192U,
               "Quest Garden static state exceeds the RAM budget");
#endif

/*
 * QUESTGARDEN_SaturatingIncrement
 * 功能：对 32 位无符号计数执行饱和加一，避免整数回绕。
 * 参数：
 *   - pValue: 待更新计数指针
 * 返回值：计数发生变化返回 true，否则返回 false。
 */
static bool QUESTGARDEN_SaturatingIncrement(uint32_t *pValue)
{
    if ((NULL == pValue) || (UINT32_MAX == *pValue))
    {
        return false;
    }

    (*pValue)++;
    return true;
}

/*
 * QUESTGARDEN_ClearResult
 * 功能：把一次操作结果初始化为无变化。
 * 参数：
 *   - pResult: 输出结果指针
 * 返回值：无。
 */
static void QUESTGARDEN_ClearResult(QUEST_GARDEN_RESULT *pResult)
{
    if (NULL != pResult)
    {
        (void)memset(pResult, 0, sizeof(*pResult));
    }

    return;
}

/*
 * QUESTGARDEN_FindSlot
 * 功能：在固定任务槽中查找任务摘要。
 * 参数：
 *   - pGarden: 花园状态机实例
 *   - ulTaskHash: 待查找任务摘要
 * 返回值：找到返回槽指针，否则返回 NULL。
 */
static QUEST_GARDEN_TASK_SLOT *QUESTGARDEN_FindSlot(
    QUEST_GARDEN *pGarden, uint32_t ulTaskHash)
{
    uint8_t ucIndex;

    if (NULL == pGarden)
    {
        return NULL;
    }

    for (ucIndex = 0U; ucIndex < QUEST_GARDEN_MAX_TASKS; ucIndex++)
    {
        if ((true == pGarden->aTaskSlots[ucIndex].bOccupied) &&
            (ulTaskHash == pGarden->aTaskSlots[ucIndex].ulTaskHash))
        {
            return &pGarden->aTaskSlots[ucIndex];
        }
    }

    return NULL;
}

/*
 * QUESTGARDEN_AllocateSlot
 * 功能：分配空闲任务槽，槽满时按固定顺序保守替换。
 * 参数：
 *   - pGarden: 花园状态机实例
 * 返回值：成功返回任务槽指针，参数无效返回 NULL。
 */
static QUEST_GARDEN_TASK_SLOT *QUESTGARDEN_AllocateSlot(
    QUEST_GARDEN *pGarden)
{
    uint8_t ucIndex;
    QUEST_GARDEN_TASK_SLOT *pSlot;

    if (NULL == pGarden)
    {
        return NULL;
    }

    for (ucIndex = 0U; ucIndex < QUEST_GARDEN_MAX_TASKS; ucIndex++)
    {
        if (false == pGarden->aTaskSlots[ucIndex].bOccupied)
        {
            return &pGarden->aTaskSlots[ucIndex];
        }
    }

    if (QUEST_GARDEN_MAX_TASKS <= pGarden->ucReplacementIndex)
    {
        pGarden->ucReplacementIndex = 0U;
    }
    pSlot = &pGarden->aTaskSlots[pGarden->ucReplacementIndex];
    pGarden->ucReplacementIndex++;
    if (QUEST_GARDEN_MAX_TASKS <= pGarden->ucReplacementIndex)
    {
        pGarden->ucReplacementIndex = 0U;
    }

    return pSlot;
}

/*
 * QUESTGARDEN_RecordCompletion
 * 功能：记录一次完成转换，并按固定队列容量生成待领取种子。
 * 参数：
 *   - pGarden: 花园状态机实例
 *   - pResult: 本次操作结果
 * 返回值：无。
 */
static void QUESTGARDEN_RecordCompletion(QUEST_GARDEN *pGarden,
                                          QUEST_GARDEN_RESULT *pResult)
{
    if ((NULL == pGarden) || (NULL == pResult))
    {
        return;
    }

    (void)QUESTGARDEN_SaturatingIncrement(
        &pGarden->tPersisted.ulTodayCompleted);
    if (QUEST_GARDEN_MAX_PENDING > pGarden->tPersisted.ulPending)
    {
        pGarden->tPersisted.ulPending++;
        pResult->ucNewSeedCount++;
    }
    else
    {
        (void)QUESTGARDEN_SaturatingIncrement(
            &pGarden->tPersisted.ulOverflow);
    }
    pResult->bChanged = true;
    pResult->bSaveRequired = true;

    return;
}

/*
 * QUESTGARDEN_Init
 * 功能：初始化固定内存状态机，并校验可选持久化聚合值。
 * 参数：
 *   - pGarden: 花园状态机实例
 *   - pPersisted: 可选持久化数据，仅输入
 * 返回值：初始化成功返回 true，参数无效返回 false。
 */
bool QUESTGARDEN_Init(QUEST_GARDEN *pGarden,
                      const QUEST_GARDEN_PERSISTED *pPersisted)
{
    if (NULL == pGarden)
    {
        return false;
    }

    (void)memset(pGarden, 0, sizeof(*pGarden));
    pGarden->tPersisted.ulVersion = QUEST_GARDEN_PERSIST_VERSION;
    if ((NULL != pPersisted) &&
        (QUEST_GARDEN_PERSIST_VERSION == pPersisted->ulVersion) &&
        (QUEST_GARDEN_MAX_PENDING >= pPersisted->ulPending))
    {
        pGarden->tPersisted = *pPersisted;
    }

    return true;
}

/*
 * QUESTGARDEN_Rollover
 * 功能：在可信 UTC 日期单调前进时结算昨日并清空今日计数。
 * 参数：
 *   - pGarden: 花园状态机实例
 *   - ulCurrentDay: 当前 UTC epoch day，0 表示未知
 *   - pResult: 输出结果
 * 返回值：参数有效返回 true，否则返回 false。
 */
bool QUESTGARDEN_Rollover(QUEST_GARDEN *pGarden, uint32_t ulCurrentDay,
                          QUEST_GARDEN_RESULT *pResult)
{
    uint32_t ulDayDelta;

    if ((NULL == pGarden) || (NULL == pResult))
    {
        return false;
    }

    QUESTGARDEN_ClearResult(pResult);
    if (0U == ulCurrentDay)
    {
        return true;
    }
    if (0U == pGarden->tPersisted.ulDay)
    {
        pGarden->tPersisted.ulDay = ulCurrentDay;
        pResult->bChanged = true;
        pResult->bSaveRequired = true;
        return true;
    }
    if (ulCurrentDay <= pGarden->tPersisted.ulDay)
    {
        return true;
    }

    ulDayDelta = ulCurrentDay - pGarden->tPersisted.ulDay;
    if ((1U == ulDayDelta) &&
        (0U != pGarden->tPersisted.ulTodayCollected))
    {
        (void)QUESTGARDEN_SaturatingIncrement(&pGarden->tPersisted.ulStreak);
    }
    else
    {
        pGarden->tPersisted.ulStreak = 0U;
    }
    pGarden->tPersisted.ulDay = ulCurrentDay;
    pGarden->tPersisted.ulTodayCompleted = 0U;
    pGarden->tPersisted.ulTodayCollected = 0U;
    pResult->bChanged = true;
    pResult->bSaveRequired = true;

    return true;
}

/*
 * QUESTGARDEN_ProcessSnapshot
 * 功能：处理快照中的任务状态转换，首帧仅建立冷启动基线。
 * 参数：
 *   - pGarden: 花园状态机实例
 *   - ulCurrentDay: 当前 UTC epoch day，0 表示未知
 *   - pSnapshot: 已验证 Agent Pet 快照，仅输入
 *   - pResult: 输出结果
 * 返回值：处理成功返回 true，参数或会话数无效返回 false。
 */
bool QUESTGARDEN_ProcessSnapshot(QUEST_GARDEN *pGarden,
                                 uint32_t ulCurrentDay,
                                 const AGENTPET_SNAPSHOT *pSnapshot,
                                 QUEST_GARDEN_RESULT *pResult)
{
    QUEST_GARDEN_RESULT tRolloverResult;
    QUEST_GARDEN_TASK_SLOT *pSlot;
    uint8_t ucIndex;
    bool bCreatingBaseline;

    if ((NULL == pGarden) || (NULL == pSnapshot) || (NULL == pResult) ||
        (QUEST_GARDEN_MAX_TASKS < pSnapshot->ucSessionCount))
    {
        return false;
    }

    for (ucIndex = 0U; ucIndex < pSnapshot->ucSessionCount; ucIndex++)
    {
        if (AGENTPET_STATE_ERROR < pSnapshot->aSessions[ucIndex].ucState)
        {
            return false;
        }
    }

    QUESTGARDEN_ClearResult(pResult);
    if (!QUESTGARDEN_Rollover(pGarden, ulCurrentDay, &tRolloverResult))
    {
        return false;
    }
    pResult->bChanged = tRolloverResult.bChanged;
    pResult->bSaveRequired = tRolloverResult.bSaveRequired;
    bCreatingBaseline = (false == pGarden->bSnapshotBaselineReady);

    for (ucIndex = 0U; ucIndex < pSnapshot->ucSessionCount; ucIndex++)
    {
        pSlot = QUESTGARDEN_FindSlot(
            pGarden, pSnapshot->aSessions[ucIndex].ulTaskHash);
        if (NULL == pSlot)
        {
            pSlot = QUESTGARDEN_AllocateSlot(pGarden);
            if (NULL == pSlot)
            {
                return false;
            }
            pSlot->ulTaskHash = pSnapshot->aSessions[ucIndex].ulTaskHash;
            pSlot->ucState = pSnapshot->aSessions[ucIndex].ucState;
            pSlot->bOccupied = true;
            continue;
        }

        if ((false == bCreatingBaseline) &&
            (AGENTPET_STATE_COMPLETED != pSlot->ucState) &&
            (AGENTPET_STATE_COMPLETED ==
             pSnapshot->aSessions[ucIndex].ucState))
        {
            QUESTGARDEN_RecordCompletion(pGarden, pResult);
        }
        pSlot->ucState = pSnapshot->aSessions[ucIndex].ucState;
    }

    if (true == bCreatingBaseline)
    {
        pGarden->bSnapshotBaselineReady = true;
        pResult->bBaselineCreated = true;
    }

    return true;
}

/*
 * QUESTGARDEN_Collect
 * 功能：本地领取最多一颗待领取种子，不执行任何外部写操作。
 * 参数：
 *   - pGarden: 花园状态机实例
 *   - pResult: 输出结果
 * 返回值：参数有效返回 true，否则返回 false。
 */
bool QUESTGARDEN_Collect(QUEST_GARDEN *pGarden,
                         QUEST_GARDEN_RESULT *pResult)
{
    if ((NULL == pGarden) || (NULL == pResult))
    {
        return false;
    }

    QUESTGARDEN_ClearResult(pResult);
    if (0U == pGarden->tPersisted.ulPending)
    {
        return true;
    }

    pGarden->tPersisted.ulPending--;
    (void)QUESTGARDEN_SaturatingIncrement(
        &pGarden->tPersisted.ulTodayCollected);
    pResult->bChanged = true;
    pResult->bSaveRequired = true;

    return true;
}

/*
 * QUESTGARDEN_GetView
 * 功能：复制供 UI 使用的有界只读视图。
 * 参数：
 *   - pGarden: 花园状态机实例，仅输入
 *   - pView: 输出视图
 * 返回值：成功返回 true，参数无效返回 false。
 */
bool QUESTGARDEN_GetView(const QUEST_GARDEN *pGarden,
                         QUEST_GARDEN_VIEW *pView)
{
    if ((NULL == pGarden) || (NULL == pView))
    {
        return false;
    }

    pView->ulTodayCompleted = pGarden->tPersisted.ulTodayCompleted;
    pView->ulTodayCollected = pGarden->tPersisted.ulTodayCollected;
    pView->ulPending = pGarden->tPersisted.ulPending;
    pView->ulStreak = pGarden->tPersisted.ulStreak;
    pView->ulOverflow = pGarden->tPersisted.ulOverflow;
    pView->ucVisibleLeaves =
        (QUEST_GARDEN_MAX_LEAVES < pGarden->tPersisted.ulTodayCollected) ?
        QUEST_GARDEN_MAX_LEAVES :
        (uint8_t)pGarden->tPersisted.ulTodayCollected;
    pView->bFlowerVisible =
        (QUEST_GARDEN_MAX_LEAVES <= pGarden->tPersisted.ulTodayCollected);

    return true;
}

/*
 * QUESTGARDEN_GetPersisted
 * 功能：复制可持久化聚合状态，不暴露短期任务槽。
 * 参数：
 *   - pGarden: 花园状态机实例，仅输入
 *   - pPersisted: 输出持久化数据
 * 返回值：成功返回 true，参数无效返回 false。
 */
bool QUESTGARDEN_GetPersisted(const QUEST_GARDEN *pGarden,
                              QUEST_GARDEN_PERSISTED *pPersisted)
{
    if ((NULL == pGarden) || (NULL == pPersisted))
    {
        return false;
    }

    *pPersisted = pGarden->tPersisted;
    return true;
}
