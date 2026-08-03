#ifndef AGENT_PET_PROTOCOL_H
#define AGENT_PET_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AGENTPET_FRAME_SIZE              (20U)
#define AGENTPET_FRAME_PAYLOAD_SIZE      (10U)
#define AGENTPET_MAX_CHUNK_COUNT         (13U)
#define AGENTPET_MAX_SESSION_COUNT       (12U)
#define AGENTPET_TASK_FLAG_APPROVAL      (0x01U)
#define AGENTPET_TASK_FLAG_ACTIVE        (0x02U)

typedef enum _AGENTPET_STATE
{
    AGENTPET_STATE_IDLE = 0,
    AGENTPET_STATE_RUNNING = 1,
    AGENTPET_STATE_NEEDS_INPUT = 2,
    AGENTPET_STATE_COMPLETED = 3,
    AGENTPET_STATE_ERROR = 4
} AGENTPET_STATE;

typedef enum _AGENTPET_RESULT
{
    AGENTPET_RESULT_FRAME_ACCEPTED = 0,
    AGENTPET_RESULT_SNAPSHOT_PUBLISHED = 1,
    AGENTPET_RESULT_DUPLICATE = 2,
    AGENTPET_RESULT_EVENT_PUBLISHED = 3,
    AGENTPET_ERROR_INVALID_PARAMETER = 100,
    AGENTPET_ERROR_FRAME_LENGTH = 101,
    AGENTPET_ERROR_HEADER = 102,
    AGENTPET_ERROR_CRC = 103,
    AGENTPET_ERROR_CHUNK = 104,
    AGENTPET_ERROR_PADDING = 105,
    AGENTPET_ERROR_SNAPSHOT = 106,
    AGENTPET_ERROR_EVENT = 107
} AGENTPET_RESULT;

/* AGENTPET_SESSION: Agent 会话的固定资源状态记录。
 * 成员说明：
 *   - ucState: 会话状态，范围 0~4
 *   - ucProvider: Agent 类型，范围 0~2
 *   - ucSource: Agent 来源，范围 0~3
 *   - ucFlags: bit0 待授权、bit1 聚合活动会话，其余位保留
 *   - ulTaskHash: 会话 ID 的 FNV-1a 32 位摘要，仅用于显示区分
 *   - usAgeSeconds: 最后更新时间差，65535 表示未知或溢出
 */
typedef struct _AGENTPET_SESSION
{
    uint8_t ucState;
    uint8_t ucProvider;
    uint8_t ucSource;
    uint8_t ucFlags;
    uint32_t ulTaskHash;
    uint16_t usAgeSeconds;
} AGENTPET_SESSION;

/* AGENTPET_SNAPSHOT: 一次完整且已校验的 Agent 状态快照。
 * 成员说明：
 *   - ucAggregateState: 聚合状态，范围 0~4
 *   - ucSessionCount: 有效会话数，范围 0~12
 *   - ulGeneratedAt: 桌面端生成快照时的 Unix 秒时间戳
 *   - usSequence: 协议快照序号
 *   - aSessions: 固定容量会话记录，只有前 ucSessionCount 项有效
 */
typedef struct _AGENTPET_SNAPSHOT
{
    uint8_t ucAggregateState;
    uint8_t ucSessionCount;
    uint32_t ulGeneratedAt;
    uint16_t usSequence;
    AGENTPET_SESSION aSessions[AGENTPET_MAX_SESSION_COUNT];
} AGENTPET_SNAPSHOT;

void AGENTPET_ProtocolInit(void);
void AGENTPET_ResetAssembly(void);
uint8_t AGENTPET_Crc8Atm(const uint8_t *pData, size_t ulLength);
AGENTPET_RESULT AGENTPET_ProcessFrame(const uint8_t *pFrame, size_t ulLength);
bool AGENTPET_GetSnapshot(AGENTPET_SNAPSHOT *pSnapshot, uint32_t *pGeneration);
bool AGENTPET_GetWoodenFishEvent(uint16_t *pSequence, uint32_t *pGeneration);

#endif /* AGENT_PET_PROTOCOL_H */
