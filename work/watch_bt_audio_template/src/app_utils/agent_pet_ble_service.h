#ifndef AGENT_PET_BLE_SERVICE_H
#define AGENT_PET_BLE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "agent_pet_protocol.h"
#include "agent_pet_image_transfer.h"

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
    bool bHasWoodenFishEvent;
    uint32_t ulGeneration;
    uint32_t ulWoodenFishGeneration;
    uint16_t usWoodenFishSequence;
    uint32_t ulAcceptedFrameCount;
    uint32_t ulRejectedFrameCount;
    AGENTPET_SNAPSHOT tSnapshot;
    AGENTPET_IMAGE_STATUS tImageStatus;
} AGENTPET_BLE_STATUS;

void AGENTPETBLE_Init(void);
bool AGENTPETBLE_RegisterService(void);
void AGENTPETBLE_SetConnected(bool bConnected);
void AGENTPETBLE_SetMtu(uint16_t usMtu);
bool AGENTPETBLE_GetStatus(AGENTPET_BLE_STATUS *pStatus);
void AGENTPETBLE_NotifyMerit(void);
uint16_t AGENTPETBLE_GetAudioFrameLimit(void);
int32_t AGENTPETBLE_SendAudioNotification(const uint8_t *pFrame,
                                           uint16_t usFrameLength);

#endif /* AGENT_PET_BLE_SERVICE_H */
