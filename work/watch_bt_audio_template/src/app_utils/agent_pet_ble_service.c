#include "agent_pet_ble_service.h"

#include <rtthread.h>
#include <drivers/rtc.h>
#include <time.h>

#include "bf0_sibles.h"
#include "bf0_sibles_internal.h"
#include "watch_settings.h"

#define LOG_TAG "agent_pet_ble"
#include "log.h"

#define AGENTPET_TIME_THREAD_STACK_SIZE (2048U)
#define AGENTPET_TIME_THREAD_TIME_SLICE (10U)
#define AGENTPET_TIME_MAILBOX_DEPTH     (1U)

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
#define AGENTPET_IMAGE_DIGEST_UUID_BYTES \
{ \
    0x00U, 0x10U, 0x0BU, 0x1AU, \
    0x2FU, 0x3EU, 0x9DU, 0x8CU, \
    0x5CU, 0x4FU, 0x5FU, 0x6BU, \
    0x04U, 0x00U, 0x1EU, 0x7AU \
}

enum AGENTPET_ATT_INDEX
{
    AGENTPET_ATT_SERVICE = 0,
    AGENTPET_ATT_STATUS_CHAR,
    AGENTPET_ATT_STATUS_VALUE,
    AGENTPET_ATT_IMAGE_CHAR,
    AGENTPET_ATT_IMAGE_VALUE,
    AGENTPET_ATT_IMAGE_DIGEST_CHAR,
    AGENTPET_ATT_IMAGE_DIGEST_VALUE,
    AGENTPET_ATT_COUNT
};

/* Agent Pet 主服务 UUID，固定 128 位 BLE 小端序数组，仅用于 GATT 注册。 */
static uint8_t l_aAgentPetServiceUuid[ATT_UUID_128_LEN] =
    AGENTPET_SERVICE_UUID_BYTES;
/* Agent Pet 服务句柄。0 表示尚未注册，用于阻止同一蓝牙生命周期重复注册。 */
static sibles_hdl l_tAgentPetServiceHandle;
/* Agent Pet BLE 链路状态。只在短临界区内读写，保存连接和诊断计数。 */
static AGENTPET_BLE_STATUS l_tAgentPetBleStatus;
/* Read response: magic/version/availability followed by the persistent JPEG MD5. */
static uint8_t l_aImageDigestResponse[AGENTPET_IMAGE_DIGEST_FRAME_SIZE];
/* Static mailbox used only as a non-blocking wake signal; time data stays in the protocol state. */
static struct rt_mailbox l_tTimeSyncMailbox;
/* One-entry mailbox pool; additional updates coalesce into the latest protocol generation. */
static rt_ubase_t l_aTimeSyncMailboxPool[AGENTPET_TIME_MAILBOX_DEPTH];
/* Static worker thread that performs RTC and settings writes outside the GATT callback. */
static struct rt_thread l_tTimeSyncThread;
/* Fixed worker stack, 2048 bytes, used for time conversion and settings persistence. */
static uint8_t l_aTimeSyncThreadStack[AGENTPET_TIME_THREAD_STACK_SIZE];
/* True after the static mailbox and worker thread have started successfully. */
static bool l_bTimeSyncWorkerReady;

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
    BLE_GATT_CHAR_DECLARE(
        AGENTPET_ATT_IMAGE_DIGEST_CHAR,
        AGENTPET_UUID_16_LE(0x2803U),
        BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(
        AGENTPET_ATT_IMAGE_DIGEST_VALUE,
        AGENTPET_IMAGE_DIGEST_UUID_BYTES,
        BLE_GATT_PERM_READ_ENABLE,
        BLE_GATT_VALUE_PERM_UUID_128 |
        BLE_GATT_VALUE_PERM_RI_ENABLE,
        AGENTPET_IMAGE_DIGEST_FRAME_SIZE),
};

