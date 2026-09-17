#include "bike_ble_gatt_client.h"

#include <stddef.h>
#include <string.h>

#include <rtthread.h>

#include "att.h"
#include "bf0_ble_common.h"
#include "bf0_ble_gap.h"
#include "bf0_sibles.h"
#include "bike_ble_measurement.h"

#define LOG_TAG "bike.gatt"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#define BIKE_BLE_GATT_SLOT_COUNT (2U)
#define BIKE_BLE_GATT_INVALID_CONN_INDEX (0xFFU)

/* BIKE_BLE_GATT_STATE: 单个远端标准服务的发现和订阅状态。 */
typedef enum _BIKE_BLE_GATT_STATE
{
    BIKE_BLE_GATT_STATE_IDLE = 0,
    BIKE_BLE_GATT_STATE_SEARCHING,
    BIKE_BLE_GATT_STATE_WAIT_SECURITY,
    BIKE_BLE_GATT_STATE_REGISTERING,
    BIKE_BLE_GATT_STATE_SUBSCRIBING,
    BIKE_BLE_GATT_STATE_READY,
    BIKE_BLE_GATT_STATE_FAILED
} BIKE_BLE_GATT_STATE;

/* BIKE_BLE_GATT_SLOT: 一个 HR 或 CSC service 的逐连接固定实例。
 * 成员说明：
 *   - eState: service discovery/register/subscribe 状态
 *   - ucServiceMask: BIKE_BLE_SERVICE_* 单个位
 *   - ucConnIndex: SDK 连接索引，空闲时为 0xFF
 *   - bRequested: 当前连接是否请求此服务
 *   - bSecuritySeen: 已收到配对或加密结果
 *   - usServiceStart/usServiceEnd: 远端服务 handle 范围
 *   - usRemoteHandle: SIBLES 远端服务注册句柄
 *   - usValueHandle/usCccdHandle: 测量值和 CCCD handle
 */
typedef struct _BIKE_BLE_GATT_SLOT
{
    BIKE_BLE_GATT_STATE eState;
    uint8_t ucServiceMask;
    uint8_t ucConnIndex;
    bool bRequested;
    bool bSecuritySeen;
    uint16_t usServiceStart;
    uint16_t usServiceEnd;
    uint16_t usRemoteHandle;
    uint16_t usValueHandle;
    uint16_t usCccdHandle;
} BIKE_BLE_GATT_SLOT;

/* l_aBikeBleGattSlots: 固定两个逐连接 service slot，分别用于 HR 和 CSC。 */
static BIKE_BLE_GATT_SLOT l_aBikeBleGattSlots[BIKE_BLE_GATT_SLOT_COUNT];

/* l_pBikeBleGattReadyCallback: service 就绪或失败回调。 */
static BIKE_BLE_GATT_READY_CALLBACK l_pBikeBleGattReadyCallback;

/* l_pBikeBleGattHeartRateCallback: 已校验心率测量回调。 */
static BIKE_BLE_GATT_HEART_RATE_CALLBACK l_pBikeBleGattHeartRateCallback;

/* l_pBikeBleGattCscCallback: 已校验 CSC 测量回调。 */
static BIKE_BLE_GATT_CSC_CALLBACK l_pBikeBleGattCscCallback;

/* l_bBikeBleGattReady: 固定 slot 和回调已经初始化。 */
static bool l_bBikeBleGattReady;

static int BikeBleGatt_RemoteEventHandler(uint16_t usEventId, uint8_t *pData,
                                          uint16_t usLength);

/* BikeBleGatt_ResetSlot: 清除 slot 并保留服务类型。
 * 参数：
 *   - pSlot: 目标 slot
 *   - ucServiceMask: HR 或 CSC 位
 * 返回值：无
 */
static void BikeBleGatt_ResetSlot(BIKE_BLE_GATT_SLOT *pSlot,
                                  uint8_t ucServiceMask)
{
    if (NULL != pSlot)
    {
        (void)memset(pSlot, 0, sizeof(*pSlot));
        pSlot->ucServiceMask = ucServiceMask;
        pSlot->ucConnIndex = BIKE_BLE_GATT_INVALID_CONN_INDEX;
        pSlot->usRemoteHandle = SIBLES_ERROR_REMOTE_HANDLE;
    }

    return;
}

