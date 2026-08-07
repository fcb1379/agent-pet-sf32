#ifndef AGENT_QUEST_GARDEN_H
#define AGENT_QUEST_GARDEN_H

#include <stdbool.h>
#include <stdint.h>

#include "agent_pet_protocol.h"

#define QUEST_GARDEN_PERSIST_VERSION (1U)
#define QUEST_GARDEN_MAX_PENDING (8U)
#define QUEST_GARDEN_MAX_LEAVES (5U)
#define QUEST_GARDEN_MAX_TASKS (AGENTPET_MAX_SESSION_COUNT)

/* QUEST_GARDEN_PERSISTED: 花园的版本化持久化聚合数据。
 * 成员说明：
 *   - ulVersion: 布局版本，仅 QUEST_GARDEN_PERSIST_VERSION 有效
 *   - ulDay: UTC epoch day，0 表示 RTC 日期未知
 *   - ulTodayCompleted: 当日检测到的完成转换数，饱和于 UINT32_MAX
 *   - ulTodayCollected: 当日领取数，饱和于 UINT32_MAX
 *   - ulPending: 待领取种子数，范围 0~QUEST_GARDEN_MAX_PENDING
 *   - ulStreak: 连续有领取成果的结算天数，饱和于 UINT32_MAX
 *   - ulOverflow: 待领取队列满后的诊断计数，饱和于 UINT32_MAX
 */
typedef struct _QUEST_GARDEN_PERSISTED
{
    uint32_t ulVersion;
    uint32_t ulDay;
    uint32_t ulTodayCompleted;
    uint32_t ulTodayCollected;
    uint32_t ulPending;
    uint32_t ulStreak;
    uint32_t ulOverflow;
} QUEST_GARDEN_PERSISTED;

/* QUEST_GARDEN_TASK_SLOT: 固定容量任务状态槽，用于短期完成转换去重。
 * 成员说明：
 *   - ulTaskHash: Agent 协议任务摘要，仅用于本地短期去重
 *   - ucState: 最近一次有效 Agent 状态，范围 0~AGENTPET_STATE_ERROR
 *   - bOccupied: 槽是否已建立任务基线
 */
typedef struct _QUEST_GARDEN_TASK_SLOT
{
    uint32_t ulTaskHash;
    uint8_t ucState;
    bool bOccupied;
} QUEST_GARDEN_TASK_SLOT;

/* QUEST_GARDEN: 固定内存花园状态机实例。
 * 成员说明：
 *   - tPersisted: 可持久化聚合数据
 *   - aTaskSlots: 最多 12 个任务的短期去重槽
 *   - ucReplacementIndex: 槽满后的固定轮换替换位置
 *   - bSnapshotBaselineReady: 是否已处理冷启动首个有效快照
 */
typedef struct _QUEST_GARDEN
{
    QUEST_GARDEN_PERSISTED tPersisted;
    QUEST_GARDEN_TASK_SLOT aTaskSlots[QUEST_GARDEN_MAX_TASKS];
    uint8_t ucReplacementIndex;
    bool bSnapshotBaselineReady;
} QUEST_GARDEN;

/* QUEST_GARDEN_RESULT: 一次状态机操作的有界结果。
 * 成员说明：
 *   - ucNewSeedCount: 本次新增种子数，范围 0~8
 *   - bBaselineCreated: 本次是否只建立了冷启动快照基线
 *   - bChanged: 聚合状态是否改变，需要刷新 UI
 *   - bSaveRequired: 聚合状态是否改变，需要离散保存
 */
typedef struct _QUEST_GARDEN_RESULT
{
    uint8_t ucNewSeedCount;
    bool bBaselineCreated;
    bool bChanged;
    bool bSaveRequired;
} QUEST_GARDEN_RESULT;

/* QUEST_GARDEN_VIEW: 提供给 UI 的只读聚合视图。
 * 成员说明：
 *   - ulTodayCompleted: 当日完成转换数
 *   - ulTodayCollected: 当日领取数
 *   - ulPending: 待领取种子数，范围 0~8
 *   - ulStreak: 连续成果天数
 *   - ulOverflow: 队列溢出诊断计数
 *   - ucVisibleLeaves: 可见叶片数，范围 0~5
 *   - bFlowerVisible: 当日领取至少 5 颗时为 true
 */
typedef struct _QUEST_GARDEN_VIEW
{
    uint32_t ulTodayCompleted;
    uint32_t ulTodayCollected;
    uint32_t ulPending;
    uint32_t ulStreak;
    uint32_t ulOverflow;
    uint8_t ucVisibleLeaves;
    bool bFlowerVisible;
} QUEST_GARDEN_VIEW;

bool QUESTGARDEN_Init(QUEST_GARDEN *pGarden,
                      const QUEST_GARDEN_PERSISTED *pPersisted);
bool QUESTGARDEN_Rollover(QUEST_GARDEN *pGarden, uint32_t ulCurrentDay,
                          QUEST_GARDEN_RESULT *pResult);
bool QUESTGARDEN_ProcessSnapshot(QUEST_GARDEN *pGarden,
                                 uint32_t ulCurrentDay,
                                 const AGENTPET_SNAPSHOT *pSnapshot,
                                 QUEST_GARDEN_RESULT *pResult);
bool QUESTGARDEN_Collect(QUEST_GARDEN *pGarden,
                         QUEST_GARDEN_RESULT *pResult);
bool QUESTGARDEN_GetView(const QUEST_GARDEN *pGarden,
                         QUEST_GARDEN_VIEW *pView);
bool QUESTGARDEN_GetPersisted(const QUEST_GARDEN *pGarden,
                              QUEST_GARDEN_PERSISTED *pPersisted);

#endif /* AGENT_QUEST_GARDEN_H */