static uint8_t *Local_GattReadCallback(
    uint8_t ucConnectionIndex,
    uint8_t ucAttributeIndex,
    uint16_t *pLength)
{
    AGENTPET_IMAGE_STATUS tImageStatus;
    bool bImageAvailable;

    (void)ucConnectionIndex;
    if (NULL == pLength)
    {
        return NULL;
    }
    *pLength = 0U;
    if (AGENTPET_ATT_IMAGE_DIGEST_VALUE != ucAttributeIndex)
    {
        return NULL;
    }

    (void)rt_memset(
        l_aImageDigestResponse,
        0,
        sizeof(l_aImageDigestResponse));
    (void)rt_memset(&tImageStatus, 0, sizeof(tImageStatus));
    bImageAvailable = false;
    l_aImageDigestResponse[0] = 0x41U;
    l_aImageDigestResponse[1] = 0x49U;
    l_aImageDigestResponse[2] = 2U;
    if (!AGENTPETIMAGE_GetStatus(&tImageStatus))
    {
        LOG_W("Custom mascot status snapshot unavailable");
    }
    if (!AGENTPETIMAGE_GetDigest(
            &bImageAvailable,
            &l_aImageDigestResponse[4]))
    {
        LOG_W("Custom mascot digest snapshot unavailable");
    }
    l_aImageDigestResponse[3] = bImageAvailable ? 1U : 0U;
    l_aImageDigestResponse[20] = (uint8_t)tImageStatus.ulReceived;
    l_aImageDigestResponse[21] = (uint8_t)(tImageStatus.ulReceived >> 8U);
    l_aImageDigestResponse[22] = (uint8_t)(tImageStatus.ulReceived >> 16U);
    l_aImageDigestResponse[23] = (uint8_t)(tImageStatus.ulReceived >> 24U);
    l_aImageDigestResponse[24] = (uint8_t)tImageStatus.ulTotal;
    l_aImageDigestResponse[25] = (uint8_t)(tImageStatus.ulTotal >> 8U);
    l_aImageDigestResponse[26] = (uint8_t)(tImageStatus.ulTotal >> 16U);
    l_aImageDigestResponse[27] = (uint8_t)(tImageStatus.ulTotal >> 24U);
    l_aImageDigestResponse[28] = (uint8_t)tImageStatus.eState;
    l_aImageDigestResponse[29] = (uint8_t)tImageStatus.eLastResult;
    *pLength = sizeof(l_aImageDigestResponse);

    return l_aImageDigestResponse;
}

/*
 * Local_ApplyTimeSync
 * Function: Convert a validated UTC/timezone payload to local time, update RTC, and persist metadata.
 * Parameters:
 *   - pTimeSync: validated time synchronization payload; must not be NULL.
 * Return: true when RTC update succeeds; false on conversion or RTC failure.
 */
static bool Local_ApplyTimeSync(const AGENTPET_TIME_SYNC *pTimeSync)
{
    time_t tLocalSeconds;
    struct tm tLocalTime;
    rt_err_t eDateResult;
    rt_err_t eTimeResult;
    rt_err_t eSettingsResult;

    if (NULL == pTimeSync)
    {
        return false;
    }

    tLocalSeconds = (time_t)pTimeSync->ulUtcEpoch +
        ((time_t)pTimeSync->sTimezoneOffsetMinutes * 60);
    if (NULL == gmtime_r(&tLocalSeconds, &tLocalTime))
    {
        LOG_E("Time sync conversion failed epoch=%lu tz=%d",
              (unsigned long)pTimeSync->ulUtcEpoch,
              pTimeSync->sTimezoneOffsetMinutes);
        return false;
    }

    eDateResult = set_date(
        (rt_uint32_t)tLocalTime.tm_year + 1900U,
        (rt_uint32_t)tLocalTime.tm_mon + 1U,
        (rt_uint32_t)tLocalTime.tm_mday);
    eTimeResult = set_time(
        (rt_uint32_t)tLocalTime.tm_hour,
        (rt_uint32_t)tLocalTime.tm_min,
        (rt_uint32_t)tLocalTime.tm_sec);
    if ((RT_EOK != eDateResult) || (RT_EOK != eTimeResult))
    {
        LOG_E("Time sync RTC update failed date=%d time=%d",
              eDateResult,
              eTimeResult);
        return false;
    }

    eSettingsResult = watch_settings_set_time_sync(
        pTimeSync->sTimezoneOffsetMinutes,
        pTimeSync->ulUtcEpoch);
    if (RT_EOK != eSettingsResult)
    {
        LOG_W("Time sync persistence failed result=%d", eSettingsResult);
    }
    LOG_I("Time synchronized epoch=%lu tz=%d sequence=%u",
          (unsigned long)pTimeSync->ulUtcEpoch,
          pTimeSync->sTimezoneOffsetMinutes,
          pTimeSync->usSequence);

    return true;
}