/* BikeBleGatt_GetSlotByMask: 按服务位获取固定 slot。
 * 参数：
 *   - ucServiceMask: HR 或 CSC 单个位
 * 返回值：slot；不支持时返回 NULL
 */
static BIKE_BLE_GATT_SLOT *BikeBleGatt_GetSlotByMask(uint8_t ucServiceMask)
{
    uint8_t ucIndex;

    for (ucIndex = 0U; ucIndex < BIKE_BLE_GATT_SLOT_COUNT; ucIndex++)
    {
        if (ucServiceMask == l_aBikeBleGattSlots[ucIndex].ucServiceMask)
        {
            return &l_aBikeBleGattSlots[ucIndex];
        }
    }

    return NULL;
}

/* BikeBleGatt_GetUuid: 返回服务和测量 characteristic UUID。
 * 参数：
 *   - ucServiceMask: HR 或 CSC 单个位
 *   - pServiceUuid/pValueUuid: 输出 UUID
 * 返回值：支持该服务返回 true，否则返回 false
 */
static bool BikeBleGatt_GetUuid(uint8_t ucServiceMask, uint16_t *pServiceUuid,
                                uint16_t *pValueUuid)
{
    if ((NULL == pServiceUuid) || (NULL == pValueUuid))
    {
        return false;
    }
    if (BIKE_BLE_SERVICE_HEART_RATE == ucServiceMask)
    {
        *pServiceUuid = ATT_UUID_16(ATT_SVC_HEART_RATE);
        *pValueUuid = ATT_UUID_16(ATT_CHAR_HEART_RATE_MEAS);
        return true;
    }
    if (BIKE_BLE_SERVICE_CSC == ucServiceMask)
    {
        *pServiceUuid = ATT_UUID_16(ATT_SVC_CYCLING_SPEED_CADENCE);
        *pValueUuid = ATT_UUID_16(ATT_CHAR_CSC_MEAS);
        return true;
    }

    return false;
}

/* BikeBleGatt_NotifyReady: 通知上层服务订阅结果。
 * 参数：
 *   - pSlot: 目标 slot
 *   - bReady: 是否就绪
 * 返回值：无
 */
static void BikeBleGatt_NotifyReady(const BIKE_BLE_GATT_SLOT *pSlot,
                                    bool bReady)
{
    if ((NULL != pSlot) && (NULL != l_pBikeBleGattReadyCallback))
    {
        l_pBikeBleGattReadyCallback(pSlot->ucConnIndex,
                                    pSlot->ucServiceMask, bReady);
    }

    return;
}

/* BikeBleGatt_StartNext: 同一连接串行发现 HR/CSC，避免响应无法区分。
 * 参数：
 *   - ucConnIndex: SDK 连接索引
 * 返回值：命令提交成功或无需动作返回 true
 */
