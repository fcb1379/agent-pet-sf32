#include "agent_pet_ble_service.h"

#include <rtthread.h>

#include "bf0_sibles.h"
#include "bf0_sibles_internal.h"

#define LOG_TAG "agent_pet_ble"
#include "log.h"

#define AGENTPET_UUID_16_LE(x) \
    { ((uint8_t)((x) & 0xFFU)), ((uint8_t)((x) >> 8U)) }

#define AGENTPET_SERVICE_UUID_BYTES \
{ \
    0x00U, 0x10U, 0x0BU, 0x1AU, \
    0x2FU, 0x3EU, 0x9DU, 0x8CU, \
    0x5CU, 0x4FU, 0x5FU, 0x6BU, \
    0x01U, 0x00U, 0x1EU, 0x7AU \
}

#define AGENTPET_STATUS_UUID_BYTES \
{ \
    0x00U, 0x10U, 0x0BU, 0x1AU, \
    0x2FU, 0x3EU, 0x9DU, 0x8CU, \
    0x5CU, 0x4FU, 0x5FU, 0x6BU, \
    0x02U, 0x00U, 0x1EU, 0x7AU \
}
#define AGENTPET_IMAGE_UUID_BYTES \
{ \
    0x00U, 0x10U, 0x0BU, 0x1AU, \
    0x2FU, 0x3EU, 0x9DU, 0x8CU, \
    0x5CU, 0x4FU, 0x5FU, 0x6BU, \
    0x03U, 0x00U, 0x1EU, 0x7AU \
}

enum AGENTPET_ATT_INDEX
{
    AGENTPET_ATT_SERVICE = 0,
    AGENTPET_ATT_STATUS_CHAR,
    AGENTPET_ATT_STATUS_VALUE,
    AGENTPET_ATT_IMAGE_CHAR,
    AGENTPET_ATT_IMAGE_VALUE,
    AGENTPET_ATT_COUNT
};

/* Agent Pet 主服务 UUID，固定 128 位 BLE 小端序数组，仅用于 GATT 注册。 */
static uint8_t l_aAgentPetServiceUuid[ATT_UUID_128_LEN] =
    AGENTPET_SERVICE_UUID_BYTES;
/* Agent Pet 服务句柄。0 表示尚未注册，用于阻止同一蓝牙生命周期重复注册。 */
static sibles_hdl l_tAgentPetServiceHandle;
/* Agent Pet BLE 链路状态。只在短临界区内读写，保存连接和诊断计数。 */
static AGENTPET_BLE_STATUS l_tAgentPetBleStatus;

