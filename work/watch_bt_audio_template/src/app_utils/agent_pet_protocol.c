#include "agent_pet_protocol.h"

#include <string.h>

#define AGENTPET_MAGIC_FIRST             (0x41U)
#define AGENTPET_MAGIC_SECOND            (0x50U)
#define AGENTPET_PROTOCOL_VERSION        (1U)
#define AGENTPET_MESSAGE_TYPE_SNAPSHOT   (1U)
#define AGENTPET_MESSAGE_TYPE_WOODEN_FISH (2U)
#define AGENTPET_MESSAGE_TYPE_TIME_SYNC  (3U)
#define AGENTPET_WOODEN_FISH_ACTION      (1U)
#define AGENTPET_WOODEN_FISH_PAYLOAD_SIZE (1U)
#define AGENTPET_TIME_SYNC_PAYLOAD_SIZE  (6U)
#define AGENTPET_TIME_MIN                (1577836800UL)
#define AGENTPET_TIME_MAX                (2145916800UL)
#define AGENTPET_TIMEZONE_MIN            (-840)
#define AGENTPET_TIMEZONE_MAX            (840)
#define AGENTPET_FRAME_HEADER_SIZE       (9U)
#define AGENTPET_FRAME_CRC_OFFSET        (19U)
#define AGENTPET_MAX_SNAPSHOT_SIZE       (126U)
#define AGENTPET_SNAPSHOT_HEADER_SIZE    (6U)
#define AGENTPET_SESSION_SIZE            (10U)
#define AGENTPET_MAX_STATE               (4U)
#define AGENTPET_MAX_PROVIDER            (2U)
#define AGENTPET_MAX_SOURCE              (3U)

/* AGENTPET_ASSEMBLY: 当前分片重组上下文，只使用静态缓冲区。
 * 成员说明：
 *   - bActive: 当前是否正在重组快照
 *   - usSequence: 当前快照序号
 *   - ucChunkCount: 当前快照总分片数
 *   - usReceivedMask: 已接收分片位图
 *   - aChunkLength: 各分片有效载荷长度
 *   - aPayload: 最大 126 字节的重组缓冲区
 */
typedef struct _AGENTPET_ASSEMBLY
{
    bool bActive;
    uint16_t usSequence;
    uint8_t ucMessageType;
    uint8_t ucChunkCount;
    uint16_t usReceivedMask;
    uint8_t aChunkLength[AGENTPET_MAX_CHUNK_COUNT];
    uint8_t aPayload[AGENTPET_MAX_SNAPSHOT_SIZE];
} AGENTPET_ASSEMBLY;

/* 模块内部重组状态。取值由有效协议帧驱动，用于有界分片重组；调用方需在多线程访问时提供临界区保护。 */
static AGENTPET_ASSEMBLY l_tAssembly;
/* 最近一次通过完整校验的快照。最多包含 12 个会话，校验失败时保持不变。 */
static AGENTPET_SNAPSHOT l_tPublishedSnapshot;
/* 快照发布代数。范围 0~4294967295，递增用于 UI 判断是否需要刷新。 */
static uint32_t l_ulGeneration;
/* 已发布快照标志。false 表示设备启动后尚未收到完整有效快照。 */
static bool l_bHasSnapshot;
/* 最近一次已发布的协议序号。用于忽略重复发送的同一完整快照。 */
static uint16_t l_usPublishedSequence;
/* 最近一次木鱼事件序号，仅用于拒绝 BLE 重传造成的重复动画。 */
static uint16_t l_usWoodenFishSequence;
/* 木鱼事件发布代数，范围 0~4294967295，供 LVGL 线程检测新点击。 */
static uint32_t l_ulWoodenFishGeneration;
/* 木鱼事件有效标志，false 表示设备启动后尚未收到上位机木鱼事件。 */
static bool l_bHasWoodenFishEvent;
/* Latest validated time synchronization payload; protected by the caller's critical section. */
static AGENTPET_TIME_SYNC l_tTimeSync;
/* Time synchronization publication generation, range 0~4294967295. */
static uint32_t l_ulTimeSyncGeneration;
/* True after at least one valid time synchronization frame has been received. */
static bool l_bHasTimeSync;