static bool BikeBleGatt_StartNext(uint8_t ucConnIndex)
{
    BIKE_BLE_GATT_SLOT *pSlot;
    uint16_t usServiceUuid;
    uint16_t usValueUuid;
    uint8_t ucIndex;
    int8_t cResult;

    for (ucIndex = 0U; ucIndex < BIKE_BLE_GATT_SLOT_COUNT; ucIndex++)
    {
        pSlot = &l_aBikeBleGattSlots[ucIndex];
        if (pSlot->bRequested && (ucConnIndex == pSlot->ucConnIndex) &&
            ((BIKE_BLE_GATT_STATE_SEARCHING == pSlot->eState) ||
             (BIKE_BLE_GATT_STATE_REGISTERING == pSlot->eState) ||
             (BIKE_BLE_GATT_STATE_SUBSCRIBING == pSlot->eState) ||
             (BIKE_BLE_GATT_STATE_WAIT_SECURITY == pSlot->eState)))
        {
            return true;
        }
    }

    for (ucIndex = 0U; ucIndex < BIKE_BLE_GATT_SLOT_COUNT; ucIndex++)
    {
        pSlot = &l_aBikeBleGattSlots[ucIndex];
        if (pSlot->bRequested && (ucConnIndex == pSlot->ucConnIndex) &&
            (BIKE_BLE_GATT_STATE_IDLE == pSlot->eState))
        {
            if (!BikeBleGatt_GetUuid(pSlot->ucServiceMask, &usServiceUuid,
                                     &usValueUuid))
            {
                pSlot->eState = BIKE_BLE_GATT_STATE_FAILED;
                BikeBleGatt_NotifyReady(pSlot, false);
                continue;
            }
            pSlot->eState = BIKE_BLE_GATT_STATE_SEARCHING;
            cResult = sibles_search_service(ucConnIndex, ATT_UUID_16_LEN,
                                            (uint8_t *)&usServiceUuid);
            if (0 != cResult)
            {
                pSlot->eState = BIKE_BLE_GATT_STATE_FAILED;
                LOG_E("search submit failed conn=%u mask=0x%02x ret=%d",
                      ucConnIndex, pSlot->ucServiceMask, cResult);
                BikeBleGatt_NotifyReady(pSlot, false);
                continue;
            }
            LOG_I("search conn=%u mask=0x%02x", ucConnIndex,
                  pSlot->ucServiceMask);
            return true;
        }
    }

    return true;
}

/* BikeBleGatt_Subscribe: 写入测量 CCCD 并等待远端响应。
 * 参数：
 *   - pSlot: 已注册的服务 slot
 * 返回值：写命令提交成功返回 true，否则返回 false
 */
static bool BikeBleGatt_Subscribe(BIKE_BLE_GATT_SLOT *pSlot)
{
    sibles_write_remote_value_t tValue;
    uint16_t usEnable;
    int8_t cResult;

    if ((NULL == pSlot) || (0U == pSlot->usCccdHandle) ||
        (SIBLES_ERROR_REMOTE_HANDLE == pSlot->usRemoteHandle))
    {
        return false;
    }
    usEnable = 1U;
    (void)memset(&tValue, 0, sizeof(tValue));
    tValue.handle = pSlot->usCccdHandle;
    tValue.write_type = SIBLES_WRITE;
    tValue.len = sizeof(usEnable);
    tValue.value = (uint8_t *)&usEnable;
    pSlot->eState = BIKE_BLE_GATT_STATE_SUBSCRIBING;
    cResult = sibles_write_remote_value(pSlot->usRemoteHandle,
                                        pSlot->ucConnIndex, &tValue);
    if (SIBLES_WRITE_NO_ERR != cResult)
    {
        pSlot->eState = pSlot->bSecuritySeen ?
                        BIKE_BLE_GATT_STATE_FAILED :
                        BIKE_BLE_GATT_STATE_WAIT_SECURITY;
        LOG_W("CCCD submit failed conn=%u mask=0x%02x ret=%d",
              pSlot->ucConnIndex, pSlot->ucServiceMask, cResult);
        if (BIKE_BLE_GATT_STATE_FAILED == pSlot->eState)
        {
            BikeBleGatt_NotifyReady(pSlot, false);
        }
        return false;
    }

    return true;
}

/* BikeBleGatt_HandleSearch: 保存搜索结果并注册逐连接远端服务。
 * 参数：
 *   - pResponse: SIBLES 搜索响应
 * 返回值：无
 */