/*
 * Local_TimeSyncWorker
 * Function: Wait for bounded mailbox signals and apply the latest validated time outside BLE context.
 * Parameters:
 *   - pParameter: unused RT-Thread entry parameter.
 * Return: none; the worker runs for the device lifetime.
 */
static void Local_TimeSyncWorker(void *pParameter)
{
    AGENTPET_TIME_SYNC tTimeSync;
    uint32_t ulSignal;
    bool bHasTimeSync;

    (void)pParameter;
    while (true)
    {
        if (RT_EOK != rt_mb_recv(
                &l_tTimeSyncMailbox,
                &ulSignal,
                RT_WAITING_FOREVER))
        {
            continue;
        }
        (void)ulSignal;
        rt_enter_critical();
        bHasTimeSync = AGENTPET_GetTimeSync(&tTimeSync, NULL);
        rt_exit_critical();
        if (bHasTimeSync)
        {
            (void)Local_ApplyTimeSync(&tTimeSync);
        }
    }
}

/*
 * Local_InitTimeSyncWorker
 * Function: Initialize the static mailbox, fixed stack, and time synchronization worker once.
 * Parameters: none.
 * Return: true when the worker is ready; false when any RT-Thread initialization step fails.
 */
static bool Local_InitTimeSyncWorker(void)
{
    rt_err_t eResult;

    if (l_bTimeSyncWorkerReady)
    {
        return true;
    }

    eResult = rt_mb_init(
        &l_tTimeSyncMailbox,
        "pet_time",
        l_aTimeSyncMailboxPool,
        AGENTPET_TIME_MAILBOX_DEPTH,
        RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        LOG_E("Time sync mailbox init failed result=%d", eResult);
        return false;
    }
    eResult = rt_thread_init(
        &l_tTimeSyncThread,
        "pet_time",
        Local_TimeSyncWorker,
        NULL,
        l_aTimeSyncThreadStack,
        sizeof(l_aTimeSyncThreadStack),
        RT_THREAD_PRIORITY_MIDDLE + 3U,
        AGENTPET_TIME_THREAD_TIME_SLICE);
    if (RT_EOK != eResult)
    {
        LOG_E("Time sync worker init failed result=%d", eResult);
        (void)rt_mb_detach(&l_tTimeSyncMailbox);
        return false;
    }
    eResult = rt_thread_startup(&l_tTimeSyncThread);
    if (RT_EOK != eResult)
    {
        LOG_E("Time sync worker start failed result=%d", eResult);
        (void)rt_thread_detach(&l_tTimeSyncThread);
        (void)rt_mb_detach(&l_tTimeSyncMailbox);
        return false;
    }
    l_bTimeSyncWorkerReady = true;

    return true;
}
static uint8_t Local_GattWriteCallback(
    uint8_t ucConnectionIndex,
    sibles_set_cbk_t *pParameter)
{
    AGENTPET_RESULT eResult;
    bool bQueued;
    rt_err_t eMailboxResult;
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
            (AGENTPET_RESULT_TIME_SYNC_PUBLISHED == eResult) ||
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
        if (AGENTPET_RESULT_TIME_SYNC_PUBLISHED == eResult)
        {
            if (l_bTimeSyncWorkerReady)
            {
                eMailboxResult = rt_mb_send(&l_tTimeSyncMailbox, 1U);
                if (RT_EOK != eMailboxResult)
                {
                    LOG_D("Time sync wake coalesced result=%d", eMailboxResult);
                }
            }
            else
            {
                LOG_E("Time sync worker is unavailable");
            }
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
    (void)Local_InitTimeSyncWorker();
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
    AGENTPET_IMAGE_STATUS tImageStatus;
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
    if (AGENTPETIMAGE_GetStatus(&tImageStatus))
    {
        rt_enter_critical();
        l_tAgentPetBleStatus.tImageStatus = tImageStatus;
        pStatus->tImageStatus = tImageStatus;
        rt_exit_critical();
    }

    return true;
}