static uint16_t Local_ReadLe16(const uint8_t *pData)
{
    uint16_t usValue;

    usValue = (uint16_t)pData[0];
    usValue |= (uint16_t)((uint16_t)pData[1] << 8U);

    return usValue;
}

static uint32_t Local_ReadLe32(const uint8_t *pData)
{
    uint32_t ulValue;

    ulValue = (uint32_t)pData[0];
    ulValue |= (uint32_t)pData[1] << 8U;
    ulValue |= (uint32_t)pData[2] << 16U;
    ulValue |= (uint32_t)pData[3] << 24U;

    return ulValue;
}

static void Local_StartAssembly(uint16_t usSequence, uint8_t ucChunkCount)
{
    (void)memset(&l_tAssembly, 0, sizeof(l_tAssembly));
    l_tAssembly.bActive = true;
    l_tAssembly.usSequence = usSequence;
    l_tAssembly.ucChunkCount = ucChunkCount;

    return;
}

static bool Local_AllChunksReceived(void)
{
    uint16_t usExpectedMask;

    usExpectedMask = (uint16_t)((1UL << l_tAssembly.ucChunkCount) - 1UL);

    return (usExpectedMask == l_tAssembly.usReceivedMask);
}

static AGENTPET_RESULT Local_PublishSnapshot(size_t ulPayloadLength)
{
    AGENTPET_SNAPSHOT tCandidate;
    uint8_t ucSessionCount;
    uint8_t ucIndex;
    size_t ulExpectedLength;
    size_t ulOffset;

    (void)memset(&tCandidate, 0, sizeof(tCandidate));
    if (AGENTPET_SNAPSHOT_HEADER_SIZE > ulPayloadLength)
    {
        return AGENTPET_ERROR_SNAPSHOT;
    }

    tCandidate.ucAggregateState = l_tAssembly.aPayload[0];
    ucSessionCount = l_tAssembly.aPayload[1];
    if (
        (AGENTPET_MAX_STATE < tCandidate.ucAggregateState) ||
        (AGENTPET_MAX_SESSION_COUNT < ucSessionCount)
    )
    {
        return AGENTPET_ERROR_SNAPSHOT;
    }

    ulExpectedLength = AGENTPET_SNAPSHOT_HEADER_SIZE +
        ((size_t)ucSessionCount * AGENTPET_SESSION_SIZE);
    if (ulExpectedLength != ulPayloadLength)
    {
        return AGENTPET_ERROR_SNAPSHOT;
    }

    tCandidate.ucSessionCount = ucSessionCount;
    tCandidate.ulGeneratedAt = Local_ReadLe32(&l_tAssembly.aPayload[2]);
    tCandidate.usSequence = l_tAssembly.usSequence;

    for (ucIndex = 0U; ucIndex < ucSessionCount; ucIndex++)
    {
        AGENTPET_SESSION *pSession;

        ulOffset = AGENTPET_SNAPSHOT_HEADER_SIZE +
            ((size_t)ucIndex * AGENTPET_SESSION_SIZE);
        pSession = &tCandidate.aSessions[ucIndex];
        pSession->ucState = l_tAssembly.aPayload[ulOffset];
        pSession->ucProvider = l_tAssembly.aPayload[ulOffset + 1U];
        pSession->ucSource = l_tAssembly.aPayload[ulOffset + 2U];
        pSession->ucFlags = l_tAssembly.aPayload[ulOffset + 3U];
        pSession->ulTaskHash = Local_ReadLe32(&l_tAssembly.aPayload[ulOffset + 4U]);
        pSession->usAgeSeconds = Local_ReadLe16(&l_tAssembly.aPayload[ulOffset + 8U]);

        if (
            (AGENTPET_MAX_STATE < pSession->ucState) ||
            (AGENTPET_MAX_PROVIDER < pSession->ucProvider) ||
            (AGENTPET_MAX_SOURCE < pSession->ucSource)
        )
        {
            return AGENTPET_ERROR_SNAPSHOT;
        }
    }

    (void)memcpy(&l_tPublishedSnapshot, &tCandidate, sizeof(l_tPublishedSnapshot));
    l_usPublishedSequence = l_tAssembly.usSequence;
    l_bHasSnapshot = true;
    l_ulGeneration++;

    return AGENTPET_RESULT_SNAPSHOT_PUBLISHED;
}