static void BikeBleGatt_HandleSearch(const sibles_svc_search_rsp_t *pResponse)
{
    BIKE_BLE_GATT_SLOT *pSlot;
    sibles_svc_search_char_t *pCharacteristic;
    uint16_t usServiceUuid;
    uint16_t usValueUuid;
    uint16_t usOffset;
    uint32_t ulIndex;

    if ((NULL == pResponse) || (ATT_UUID_16_LEN != pResponse->search_svc_len))
    {
        return;
    }
    pSlot = NULL;
    for (ulIndex = 0U; ulIndex < BIKE_BLE_GATT_SLOT_COUNT; ulIndex++)
    {
        if (!BikeBleGatt_GetUuid(l_aBikeBleGattSlots[ulIndex].ucServiceMask,
                                 &usServiceUuid, &usValueUuid))
        {
            continue;
        }
        if ((pResponse->conn_idx == l_aBikeBleGattSlots[ulIndex].ucConnIndex) &&
            (BIKE_BLE_GATT_STATE_SEARCHING ==
             l_aBikeBleGattSlots[ulIndex].eState) &&
            (0 == memcmp(pResponse->search_uuid, &usServiceUuid,
                         ATT_UUID_16_LEN)))
        {
            pSlot = &l_aBikeBleGattSlots[ulIndex];
            break;
        }
    }
    if (NULL == pSlot)
    {
        return;
    }
    if ((HL_ERR_NO_ERROR != pResponse->result) || (NULL == pResponse->svc) ||
        (NULL == pResponse->svc->att_db))
    {
        pSlot->eState = pSlot->bSecuritySeen ?
                        BIKE_BLE_GATT_STATE_FAILED :
                        BIKE_BLE_GATT_STATE_WAIT_SECURITY;
        if (BIKE_BLE_GATT_STATE_FAILED == pSlot->eState)
        {
            BikeBleGatt_NotifyReady(pSlot, false);
            (void)BikeBleGatt_StartNext(pSlot->ucConnIndex);
        }
        return;
    }

    pSlot->usServiceStart = pResponse->svc->hdl_start;
    pSlot->usServiceEnd = pResponse->svc->hdl_end;
    pSlot->usValueHandle = 0U;
    pSlot->usCccdHandle = 0U;
    pCharacteristic = (sibles_svc_search_char_t *)pResponse->svc->att_db;
    for (ulIndex = 0U; ulIndex < pResponse->svc->char_count; ulIndex++)
    {
        if ((ATT_UUID_16_LEN == pCharacteristic->uuid_len) &&
            (0 == memcmp(pCharacteristic->uuid, &usValueUuid,
                         ATT_UUID_16_LEN)))
        {
            pSlot->usValueHandle = pCharacteristic->pointer_hdl;
            pSlot->usCccdHandle = sibles_descriptor_handle_find(
                pCharacteristic, ATT_DESC_CLIENT_CHAR_CFG);
        }
        usOffset = (uint16_t)(sizeof(sibles_svc_search_char_t) +
                   pCharacteristic->desc_count *
                   sizeof(struct sibles_disc_char_desc_ind));
        pCharacteristic = (sibles_svc_search_char_t *)(
            (uint8_t *)pCharacteristic + usOffset);
    }
    if ((0U == pSlot->usValueHandle) || (0U == pSlot->usCccdHandle))
    {
        pSlot->eState = BIKE_BLE_GATT_STATE_FAILED;
        BikeBleGatt_NotifyReady(pSlot, false);
        (void)BikeBleGatt_StartNext(pSlot->ucConnIndex);
        return;
    }

    pSlot->eState = BIKE_BLE_GATT_STATE_REGISTERING;
    pSlot->usRemoteHandle = sibles_register_remote_svc(
        pSlot->ucConnIndex, pSlot->usServiceStart, pSlot->usServiceEnd,
        BikeBleGatt_RemoteEventHandler);
    if (SIBLES_ERROR_REMOTE_HANDLE == pSlot->usRemoteHandle)
    {
        pSlot->eState = BIKE_BLE_GATT_STATE_FAILED;
        BikeBleGatt_NotifyReady(pSlot, false);
        (void)BikeBleGatt_StartNext(pSlot->ucConnIndex);
    }

    return;
}

/* BikeBleGatt_SecurityReady: 配对/加密后重试搜索或 CCCD。
 * 参数：
 *   - ucConnIndex: SDK 连接索引
 * 返回值：无
 */
static void BikeBleGatt_SecurityReady(uint8_t ucConnIndex)
{
    BIKE_BLE_GATT_SLOT *pSlot;
    uint8_t ucIndex;

    for (ucIndex = 0U; ucIndex < BIKE_BLE_GATT_SLOT_COUNT; ucIndex++)
    {
        pSlot = &l_aBikeBleGattSlots[ucIndex];
        if (pSlot->bRequested && (ucConnIndex == pSlot->ucConnIndex))
        {
            pSlot->bSecuritySeen = true;
            if (BIKE_BLE_GATT_STATE_WAIT_SECURITY == pSlot->eState)
            {
                if ((0U != pSlot->usCccdHandle) &&
                    (SIBLES_ERROR_REMOTE_HANDLE != pSlot->usRemoteHandle))
                {
                    (void)BikeBleGatt_Subscribe(pSlot);
                }
                else
                {
                    pSlot->eState = BIKE_BLE_GATT_STATE_IDLE;
                }
            }
        }
    }
    (void)BikeBleGatt_StartNext(ucConnIndex);

    return;
}

