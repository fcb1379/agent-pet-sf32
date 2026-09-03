#ifndef MOMO_AGENT_SQUAD_H
#define MOMO_AGENT_SQUAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agent_pet_protocol.h"

#define MOMO_AGENT_SQUAD_VISIBLE_MAX (5U)
#define MOMO_AGENT_SQUAD_STATE_COUNT (5U)

/* MOMO_AGENT_SQUAD_IDENTITY: 仅用于刷新时保持本地只读选择。
 * 成员说明：
 *   - ulTaskHash: 任务短摘要，不作为安全身份
 *   - ucProvider: Agent 提供方
 *   - ucSource: Agent 状态来源
 *   - bValid: 该临时身份是否有效
 */
typedef struct _MOMO_AGENT_SQUAD_IDENTITY
{
    uint32_t ulTaskHash;
    uint8_t ucProvider;
    uint8_t ucSource;
    bool bValid;
} MOMO_AGENT_SQUAD_IDENTITY;

/* MOMO_AGENT_SQUAD_VIEW: 固定容量的小队状态只读视图。
 * 成员说明：
 *   - aStateCounts: IDLE 到 ERROR 五种状态的计数
 *   - aVisibleIndices: 排序后最多五个代表会话的原快照索引
 *   - ucVisibleCount: 可见代表会话数量，范围0~5
 *   - ucHiddenCount: 未显示会话数量，范围0~7
 *   - ucSelectedPosition: 当前自动轮播位置，范围0~ucVisibleCount-1
 */
typedef struct _MOMO_AGENT_SQUAD_VIEW
{
    uint8_t aStateCounts[MOMO_AGENT_SQUAD_STATE_COUNT];
    uint8_t aVisibleIndices[MOMO_AGENT_SQUAD_VISIBLE_MAX];
    uint8_t ucVisibleCount;
    uint8_t ucHiddenCount;
    uint8_t ucSelectedPosition;
} MOMO_AGENT_SQUAD_VIEW;

bool MOMOAGENTSQUAD_BuildView(
    const AGENTPET_SNAPSHOT *pSnapshot,
    const MOMO_AGENT_SQUAD_IDENTITY *pPreviousIdentity,
    MOMO_AGENT_SQUAD_VIEW *pView);
uint8_t MOMOAGENTSQUAD_Next(const MOMO_AGENT_SQUAD_VIEW *pView);
const AGENTPET_SESSION *MOMOAGENTSQUAD_GetSession(
    const AGENTPET_SNAPSHOT *pSnapshot,
    const MOMO_AGENT_SQUAD_VIEW *pView,
    uint8_t ucPosition);
bool MOMOAGENTSQUAD_GetIdentity(
    const AGENTPET_SESSION *pSession,
    MOMO_AGENT_SQUAD_IDENTITY *pIdentity);
bool MOMOAGENTSQUAD_FormatSummary(
    const MOMO_AGENT_SQUAD_VIEW *pView,
    bool bConnected,
    char *pBuffer,
    size_t ulBufferSize);
bool MOMOAGENTSQUAD_FormatDetail(
    const AGENTPET_SESSION *pSession,
    char *pBuffer,
    size_t ulBufferSize);

#endif /* MOMO_AGENT_SQUAD_H */