/*
 * AGENTPET_ProtocolInit
 * 功能：初始化协议重组器和已发布快照。
 * 参数：无。
 * 返回值：无。
 */
void AGENTPET_ProtocolInit(void)
{
    (void)memset(&l_tAssembly, 0, sizeof(l_tAssembly));
    (void)memset(&l_tPublishedSnapshot, 0, sizeof(l_tPublishedSnapshot));
    l_ulGeneration = 0U;
    l_bHasSnapshot = false;
    l_usPublishedSequence = 0U;
    l_usWoodenFishSequence = 0U;
    l_ulWoodenFishGeneration = 0U;
    l_bHasWoodenFishEvent = false;
    (void)memset(&l_tTimeSync, 0, sizeof(l_tTimeSync));
    l_ulTimeSyncGeneration = 0U;
    l_bHasTimeSync = false;

    return;
}

/*
 * AGENTPET_ResetAssembly
 * 功能：连接断开或异常恢复时丢弃未完成分片，保留最近有效快照。
 * 参数：无。
 * 返回值：无。
 */
void AGENTPET_ResetAssembly(void)
{
    (void)memset(&l_tAssembly, 0, sizeof(l_tAssembly));

    return;
}

/*
 * AGENTPET_Crc8Atm
 * 功能：计算 CRC-8/ATM，参数为 poly=0x07、init=0、xorout=0。
 * 参数：
 *   - pData: 输入数据，只读。
 *   - ulLength: 输入字节数。
 * 返回值：CRC 值；空指针时返回 0。
 */
uint8_t AGENTPET_Crc8Atm(const uint8_t *pData, size_t ulLength)
{
    uint8_t ucCrc;
    size_t ulIndex;
    uint8_t ucBit;

    ucCrc = 0U;
    if (NULL == pData)
    {
        return ucCrc;
    }

    for (ulIndex = 0U; ulIndex < ulLength; ulIndex++)
    {
        ucCrc ^= pData[ulIndex];
        for (ucBit = 0U; ucBit < 8U; ucBit++)
        {
            if (0U != (ucCrc & 0x80U))
            {
                ucCrc = (uint8_t)((uint8_t)(ucCrc << 1U) ^ 0x07U);
            }
            else
            {
                ucCrc = (uint8_t)(ucCrc << 1U);
            }
        }
    }

    return ucCrc;
}

/*
 * AGENTPET_ProcessFrame
 * 功能：校验并重组一个固定 20 字节 Agent Pet v1.0 状态帧。
 * 参数：
 *   - pFrame: 输入帧，只读。
 *   - ulLength: 输入帧长度，必须为 20。
 * 返回值：接受、发布、重复或具体协议错误码。
 */
