#ifndef AGENT_PET_BLE_SERVICE_H
#define AGENT_PET_BLE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "agent_pet_protocol.h"

/* AGENTPET_BLE_STATUS: BLE 链路和最近一次有效 Agent 快照的只读副本。
 * 成员说明：
 *   - bConnected: Agent Pet BLE 链路是否已连接
 *   - bHasSnapshot: 是否已经发布过完整有效快照
 *   - ulGeneration: 快照发布代数，供 LVGL 判断刷新
 *   - ulAcceptedFrameCount: 已接受或已发布的帧数
 *   - ulRejectedFrameCount: 被协议校验拒绝的帧数
 *   - tSnapshot: 最近一次有效快照，bHasSnapshot 为 true 时有效
 */
typedef struct _AGENTPET_BLE_STATUS
{
    bool bConnected;
    bool bHasSnapshot;
    uint32_t ulGeneration;
    uint32_t ulAcceptedFrameCount;
    uint32_t ulRejectedFrameCount;
    AGENTPET_SNAPSHOT tSnapshot;
} AGENTPET_BLE_STATUS;

void AGENTPETBLE_Init(void);
bool AGENTPETBLE_RegisterService(void);
void AGENTPETBLE_SetConnected(bool bConnected);
bool AGENTPETBLE_GetStatus(AGENTPET_BLE_STATUS *pStatus);

#endif /* AGENT_PET_BLE_SERVICE_H */