/* BikeBleGatt_RemoteEventHandler: 处理逐连接注册、CCCD 和测量通知。
 * 参数：
 *   - usEventId: SIBLES 远端服务事件
 *   - pData/usLength: 事件数据及长度
 * 返回值：0
 */
static int BikeBleGatt_RemoteEventHandler(uint16_t usEventId, uint8_t *pData,
                                          uint16_t usLength)
{
    BIKE_BLE_GATT_SLOT *pSlot;
    uint8_t ucIndex;

    if (NULL == pData)
    {
        return 0;
    }
    if ((SIBLES_REGISTER_REMOTE_SVC_RSP == usEventId) &&
        (sizeof(sibles_register_remote_svc_rsp_t) <= usLength))
    {
        const sibles_register_remote_svc_rsp_t *pResponse;

        pResponse = (const sibles_register_remote_svc_rsp_t *)pData;
        for (ucIndex = 0U; ucIndex < BIKE_BLE_GATT_SLOT_COUNT; ucIndex++)
        {
            pSlot = &l_aBikeBleGattSlots[ucIndex];
            if ((pResponse->conn_idx == pSlot->ucConnIndex) &&
                (BIKE_BLE_GATT_STATE_REGISTERING == pSlot->eState))
            {
                if (HL_ERR_NO_ERROR == pResponse->status)
                {
                    (void)BikeBleGatt_Subscribe(pSlot);
                }
                else
                {
                    pSlot->eState = BIKE_BLE_GATT_STATE_FAILED;
                    BikeBleGatt_NotifyReady(pSlot, false);
                    (void)BikeBleGatt_StartNext(pSlot->ucConnIndex);
                }
                break;
            }
        }
    }
    else if ((SIBLES_WRITE_REMOTE_VALUE_RSP == usEventId) &&
             (sizeof(sibles_write_remote_value_rsp_t) <= usLength))
    {
        const sibles_write_remote_value_rsp_t *pResponse;

        pResponse = (const sibles_write_remote_value_rsp_t *)pData;
        for (ucIndex = 0U; ucIndex < BIKE_BLE_GATT_SLOT_COUNT; ucIndex++)
        {
            pSlot = &l_aBikeBleGattSlots[ucIndex];
            if ((pResponse->conn_idx == pSlot->ucConnIndex) &&
                (BIKE_BLE_GATT_STATE_SUBSCRIBING == pSlot->eState))
            {
                if (HL_ERR_NO_ERROR == pResponse->result)
                {
                    pSlot->eState = BIKE_BLE_GATT_STATE_READY;
                    BikeBleGatt_NotifyReady(pSlot, true);
                }
                else if (!pSlot->bSecuritySeen)
                {
                    pSlot->eState = BIKE_BLE_GATT_STATE_WAIT_SECURITY;
                }
                else
                {
                    pSlot->eState = BIKE_BLE_GATT_STATE_FAILED;
                    BikeBleGatt_NotifyReady(pSlot, false);
                }
                (void)BikeBleGatt_StartNext(pSlot->ucConnIndex);
                break;
            }
        }
    }
    else if ((SIBLES_REMOTE_EVENT_IND == usEventId) &&
             (sizeof(sibles_remote_event_ind_t) <= usLength))
    {
        const sibles_remote_event_ind_t *pIndication;

        pIndication = (const sibles_remote_event_ind_t *)pData;
        if ((NULL == pIndication->value) || (0U == pIndication->length))
        {
            return 0;
        }
        for (ucIndex = 0U; ucIndex < BIKE_BLE_GATT_SLOT_COUNT; ucIndex++)
        {
            uint16_t usHeartRateBpm;
            BIKE_CSC_MEASUREMENT tMeasurement;

            pSlot = &l_aBikeBleGattSlots[ucIndex];
            if ((BIKE_BLE_GATT_STATE_READY != pSlot->eState) ||
                (pIndication->conn_idx != pSlot->ucConnIndex) ||
                (pIndication->handle != pSlot->usValueHandle))
            {
                continue;
            }
            if ((BIKE_BLE_SERVICE_HEART_RATE == pSlot->ucServiceMask) &&
                BIKE_BLE_MEAS_ParseHeartRate(pIndication->value,
                                             pIndication->length,
                                             &usHeartRateBpm) &&
                (NULL != l_pBikeBleGattHeartRateCallback))
            {
                l_pBikeBleGattHeartRateCallback(pSlot->ucConnIndex,
                                                usHeartRateBpm);
            }
            else if ((BIKE_BLE_SERVICE_CSC == pSlot->ucServiceMask) &&
                     BIKE_BLE_MEAS_ParseCsc(pIndication->value,
                                            pIndication->length,
                                            &tMeasurement) &&
                     (NULL != l_pBikeBleGattCscCallback))
            {
                l_pBikeBleGattCscCallback(pSlot->ucConnIndex, &tMeasurement);
            }
            break;
        }
    }

    return 0;
}

