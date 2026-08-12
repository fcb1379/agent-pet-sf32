#include "agent_pet_ble_service.h"

#include <rtthread.h>
#include <drivers/rtc.h>
#include <time.h>

#include "bf0_sibles.h"
#include "bf0_sibles_internal.h"
#include "watch_settings.h"
#include "agent_pet_merit.h"
#include "agent_pet_audio_upload.h"
#include "agent_pet_audio_protocol.h"

#define LOG_TAG "agent_pet_ble"
#include "log.h"

#define AGENTPET_TIME_THREAD_STACK_SIZE (2048U)
#define AGENTPET_TIME_THREAD_TIME_SLICE (10U)
#define AGENTPET_TIME_MAILBOX_DEPTH     (1U)
#define AGENTPET_DEFAULT_ATT_MTU        (23U)
#define AGENTPET_ATT_NOTIFY_OVERHEAD    (3U)

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
#define AGENTPET_MERIT_UUID_BYTES \
{ \
    0x00U, 0x10U, 0x0BU, 0x1AU, \
    0x2FU, 0x3EU, 0x9DU, 0x8CU, \
    0x5CU, 0x4FU, 0x5FU, 0x6BU, \
    0x05U, 0x00U, 0x1EU, 0x7AU \
}
#define AGENTPET_AUDIO_UUID_BYTES \
{ \
    0x00U, 0x10U, 0x0BU, 0x1AU, \
    0x2FU, 0x3EU, 0x9DU, 0x8CU, \
    0x5CU, 0x4FU, 0x5FU, 0x6BU, \
    0x06U, 0x00U, 0x1EU, 0x7AU \
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
    AGENTPET_ATT_MERIT_CHAR,
    AGENTPET_ATT_MERIT_VALUE,
    AGENTPET_ATT_MERIT_CCCD,
    AGENTPET_ATT_AUDIO_CHAR,
    AGENTPET_ATT_AUDIO_VALUE,
    AGENTPET_ATT_AUDIO_CCCD,
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
/* Read response for the date-aware daily merit synchronization frame. */
static uint8_t l_aMeritResponse[AGENTPET_MERIT_FRAME_SIZE];
/* Merit notification subscription state, valid only for the active BLE link. */
static bool l_bMeritNotifyEnabled;
/* Connection index that most recently configured the merit CCCD. */
static uint8_t l_ucMeritConnectionIndex;
/* Audio notification subscription state, valid only for the active BLE link. */
static bool l_bAudioNotifyEnabled;
/* Connection index that configured the audio CCCD. */
static uint8_t l_ucAudioConnectionIndex;
/* Negotiated ATT MTU, range 23..1024; defaults to the BLE minimum. */
static uint16_t l_usAgentPetAttMtu;
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
    BLE_GATT_CHAR_DECLARE(
        AGENTPET_ATT_MERIT_CHAR,
        AGENTPET_UUID_16_LE(0x2803U),
        BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(
        AGENTPET_ATT_MERIT_VALUE,
        AGENTPET_MERIT_UUID_BYTES,
        BLE_GATT_PERM_READ_ENABLE |
        BLE_GATT_PERM_WRITE_REQ_ENABLE |
        BLE_GATT_PERM_NOTIFY_ENABLE,
        BLE_GATT_VALUE_PERM_UUID_128 |
        BLE_GATT_VALUE_PERM_RI_ENABLE,
        AGENTPET_MERIT_FRAME_SIZE),
    BLE_GATT_DESCRIPTOR_DECLARE(
        AGENTPET_ATT_MERIT_CCCD,
        AGENTPET_UUID_16_LE(0x2902U),
        BLE_GATT_PERM_READ_ENABLE |
        BLE_GATT_PERM_WRITE_REQ_ENABLE,
        BLE_GATT_VALUE_PERM_RI_ENABLE,
        2U),
    BLE_GATT_CHAR_DECLARE(
        AGENTPET_ATT_AUDIO_CHAR,
        AGENTPET_UUID_16_LE(0x2803U),
        BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(
        AGENTPET_ATT_AUDIO_VALUE,
        AGENTPET_AUDIO_UUID_BYTES,
        BLE_GATT_PERM_NOTIFY_ENABLE |
        BLE_GATT_PERM_WRITE_REQ_ENABLE,
        BLE_GATT_VALUE_PERM_UUID_128 |
        BLE_GATT_VALUE_PERM_RI_ENABLE,
        AGENTPET_AUDIO_FRAME_MAX_SIZE),
    BLE_GATT_DESCRIPTOR_DECLARE(
        AGENTPET_ATT_AUDIO_CCCD,
        AGENTPET_UUID_16_LE(0x2902U),
        BLE_GATT_PERM_READ_ENABLE |
        BLE_GATT_PERM_WRITE_REQ_ENABLE,
        BLE_GATT_VALUE_PERM_RI_ENABLE,
        2U),
};

/*
 * Local_ReadLe32
 * Function: decode one bounded little-endian 32-bit field.
 * Parameters:
 *   - pData: pointer to at least four bytes.
 * Return: decoded value, or zero for NULL.
 */
static uint32_t Local_ReadLe32(const uint8_t *pData)
{
    if (NULL == pData)
    {
        return 0U;
    }

    return (uint32_t)pData[0] |
        ((uint32_t)pData[1] << 8U) |
        ((uint32_t)pData[2] << 16U) |
        ((uint32_t)pData[3] << 24U);
}

/*
 * Local_WriteLe32
 * Function: encode one little-endian 32-bit field.
 * Parameters:
 *   - pData: output pointer to at least four bytes.
 *   - ulValue: value to encode.
 * Return: none.
 */
static void Local_WriteLe32(uint8_t *pData, uint32_t ulValue)
{
    if (NULL != pData)
    {
        pData[0] = (uint8_t)ulValue;
        pData[1] = (uint8_t)(ulValue >> 8U);
        pData[2] = (uint8_t)(ulValue >> 16U);
        pData[3] = (uint8_t)(ulValue >> 24U);
    }

    return;
}

/*
 * Local_BuildMeritFrame
 * Function: encode the current daily merit snapshot for reads and notifications.
 * Parameters:
 *   - pFrame: output buffer of AGENTPET_MERIT_FRAME_SIZE bytes.
 * Return: true when a current snapshot was encoded.
 */
static bool Local_BuildMeritFrame(uint8_t *pFrame)
{
    AGENTPET_MERIT_SNAPSHOT tMeritSnapshot;

    if ((NULL == pFrame) || !AGENTPETMERIT_GetSnapshot(&tMeritSnapshot))
    {
        return false;
    }
    (void)rt_memset(pFrame, 0, AGENTPET_MERIT_FRAME_SIZE);
    pFrame[0] = 0x41U;
    pFrame[1] = 0x4DU;
    pFrame[2] = 1U;
    pFrame[3] = 1U;
    Local_WriteLe32(&pFrame[4], tMeritSnapshot.ulDay);
    Local_WriteLe32(&pFrame[8], tMeritSnapshot.ulCount);
    pFrame[15] = AGENTPET_Crc8Atm(pFrame, 15U);

    return true;
}

static uint8_t *Local_GattReadCallback(
    uint8_t ucConnectionIndex,
    uint8_t ucAttributeIndex,
    uint16_t *pLength)
{
    AGENTPET_IMAGE_STATUS tImageStatus;
    bool bImageAvailable;
    uint8_t ucDigestSlot;

    (void)ucConnectionIndex;
    if (NULL == pLength)
    {
        return NULL;
    }
    *pLength = 0U;
    if (AGENTPET_ATT_MERIT_VALUE == ucAttributeIndex)
    {
        if (!Local_BuildMeritFrame(l_aMeritResponse))
        {
            return NULL;
        }
        *pLength = sizeof(l_aMeritResponse);

        return l_aMeritResponse;
    }
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
    l_aImageDigestResponse[2] = 3U;
    ucDigestSlot = AGENTPETIMAGE_GetSelectedDigestSlot();
    if (!AGENTPETIMAGE_GetSlotStatus(ucDigestSlot, &tImageStatus))
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
    l_aImageDigestResponse[30] = ucDigestSlot;
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
    AGENTPETMERIT_RefreshDay();
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
    AGENTPET_AUDIO_CONTROL_COMMAND eAudioCommand;
    bool bQueued;
    rt_err_t eMailboxResult;
    uint8_t ucIndex;
    (void)ucConnectionIndex;
    if ((NULL == pParameter) || (NULL == pParameter->value))
    {
        return 0U;
    }

    eResult = AGENTPET_RESULT_FRAME_ACCEPTED;
    bQueued = false;

    if (AGENTPET_ATT_STATUS_VALUE == pParameter->idx)
    {
        rt_enter_critical();
        eResult = AGENTPET_ProcessFrame(pParameter->value, pParameter->len);
        if (
            (AGENTPET_RESULT_FRAME_ACCEPTED == eResult) ||
            (AGENTPET_RESULT_SNAPSHOT_PUBLISHED == eResult) ||
            (AGENTPET_RESULT_EVENT_PUBLISHED == eResult) ||
            (AGENTPET_RESULT_TIME_SYNC_PUBLISHED == eResult) ||
            (AGENTPET_RESULT_ANIMATION_PUBLISHED == eResult) ||
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
        if (AGENTPET_RESULT_ANIMATION_PUBLISHED == eResult)
        {
            LOG_I("Expression animation accepted action=%u slot=%u",
                  pParameter->value[9],
                  pParameter->value[10]);
        }
    }
    else if (AGENTPET_ATT_IMAGE_VALUE == pParameter->idx)
    {
        if ((AGENTPET_IMAGE_CONTROL_FRAME_SIZE == pParameter->len) &&
            (0x41U == pParameter->value[0]) &&
            (0x49U == pParameter->value[1]) &&
            (2U == pParameter->value[2]) &&
            (5U == pParameter->value[3]) &&
            (pParameter->value[19] == AGENTPET_Crc8Atm(
                pParameter->value,
                19U)))
        {
            bQueued = true;
            for (ucIndex = 5U; ucIndex < 19U; ucIndex++)
            {
                if (0U != pParameter->value[ucIndex])
                {
                    bQueued = false;
                    break;
                }
            }
            if (bQueued)
            {
                bQueued = AGENTPETIMAGE_SelectDigestSlot(pParameter->value[4]);
            }
        }
        else
        {
            bQueued = AGENTPETIMAGE_QueueFrame(
                pParameter->value,
                pParameter->len);
        }
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
            return 1U;
        }
    }
    else if (AGENTPET_ATT_MERIT_VALUE == pParameter->idx)
    {
        bQueued = (AGENTPET_MERIT_FRAME_SIZE == pParameter->len) &&
            (0x41U == pParameter->value[0]) &&
            (0x4DU == pParameter->value[1]) &&
            (1U == pParameter->value[2]) &&
            (pParameter->value[15] ==
                AGENTPET_Crc8Atm(pParameter->value, 15U)) &&
            AGENTPETMERIT_Merge(
                Local_ReadLe32(&pParameter->value[4]),
                Local_ReadLe32(&pParameter->value[8]));
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
            LOG_W("Daily merit synchronization frame rejected");
            return 1U;
        }
    }
    else if (AGENTPET_ATT_MERIT_CCCD == pParameter->idx)
    {
        rt_enter_critical();
        l_bMeritNotifyEnabled = (2U <= pParameter->len) &&
            (0U != (pParameter->value[0] & 0x01U));
        l_ucMeritConnectionIndex = ucConnectionIndex;
        rt_exit_critical();
        LOG_I("Daily merit notification %s",
              l_bMeritNotifyEnabled ? "enabled" : "disabled");
    }
    else if (AGENTPET_ATT_AUDIO_VALUE == pParameter->idx)
    {
        bQueued = AGENTPETAUDIOPROTO_ParseControl(
            pParameter->value,
            pParameter->len,
            &eAudioCommand) &&
            AGENTPETAUDIO_RequestStream(
                AGENTPET_AUDIO_CONTROL_START == eAudioCommand);
        if (!bQueued)
        {
            LOG_W("Audio control frame rejected length=%u",
                  (unsigned int)pParameter->len);
            return 1U;
        }
        LOG_I("Audio control queued command=%u",
              (unsigned int)eAudioCommand);
    }
    else if (AGENTPET_ATT_AUDIO_CCCD == pParameter->idx)
    {
        bQueued = (2U <= pParameter->len) &&
            (0U != (pParameter->value[0] & 0x01U));
        rt_enter_critical();
        l_bAudioNotifyEnabled = bQueued;
        l_ucAudioConnectionIndex = ucConnectionIndex;
        rt_exit_critical();
        AGENTPETAUDIO_SetSubscribed(bQueued);
        LOG_I("Audio notification %s",
              bQueued ? "enabled" : "disabled");
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
    AGENTPETMERIT_Init();
    AGENTPETAUDIO_Init();
    l_bMeritNotifyEnabled = false;
    l_ucMeritConnectionIndex = 0U;
    l_bAudioNotifyEnabled = false;
    l_ucAudioConnectionIndex = 0U;
    l_usAgentPetAttMtu = AGENTPET_DEFAULT_ATT_MTU;
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
        rt_enter_critical();
        l_bMeritNotifyEnabled = false;
        l_bAudioNotifyEnabled = false;
        l_usAgentPetAttMtu = AGENTPET_DEFAULT_ATT_MTU;
        rt_exit_critical();
        AGENTPETAUDIO_SetSubscribed(false);
    }

    return;
}
/*
 * AGENTPETBLE_SetMtu
 * Function: store the negotiated ATT MTU used to bound notifications.
 * Parameters: usMtu is the negotiated value, range 23..1024.
 * Return: none.
 */
void AGENTPETBLE_SetMtu(uint16_t usMtu)
{
    if (AGENTPET_DEFAULT_ATT_MTU > usMtu)
    {
        return;
    }

    rt_enter_critical();
    l_usAgentPetAttMtu = usMtu;
    rt_exit_critical();

    return;
}


/*
 * AGENTPETBLE_NotifyMerit
 * Function: notify the subscribed desktop after a device-side wooden-fish hit.
 * Parameters: none.
 * Return: none.
 */
void AGENTPETBLE_NotifyMerit(void)
{
    uint8_t aFrame[AGENTPET_MERIT_FRAME_SIZE];
    sibles_value_t tValue;
    sibles_hdl tServiceHandle;
    uint8_t ucConnectionIndex;
    bool bCanNotify;
    int32_t lResult;

    rt_enter_critical();
    bCanNotify = l_tAgentPetBleStatus.bConnected &&
        l_bMeritNotifyEnabled && (0U != l_tAgentPetServiceHandle);
    tServiceHandle = l_tAgentPetServiceHandle;
    ucConnectionIndex = l_ucMeritConnectionIndex;
    rt_exit_critical();
    if (!bCanNotify || !Local_BuildMeritFrame(aFrame))
    {
        return;
    }

    tValue.hdl = tServiceHandle;
    tValue.idx = AGENTPET_ATT_MERIT_VALUE;
    tValue.len = sizeof(aFrame);
    tValue.value = aFrame;
    lResult = sibles_write_value(ucConnectionIndex, &tValue);
    if ((int32_t)sizeof(aFrame) != lResult)
    {
        LOG_W("Daily merit notification dropped result=%ld", (long)lResult);
    }

    return;
}
/*
 * AGENTPETBLE_GetAudioFrameLimit
 * Function: return the current maximum notification value length.
 * Parameters: none.
 * Return: 20..244 bytes for a valid BLE link configuration.
 */
uint16_t AGENTPETBLE_GetAudioFrameLimit(void)
{
    uint16_t usMtu;
    uint16_t usFrameLimit;

    rt_enter_critical();
    usMtu = l_usAgentPetAttMtu;
    rt_exit_critical();
    usFrameLimit = (AGENTPET_ATT_NOTIFY_OVERHEAD < usMtu) ?
        (usMtu - AGENTPET_ATT_NOTIFY_OVERHEAD) : 0U;
    if (AGENTPET_AUDIO_FRAME_MAX_SIZE < usFrameLimit)
    {
        usFrameLimit = AGENTPET_AUDIO_FRAME_MAX_SIZE;
    }

    return usFrameLimit;
}

/*
 * AGENTPETBLE_SendAudioNotification
 * Function: copy one audio frame into an available Sibles notification slot.
 * Parameters:
 *   - pFrame: validated protocol frame.
 *   - usFrameLength: frame length within the negotiated ATT value limit.
 * Return: copied byte count, zero when TX slots are busy, or a negative error.
 */
int32_t AGENTPETBLE_SendAudioNotification(const uint8_t *pFrame,
                                          uint16_t usFrameLength)
{
    sibles_value_t tValue;
    sibles_hdl tServiceHandle;
    uint16_t usFrameLimit;
    uint8_t ucConnectionIndex;
    bool bCanNotify;

    rt_enter_critical();
    usFrameLimit = (AGENTPET_ATT_NOTIFY_OVERHEAD < l_usAgentPetAttMtu) ?
        (l_usAgentPetAttMtu - AGENTPET_ATT_NOTIFY_OVERHEAD) : 0U;
    if (AGENTPET_AUDIO_FRAME_MAX_SIZE < usFrameLimit)
    {
        usFrameLimit = AGENTPET_AUDIO_FRAME_MAX_SIZE;
    }
    bCanNotify = l_tAgentPetBleStatus.bConnected &&
        l_bAudioNotifyEnabled && (0U != l_tAgentPetServiceHandle);
    tServiceHandle = l_tAgentPetServiceHandle;
    ucConnectionIndex = l_ucAudioConnectionIndex;
    rt_exit_critical();
    if ((NULL == pFrame) || (0U == usFrameLength) ||
        (usFrameLimit < usFrameLength))
    {
        return -RT_EINVAL;
    }
    if (!bCanNotify)
    {
        return -RT_EBUSY;
    }

    tValue.hdl = tServiceHandle;
    tValue.idx = AGENTPET_ATT_AUDIO_VALUE;
    tValue.len = usFrameLength;
    tValue.value = (uint8_t *)(void *)pFrame;

    return sibles_write_value(ucConnectionIndex, &tValue);
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