AGENTPET_RESULT AGENTPET_ProcessFrame(const uint8_t *pFrame, size_t ulLength)
{
    uint16_t usSequence;
    uint8_t ucMessageType;
    uint8_t ucChunkIndex;
    uint8_t ucChunkCount;
    uint8_t ucPayloadLength;
    uint8_t ucIndex;
    uint16_t usChunkMask;
    size_t ulPayloadOffset;
    size_t ulSnapshotLength;

    if (NULL == pFrame)
    {
        return AGENTPET_ERROR_INVALID_PARAMETER;
    }
    if (AGENTPET_FRAME_SIZE != ulLength)
    {
        return AGENTPET_ERROR_FRAME_LENGTH;
    }
    if (
        (AGENTPET_MAGIC_FIRST != pFrame[0]) ||
        (AGENTPET_MAGIC_SECOND != pFrame[1]) ||
        (AGENTPET_PROTOCOL_VERSION != pFrame[2]) ||
        ((AGENTPET_MESSAGE_TYPE_SNAPSHOT != pFrame[3]) &&
         (AGENTPET_MESSAGE_TYPE_WOODEN_FISH != pFrame[3]) &&
         (AGENTPET_MESSAGE_TYPE_TIME_SYNC != pFrame[3]))
    )
    {
        return AGENTPET_ERROR_HEADER;
    }
    if (pFrame[AGENTPET_FRAME_CRC_OFFSET] !=
        AGENTPET_Crc8Atm(pFrame, AGENTPET_FRAME_CRC_OFFSET))
    {
        return AGENTPET_ERROR_CRC;
    }

    ucMessageType = pFrame[3];
    usSequence = Local_ReadLe16(&pFrame[4]);
    ucChunkIndex = pFrame[6];
    ucChunkCount = pFrame[7];
    ucPayloadLength = pFrame[8];
    if (
        (0U == ucChunkCount) ||
        (AGENTPET_MAX_CHUNK_COUNT < ucChunkCount) ||
        (ucChunkCount <= ucChunkIndex) ||
        (0U == ucPayloadLength) ||
        (AGENTPET_FRAME_PAYLOAD_SIZE < ucPayloadLength) ||
        (((uint8_t)(ucChunkCount - 1U) != ucChunkIndex) &&
         (AGENTPET_FRAME_PAYLOAD_SIZE != ucPayloadLength))
    )
    {
        return AGENTPET_ERROR_CHUNK;
    }

    for (ucIndex = ucPayloadLength; ucIndex < AGENTPET_FRAME_PAYLOAD_SIZE; ucIndex++)
    {
        if (0U != pFrame[AGENTPET_FRAME_HEADER_SIZE + ucIndex])
        {
            return AGENTPET_ERROR_PADDING;
        }
    }

    if (AGENTPET_MESSAGE_TYPE_TIME_SYNC == ucMessageType)
    {
        int16_t sTimezoneOffsetMinutes;
        uint32_t ulUtcEpoch;

        if (
            (0U != ucChunkIndex) ||
            (1U != ucChunkCount) ||
            (AGENTPET_TIME_SYNC_PAYLOAD_SIZE != ucPayloadLength)
        )
        {
            return AGENTPET_ERROR_TIME_SYNC;
        }
        ulUtcEpoch = Local_ReadLe32(&pFrame[AGENTPET_FRAME_HEADER_SIZE]);
        sTimezoneOffsetMinutes = (int16_t)Local_ReadLe16(
            &pFrame[AGENTPET_FRAME_HEADER_SIZE + 4U]);
        if (
            (AGENTPET_TIME_MIN > ulUtcEpoch) ||
            (AGENTPET_TIME_MAX < ulUtcEpoch) ||
            (AGENTPET_TIMEZONE_MIN > sTimezoneOffsetMinutes) ||
            (AGENTPET_TIMEZONE_MAX < sTimezoneOffsetMinutes)
        )
        {
            return AGENTPET_ERROR_TIME_SYNC;
        }

        l_tTimeSync.ulUtcEpoch = ulUtcEpoch;
        l_tTimeSync.sTimezoneOffsetMinutes = sTimezoneOffsetMinutes;
        l_tTimeSync.usSequence = usSequence;
        l_ulTimeSyncGeneration++;
        l_bHasTimeSync = true;
        return AGENTPET_RESULT_TIME_SYNC_PUBLISHED;
    }
    if (AGENTPET_MESSAGE_TYPE_WOODEN_FISH == ucMessageType)
    {
        if (
            (0U != ucChunkIndex) ||
            (1U != ucChunkCount) ||
            (AGENTPET_WOODEN_FISH_PAYLOAD_SIZE != ucPayloadLength) ||
            (AGENTPET_WOODEN_FISH_ACTION != pFrame[AGENTPET_FRAME_HEADER_SIZE])
        )
        {
            return AGENTPET_ERROR_EVENT;
        }
        if (l_bHasWoodenFishEvent && (l_usWoodenFishSequence == usSequence))
        {
            return AGENTPET_RESULT_DUPLICATE;
        }
        l_usWoodenFishSequence = usSequence;
        l_ulWoodenFishGeneration++;
        l_bHasWoodenFishEvent = true;
        return AGENTPET_RESULT_EVENT_PUBLISHED;
    }

    if (l_bHasSnapshot && (l_usPublishedSequence == usSequence))
    {
        return AGENTPET_RESULT_DUPLICATE;
    }

    if ((!l_tAssembly.bActive) || (l_tAssembly.usSequence != usSequence))
    {
        Local_StartAssembly(usSequence, ucChunkCount);
    }
    else if (l_tAssembly.ucChunkCount != ucChunkCount)
    {
        AGENTPET_ResetAssembly();
        return AGENTPET_ERROR_CHUNK;
    }

    ulPayloadOffset = (size_t)ucChunkIndex * AGENTPET_FRAME_PAYLOAD_SIZE;
    (void)memcpy(
        &l_tAssembly.aPayload[ulPayloadOffset],
        &pFrame[AGENTPET_FRAME_HEADER_SIZE],
        ucPayloadLength);
    l_tAssembly.aChunkLength[ucChunkIndex] = ucPayloadLength;
    usChunkMask = (uint16_t)(1UL << ucChunkIndex);
    l_tAssembly.usReceivedMask |= usChunkMask;

    if (!Local_AllChunksReceived())
    {
        return AGENTPET_RESULT_FRAME_ACCEPTED;
    }

    ulSnapshotLength =
        ((size_t)(ucChunkCount - 1U) * AGENTPET_FRAME_PAYLOAD_SIZE) +
        l_tAssembly.aChunkLength[ucChunkCount - 1U];

    return Local_PublishSnapshot(ulSnapshotLength);
}