/* BikeBleGatt_EventHandler: 路由 service search、安全状态和断链事件。
 * 参数：
 *   - usEventId: BLE 事件编号
 *   - pData/usLength: 事件数据及长度
 *   - ulContext: 未使用上下文
 * 返回值：0
 */
static int BikeBleGatt_EventHandler(uint16_t usEventId, uint8_t *pData,
                                    uint16_t usLength, uint32_t ulContext)
{
    (void)ulContext;
    switch (usEventId)
    {
    case SIBLES_SEARCH_SVC_RSP:
        if ((NULL != pData) && (sizeof(sibles_svc_search_rsp_t) <= usLength))
        {
            BikeBleGatt_HandleSearch((const sibles_svc_search_rsp_t *)pData);
        }
        break;

    case BLE_GAP_BOND_IND:
        if ((NULL != pData) && (sizeof(ble_gap_bond_ind_t) <= usLength))
        {
            const ble_gap_bond_ind_t *pBond;

            pBond = (const ble_gap_bond_ind_t *)pData;
            if (GAPC_PAIRING_SUCCEED == pBond->info)
            {
                BikeBleGatt_SecurityReady(pBond->conn_idx);
            }
        }
        break;

    case BLE_GAP_ENCRYPT_IND:
        if ((NULL != pData) && (sizeof(ble_gap_encrypt_ind_t) <= usLength))
        {
            const ble_gap_encrypt_ind_t *pEncrypt;

            pEncrypt = (const ble_gap_encrypt_ind_t *)pData;
            BikeBleGatt_SecurityReady(pEncrypt->conn_idx);
        }
        break;

    case BLE_GAP_DISCONNECTED_IND:
        if ((NULL != pData) && (sizeof(ble_gap_disconnected_ind_t) <= usLength))
        {
            const ble_gap_disconnected_ind_t *pDisconnected;

            pDisconnected = (const ble_gap_disconnected_ind_t *)pData;
            BIKE_BLE_GATT_Detach(pDisconnected->conn_idx);
        }
        break;

    default:
        break;
    }

    return 0;
}
BLE_EVENT_REGISTER(BikeBleGatt_EventHandler, NULL);

/* BIKE_BLE_GATT_Init: 初始化两个逐连接标准服务 slot。
 * 参数：
 *   - pReadyCallback: service 就绪/失败回调
 *   - pHeartRateCallback: 心率测量回调
 *   - pCscCallback: CSC 测量回调
 * 返回值：参数有效返回 true，否则返回 false
 */