BLE_GATT_SERVICE_DEFINE_128(l_tAgentPetAttributeDatabase)
{
    BLE_GATT_SERVICE_DECLARE(
        AGENTPET_ATT_SERVICE,
        AGENTPET_UUID_16_LE(0x2800U),
        BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_DECLARE(
        AGENTPET_ATT_STATUS_CHAR,
        AGENTPET_UUID_16_LE(0x2803U),
        BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(
        AGENTPET_ATT_STATUS_VALUE,
        AGENTPET_STATUS_UUID_BYTES,
        BLE_GATT_PERM_WRITE_REQ_ENABLE |
        BLE_GATT_PERM_WRITE_COMMAND_ENABLE,
        BLE_GATT_VALUE_PERM_UUID_128 |
        BLE_GATT_VALUE_PERM_RI_ENABLE,
        AGENTPET_FRAME_SIZE),    BLE_GATT_CHAR_DECLARE(
        AGENTPET_ATT_IMAGE_CHAR,
        AGENTPET_UUID_16_LE(0x2803U),
        BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(
        AGENTPET_ATT_IMAGE_VALUE,
        AGENTPET_IMAGE_UUID_BYTES,
        BLE_GATT_PERM_WRITE_REQ_ENABLE |
        BLE_GATT_PERM_WRITE_COMMAND_ENABLE,
        BLE_GATT_VALUE_PERM_UUID_128 |
        BLE_GATT_VALUE_PERM_RI_ENABLE,
        AGENTPET_IMAGE_MAX_PACKET_SIZE),
};

static uint8_t *Local_GattReadCallback(
    uint8_t ucConnectionIndex,
    uint8_t ucAttributeIndex,
    uint16_t *pLength)
{
    (void)ucConnectionIndex;
    (void)ucAttributeIndex;
    if (NULL != pLength)
    {
        *pLength = 0U;
    }

    return NULL;
}

static uint8_t Local_GattWriteCallback(
    uint8_t ucConnectionIndex,
    sibles_set_cbk_t *pParameter)
{
    AGENTPET_RESULT eResult;
    bool bQueued;

    (void)ucConnectionIndex;
    if ((NULL == pParameter) || (NULL == pParameter->value))
    {
        return 0U;
    }

    if (AGENTPET_ATT_STATUS_VALUE == pParameter->idx)
    {
        rt_enter_critical();
        eResult = AGENTPET_ProcessFrame(pParameter->value, pParameter->len);
        if (
            (AGENTPET_RESULT_FRAME_ACCEPTED == eResult) ||
            (AGENTPET_RESULT_SNAPSHOT_PUBLISHED == eResult) ||
            (AGENTPET_RESULT_EVENT_PUBLISHED == eResult) ||
            (AGENTPET_RESULT_DUPLICATE == eResult)
        )
        {
            l_tAgentPetBleStatus.ulAcceptedFrameCount++;
        }
        else
        {
            l_tAgentPetBleStatus.ulRejectedFrameCount++;
        }
        rt_exit_critical();
        if (AGENTPET_RESULT_EVENT_PUBLISHED == eResult)
        {
            LOG_I("Wooden fish event accepted sequence=%u",
                  pParameter->value[4] | ((uint16_t)pParameter->value[5] << 8U));
        }
    }
    else if (AGENTPET_ATT_IMAGE_VALUE == pParameter->idx)
    {
        bQueued = AGENTPETIMAGE_QueueFrame(
            pParameter->value,
            pParameter->len);
        rt_enter_critical();
        if (bQueued)
        {
            l_tAgentPetBleStatus.ulAcceptedFrameCount++;
        }
        else
        {
            l_tAgentPetBleStatus.ulRejectedFrameCount++;
        }
        rt_exit_critical();
        if (!bQueued)
        {
            LOG_E("Custom mascot frame queue full or invalid");
            return 1U;
        }
    }

    return 0U;
}
/*
 * AGENTPETBLE_Init
 * 功能：初始化 BLE 服务状态和纯 C 协议重组器。
 * 参数：无。
 * 返回值：无。
 */
void AGENTPETBLE_Init(void)
{
    rt_enter_critical();
    (void)rt_memset(&l_tAgentPetBleStatus, 0, sizeof(l_tAgentPetBleStatus));
    AGENTPET_ProtocolInit();
    rt_exit_critical();
    AGENTPETIMAGE_Init();
    l_tAgentPetServiceHandle = 0U;

    return;
}

/*
 * AGENTPETBLE_RegisterService
 * 功能：在 Sibles 中注册 Agent Pet v1.0 主服务和只写状态特征。
 * 参数：无。
 * 返回值：注册成功或已经注册返回 true，失败返回 false。
 */
bool AGENTPETBLE_RegisterService(void)
{
    BLE_GATT_SERVICE_INIT_128(
        tService,
        l_tAgentPetAttributeDatabase,
        AGENTPET_ATT_COUNT,
        BLE_GATT_SERVICE_PERM_NOAUTH |
        BLE_GATT_SERVICE_PERM_UUID_128 |
        BLE_GATT_SERVICE_PERM_MULTI_LINK,
        l_aAgentPetServiceUuid);

    if (0U != l_tAgentPetServiceHandle)
    {
        return true;
    }

    l_tAgentPetServiceHandle = sibles_register_svc_128(&tService);
    if (0U == l_tAgentPetServiceHandle)
    {
        LOG_E("Agent Pet GATT service registration failed");
        return false;
    }

    sibles_register_cbk(
        l_tAgentPetServiceHandle,
        Local_GattReadCallback,
        Local_GattWriteCallback);
    LOG_I("Agent Pet GATT service registered");

    return true;
}

/*
 * AGENTPETBLE_SetConnected
 * 功能：更新 BLE 连接状态；断线时丢弃未完成快照但保留最后有效状态。
 * 参数：
 *   - bConnected: true 表示已连接，false 表示已断开。
 * 返回值：无。
 */
void AGENTPETBLE_SetConnected(bool bConnected)
{
    rt_enter_critical();
    l_tAgentPetBleStatus.bConnected = bConnected;
    if (!bConnected)
    {
        AGENTPET_ResetAssembly();
    }
    rt_exit_critical();
    if (!bConnected)
    {
        AGENTPETIMAGE_ResetTransfer();
    }

    return;
}

/*
 * AGENTPETBLE_GetStatus
 * 功能：为 LVGL 或诊断命令复制连接状态和最近有效快照。
 * 参数：
 *   - pStatus: 输出状态，不能为空。
 * 返回值：参数有效返回 true，否则返回 false。
 */
bool AGENTPETBLE_GetStatus(AGENTPET_BLE_STATUS *pStatus)
{
    AGENTPET_SNAPSHOT tSnapshot;
    uint32_t ulGeneration;
    bool bHasSnapshot;
    bool bHasWoodenFishEvent;
    uint16_t usWoodenFishSequence;
    uint32_t ulWoodenFishGeneration;

    if (NULL == pStatus)
    {
        return false;
    }

    rt_enter_critical();
    (void)rt_memcpy(pStatus, &l_tAgentPetBleStatus, sizeof(*pStatus));
    bHasSnapshot = AGENTPET_GetSnapshot(&tSnapshot, &ulGeneration);
    bHasWoodenFishEvent = AGENTPET_GetWoodenFishEvent(
        &usWoodenFishSequence,
        &ulWoodenFishGeneration);
    if (bHasSnapshot)
    {
        pStatus->bHasSnapshot = true;
        pStatus->ulGeneration = ulGeneration;
        (void)rt_memcpy(&pStatus->tSnapshot, &tSnapshot, sizeof(tSnapshot));
    }
    if (bHasWoodenFishEvent)
    {
        pStatus->bHasWoodenFishEvent = true;
        pStatus->usWoodenFishSequence = usWoodenFishSequence;
        pStatus->ulWoodenFishGeneration = ulWoodenFishGeneration;
    }
    rt_exit_critical();
    (void)AGENTPETIMAGE_GetStatus(&pStatus->tImageStatus);

    return true;
}