/*
 * AGENTPET_GetSnapshot
 * 功能：复制最近一次有效快照及其发布代数。
 * 参数：
 *   - pSnapshot: 输出快照，不能为空。
 *   - pGeneration: 可选输出发布代数。
 * 返回值：已有有效快照返回 true，否则返回 false。
 */
bool AGENTPET_GetSnapshot(AGENTPET_SNAPSHOT *pSnapshot, uint32_t *pGeneration)
{
    if (NULL == pSnapshot)
    {
        return false;
    }
    if (!l_bHasSnapshot)
    {
        return false;
    }

    (void)memcpy(pSnapshot, &l_tPublishedSnapshot, sizeof(*pSnapshot));
    if (NULL != pGeneration)
    {
        *pGeneration = l_ulGeneration;
    }

    return true;
}

/*
 * AGENTPET_GetWoodenFishEvent
 * 功能：读取最近一次木鱼事件序号及事件发布代数。
 * 参数：
 *   - pSequence: 可选输出最近事件序号。
 *   - pGeneration: 输出事件发布代数，不能为 NULL。
 * 返回值：已收到至少一个木鱼事件返回 true，否则返回 false。
 */
bool AGENTPET_GetWoodenFishEvent(uint16_t *pSequence, uint32_t *pGeneration)
{
    if (NULL == pGeneration)
    {
        return false;
    }
    if (!l_bHasWoodenFishEvent)
    {
        return false;
    }

    if (NULL != pSequence)
    {
        *pSequence = l_usWoodenFishSequence;
    }
    *pGeneration = l_ulWoodenFishGeneration;

    return true;
}

/*
 * AGENTPET_GetTimeSync
 * Function: Copy the latest validated time synchronization payload.
 * Parameters:
 *   - pTimeSync: output payload; must not be NULL.
 *   - pGeneration: optional publication generation output.
 * Return: true when a valid time synchronization frame has been received.
 */
bool AGENTPET_GetTimeSync(AGENTPET_TIME_SYNC *pTimeSync, uint32_t *pGeneration)
{
    if (NULL == pTimeSync)
    {
        return false;
    }
    if (!l_bHasTimeSync)
    {
        return false;
    }

    (void)memcpy(pTimeSync, &l_tTimeSync, sizeof(*pTimeSync));
    if (NULL != pGeneration)
    {
        *pGeneration = l_ulTimeSyncGeneration;
    }

    return true;
}