bool BIKE_BLE_GATT_Init(BIKE_BLE_GATT_READY_CALLBACK pReadyCallback,
                        BIKE_BLE_GATT_HEART_RATE_CALLBACK pHeartRateCallback,
                        BIKE_BLE_GATT_CSC_CALLBACK pCscCallback)
{
    if ((NULL == pReadyCallback) || (NULL == pHeartRateCallback) ||
        (NULL == pCscCallback))
    {
        return false;
    }
    BikeBleGatt_ResetSlot(&l_aBikeBleGattSlots[0],
                          BIKE_BLE_SERVICE_HEART_RATE);
    BikeBleGatt_ResetSlot(&l_aBikeBleGattSlots[1], BIKE_BLE_SERVICE_CSC);
    l_pBikeBleGattReadyCallback = pReadyCallback;
    l_pBikeBleGattHeartRateCallback = pHeartRateCallback;
    l_pBikeBleGattCscCallback = pCscCallback;
    l_bBikeBleGattReady = true;

    return true;
}

/* BIKE_BLE_GATT_Attach: 为连接建立一个或两个标准 service client。
 * 参数：
 *   - ucConnIndex: SDK 连接索引
 *   - ucServiceMask: HR/CSC 服务位
 * 返回值：请求有效且搜索已提交返回 true，否则返回 false
 */
bool BIKE_BLE_GATT_Attach(uint8_t ucConnIndex, uint8_t ucServiceMask)
{
    BIKE_BLE_GATT_SLOT *pSlot;
    uint8_t ucMask;
    uint8_t ucSupportedMask;

    ucSupportedMask = BIKE_BLE_SERVICE_HEART_RATE | BIKE_BLE_SERVICE_CSC;
    if ((!l_bBikeBleGattReady) ||
        (BIKE_BLE_GATT_INVALID_CONN_INDEX == ucConnIndex) ||
        (0U == ucServiceMask) ||
        (0U != (ucServiceMask & (uint8_t)(~ucSupportedMask))))
    {
        return false;
    }
    for (ucMask = BIKE_BLE_SERVICE_HEART_RATE;
         ucMask <= BIKE_BLE_SERVICE_CSC; ucMask <<= 1U)
    {
        if (0U == (ucServiceMask & ucMask))
        {
            continue;
        }
        pSlot = BikeBleGatt_GetSlotByMask(ucMask);
        if ((NULL == pSlot) ||
            (pSlot->bRequested && (ucConnIndex != pSlot->ucConnIndex)))
        {
            return false;
        }
    }
    for (ucMask = BIKE_BLE_SERVICE_HEART_RATE;
         ucMask <= BIKE_BLE_SERVICE_CSC; ucMask <<= 1U)
    {
        if (0U == (ucServiceMask & ucMask))
        {
            continue;
        }
        pSlot = BikeBleGatt_GetSlotByMask(ucMask);
        if (NULL == pSlot)
        {
            return false;
        }
        if (!pSlot->bRequested)
        {
            BikeBleGatt_ResetSlot(pSlot, ucMask);
            pSlot->bRequested = true;
            pSlot->ucConnIndex = ucConnIndex;
        }
    }

    return BikeBleGatt_StartNext(ucConnIndex);
}

/* BIKE_BLE_GATT_Detach: 注销并清理某连接上的所有标准服务。
 * 参数：
 *   - ucConnIndex: SDK 连接索引
 * 返回值：无
 */
void BIKE_BLE_GATT_Detach(uint8_t ucConnIndex)
{
    BIKE_BLE_GATT_SLOT *pSlot;
    uint8_t ucIndex;
    uint8_t ucServiceMask;

    for (ucIndex = 0U; ucIndex < BIKE_BLE_GATT_SLOT_COUNT; ucIndex++)
    {
        pSlot = &l_aBikeBleGattSlots[ucIndex];
        if ((!pSlot->bRequested) || (ucConnIndex != pSlot->ucConnIndex))
        {
            continue;
        }
        if ((0U != pSlot->usServiceStart) && (0U != pSlot->usServiceEnd) &&
            (SIBLES_ERROR_REMOTE_HANDLE != pSlot->usRemoteHandle))
        {
            sibles_unregister_remote_svc(ucConnIndex, pSlot->usServiceStart,
                                         pSlot->usServiceEnd,
                                         BikeBleGatt_RemoteEventHandler);
        }
        ucServiceMask = pSlot->ucServiceMask;
        BikeBleGatt_ResetSlot(pSlot, ucServiceMask);
    }

    return;
}
