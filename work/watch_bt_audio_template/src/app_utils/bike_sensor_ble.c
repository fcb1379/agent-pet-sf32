#include "bike_sensor_ble.h"

#include <stddef.h>
#include <string.h>

#include <rtthread.h>

#include "bike_ble_advertising.h"
#ifdef BSP_BLE_SIBLES
#include "bike_ble_gatt_client.h"
#endif
#include "bike_settings.h"
#include "bf0_ble_gap.h"

#ifdef BSP_BLE_SIBLES
#include "bf0_ble_common.h"
#include "bf0_sibles.h"
#include "ble_connection_manager.h"
#endif
#ifdef BSP_SHARE_PREFS
#include "share_prefs.h"
#endif

#define LOG_TAG "bike.sensor"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#define BIKE_SENSOR_BLE_TIMEOUT_MS (5000U)
#define BIKE_SENSOR_BLE_THREAD_STACK_SIZE (2048U)
#define BIKE_SENSOR_BLE_THREAD_PRIORITY (22U)
#define BIKE_SENSOR_BLE_QUEUE_DEPTH (12U)
#define BIKE_SENSOR_BLE_RETRY_DELAY_MS (15000U)
#define BIKE_SENSOR_BLE_SCAN_DURATION_10MS (1000U)
#define BIKE_SENSOR_BLE_SCAN_INTERVAL_625US (160U)
#define BIKE_SENSOR_BLE_SCAN_WINDOW_625US (80U)
#define BIKE_SENSOR_BLE_CONNECT_TIMEOUT_10MS (1000U)
#define BIKE_SENSOR_BLE_CONNECT_SCAN_625US (48U)
#define BIKE_SENSOR_BLE_CONNECT_INTERVAL_MIN_1250US (24U)
#define BIKE_SENSOR_BLE_CONNECT_INTERVAL_MAX_1250US (40U)
#define BIKE_SENSOR_BLE_CONNECT_SUPERVISION_10MS (400U)
#define BIKE_SENSOR_BLE_CONNECT_CE_MAX_625US (48U)
#define BIKE_SENSOR_BLE_MIN_RSSI_DBM (-90)
#define BIKE_SENSOR_BLE_INVALID_CONN_INDEX (0xFFU)
#define BIKE_SENSOR_BLE_PEER_VERSION (1U)
#define BIKE_SENSOR_BLE_PEER_KEY_HEART_RATE "hr_peer"
#define BIKE_SENSOR_BLE_PEER_KEY_CSC "csc_peer"
#define BIKE_SENSOR_BLE_PEER_KEY_POWER "power_peer"

/* BIKE_SENSOR_BLE_EVENT: 连接管理线程接收的固定事件。 */
typedef enum _BIKE_SENSOR_BLE_EVENT
{
    BIKE_SENSOR_BLE_EVENT_POWER_ON = 0,
    BIKE_SENSOR_BLE_EVENT_ADVERTISEMENT,
    BIKE_SENSOR_BLE_EVENT_SCAN_STARTED,
    BIKE_SENSOR_BLE_EVENT_SCAN_STOPPED,
    BIKE_SENSOR_BLE_EVENT_CONNECT_RESULT,
    BIKE_SENSOR_BLE_EVENT_CONNECTED,
    BIKE_SENSOR_BLE_EVENT_DISCONNECTED,
    BIKE_SENSOR_BLE_EVENT_GATT_READY,
    BIKE_SENSOR_BLE_EVENT_FORCE_SCAN,
    BIKE_SENSOR_BLE_EVENT_CLEAR_PEERS
} BIKE_SENSOR_BLE_EVENT;

/* BIKE_SENSOR_BLE_MESSAGE: BLE 回调投递给连接管理线程的固定消息。 */
typedef struct _BIKE_SENSOR_BLE_MESSAGE
{
    BIKE_SENSOR_BLE_EVENT eEvent;
    ble_gap_addr_t tAddress;
    uint8_t ucServiceMask;
    uint8_t ucConnIndex;
    uint8_t ucStatus;
    int8_t cRssi;
} BIKE_SENSOR_BLE_MESSAGE;

/* BIKE_SENSOR_BLE_PEER: 持久化的目标传感器地址记录。 */
typedef struct _BIKE_SENSOR_BLE_PEER
{
    uint8_t ucVersion;
    uint8_t ucAddressType;
    uint8_t ucServiceMask;
    uint8_t ucReserved;
    uint8_t aAddress[BD_ADDR_LEN];
    uint16_t usChecksum;
} BIKE_SENSOR_BLE_PEER;

/* BIKE_SENSOR_BLE_MANAGER: 仅连接管理线程访问的中央设备状态。 */
typedef struct _BIKE_SENSOR_BLE_MANAGER
{
    bool bPowerOn;
    bool bScanning;
    bool bConnecting;
    bool bCandidatePending;
    bool bScanRequested;
    bool bPendingFromScan;
    bool bHeartRatePeerKnown;
    bool bCscPeerKnown;
    bool bPowerPeerKnown;
    uint8_t ucHeartRateConnIndex;
    uint8_t ucCscConnIndex;
    uint8_t ucPowerConnIndex;
    uint8_t ucPendingServiceMask;
    uint8_t ucKnownAttemptMask;
    int8_t cPendingRssi;
    uint32_t ulNextActionMs;
    uint32_t ulConnectionAttemptCount;
    ble_gap_addr_t tHeartRateAddress;
    ble_gap_addr_t tCscAddress;
    ble_gap_addr_t tPowerAddress;
    ble_gap_addr_t tPendingAddress;
} BIKE_SENSOR_BLE_MANAGER;

/* l_tBikeSensorSnapshot: BLE 回调与 UI 共享的传感器快照。 */
static BIKE_SENSOR_BLE_SNAPSHOT l_tBikeSensorSnapshot;

/* l_tBikeCscState: BLE 回调独占的 CSC 累计值和回绕计算状态。 */
static BIKE_CSC_STATE l_tBikeCscState;

/* l_tBikeSensorMutex: 保护传感器快照的互斥锁。 */
static struct rt_mutex l_tBikeSensorMutex;

/* l_tBikeSensorQueue: BLE 回调到连接管理线程的固定容量消息队列。 */
static struct rt_messagequeue l_tBikeSensorQueue;

/* l_tBikeSensorThread: 执行扫描、连接、配对和重连的静态线程。 */
static struct rt_thread l_tBikeSensorThread;

/* l_aBikeSensorThreadStack: 连接管理线程固定 2048 字节栈。 */
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikeSensorThreadStack[BIKE_SENSOR_BLE_THREAD_STACK_SIZE];

/* l_aBikeSensorQueuePool: 十二条固定连接事件的静态队列存储。 */
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikeSensorQueuePool[BIKE_SENSOR_BLE_QUEUE_DEPTH *
                                      sizeof(BIKE_SENSOR_BLE_MESSAGE)];

/* l_tBikeSensorManager: 连接管理线程私有状态。 */
static BIKE_SENSOR_BLE_MANAGER l_tBikeSensorManager;

/* l_bBikeSensorReady: 互斥锁和固定容量状态已完成初始化。 */
static bool l_bBikeSensorReady;

/* l_ulBikeSensorDroppedEventCount: 锁竞争或事件长度异常的累计计数，
 * 范围 0~UINT32_MAX，达到上限后保持饱和。
 */
static uint32_t l_ulBikeSensorDroppedEventCount;

#ifdef BSP_SHARE_PREFS
/* l_aBikeSensorPrefName: 传感器地址 FlashDB 命名空间。 */
static const char l_aBikeSensorPrefName[32] = "bike_sensor_ble_peers_v1";

/* l_pBikeSensorPrefs: 连接管理线程独占的传感器地址偏好句柄。 */
static share_prefs_t *l_pBikeSensorPrefs;
#endif

/* BikeSensorBle_CountDroppedEvent: 在短临界区内饱和增加丢弃计数。
 * 返回值：无
 */
static void BikeSensorBle_CountDroppedEvent(void)
{
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    if (UINT32_MAX != l_ulBikeSensorDroppedEventCount)
    {
        l_ulBikeSensorDroppedEventCount++;
    }
    rt_hw_interrupt_enable(tLevel);

    return;
}

/* BikeSensorBle_GetDroppedEventCount: 在短临界区内读取丢弃计数。
 * 返回值：累计丢弃事件数
 */
static uint32_t BikeSensorBle_GetDroppedEventCount(void)
{
    rt_base_t tLevel;
    uint32_t ulCount;

    tLevel = rt_hw_interrupt_disable();
    ulCount = l_ulBikeSensorDroppedEventCount;
    rt_hw_interrupt_enable(tLevel);

    return ulCount;
}

/* BikeSensorBle_Lock: 非阻塞获取回调共享快照锁。
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeSensorBle_Lock(void)
{
    return l_bBikeSensorReady &&
           (RT_EOK == rt_mutex_take(&l_tBikeSensorMutex, RT_WAITING_NO));
}

/* BikeSensorBle_LockForever: 连接管理线程可靠获取共享快照锁。
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeSensorBle_LockForever(void)
{
    return l_bBikeSensorReady &&
           (RT_EOK == rt_mutex_take(&l_tBikeSensorMutex, RT_WAITING_FOREVER));
}

/* BikeSensorBle_Unlock: 释放共享快照锁。
 * 返回值：无
 */
static void BikeSensorBle_Unlock(void)
{
    rt_err_t eResult;

    eResult = rt_mutex_release(&l_tBikeSensorMutex);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex release failed: %d", eResult);
    }

    return;
}

#ifdef BSP_BLE_SIBLES
/* BikeSensorBle_AddressEqual: 比较两个 BLE 地址及其类型。
 * 参数：
 *   - pLeft/pRight: 待比较地址
 * 返回值：完全一致返回 true，否则返回 false
 */
static bool BikeSensorBle_AddressEqual(const ble_gap_addr_t *pLeft,
                                       const ble_gap_addr_t *pRight)
{
    return (NULL != pLeft) && (NULL != pRight) &&
           (pLeft->addr_type == pRight->addr_type) &&
           (0 == memcmp(pLeft->addr.addr, pRight->addr.addr, BD_ADDR_LEN));
}

/* BikeSensorBle_PeerChecksum: 计算持久化传感器地址记录校验和。
 * 参数：
 *   - pPeer: 地址记录
 * 返回值：16 位累加校验和
 */
static uint16_t BikeSensorBle_PeerChecksum(const BIKE_SENSOR_BLE_PEER *pPeer)
{
    uint16_t usChecksum;
    uint8_t ucIndex;

    if (NULL == pPeer)
    {
        return 0U;
    }
    usChecksum = (uint16_t)(0xA5U + pPeer->ucVersion +
                            pPeer->ucAddressType + pPeer->ucServiceMask);
    for (ucIndex = 0U; ucIndex < BD_ADDR_LEN; ucIndex++)
    {
        usChecksum = (uint16_t)(usChecksum + pPeer->aAddress[ucIndex]);
    }

    return usChecksum;
}

/* BikeSensorBle_PeerValid: 验证持久化地址记录版本、服务位和校验和。
 * 参数：
 *   - pPeer: 地址记录
 *   - ucRequiredMask: 必须包含的服务位
 * 返回值：有效返回 true，否则返回 false
 */
static bool BikeSensorBle_PeerValid(const BIKE_SENSOR_BLE_PEER *pPeer,
                                    uint8_t ucRequiredMask)
{
    uint8_t ucIndex;
    bool bAllZero;
    bool bAllOnes;

    if ((NULL == pPeer) || (BIKE_SENSOR_BLE_PEER_VERSION != pPeer->ucVersion) ||
        (0U == (pPeer->ucServiceMask & ucRequiredMask)) ||
        (3U < pPeer->ucAddressType) ||
        (pPeer->usChecksum != BikeSensorBle_PeerChecksum(pPeer)))
    {
        return false;
    }

    bAllZero = true;
    bAllOnes = true;
    for (ucIndex = 0U; ucIndex < BD_ADDR_LEN; ucIndex++)
    {
        if (0U != pPeer->aAddress[ucIndex])
        {
            bAllZero = false;
        }
        if (UINT8_MAX != pPeer->aAddress[ucIndex])
        {
            bAllOnes = false;
        }
    }

    return (!bAllZero) && (!bAllOnes);
}

/* BikeSensorBle_AddressFromPeer: 将持久化记录恢复为 SDK 地址。
 * 参数：
 *   - pPeer: 输入记录
 *   - pAddress: 输出 SDK 地址
 * 返回值：无
 */
static void BikeSensorBle_AddressFromPeer(const BIKE_SENSOR_BLE_PEER *pPeer,
                                          ble_gap_addr_t *pAddress)
{
    if ((NULL != pPeer) && (NULL != pAddress))
    {
        pAddress->addr_type = pPeer->ucAddressType;
        (void)memcpy(pAddress->addr.addr, pPeer->aAddress, BD_ADDR_LEN);
    }

    return;
}

/* BikeSensorBle_PeerFromAddress: 生成带校验和的持久化地址记录。
 * 参数：
 *   - pAddress: SDK 地址
 *   - ucServiceMask: 地址提供的目标服务
 *   - pPeer: 输出记录
 * 返回值：无
 */
static void BikeSensorBle_PeerFromAddress(const ble_gap_addr_t *pAddress,
                                          uint8_t ucServiceMask,
                                          BIKE_SENSOR_BLE_PEER *pPeer)
{
    if ((NULL == pAddress) || (NULL == pPeer))
    {
        return;
    }
    (void)memset(pPeer, 0, sizeof(*pPeer));
    pPeer->ucVersion = BIKE_SENSOR_BLE_PEER_VERSION;
    pPeer->ucAddressType = pAddress->addr_type;
    pPeer->ucServiceMask = ucServiceMask;
    (void)memcpy(pPeer->aAddress, pAddress->addr.addr, BD_ADDR_LEN);
    pPeer->usChecksum = BikeSensorBle_PeerChecksum(pPeer);

    return;
}

/* BikeSensorBle_LoadPeers: 从 FlashDB 恢复上次成功连接的传感器地址。
 * 参数：
 *   - pManager: 连接管理状态
 * 返回值：无
 */
static void BikeSensorBle_LoadPeers(BIKE_SENSOR_BLE_MANAGER *pManager)
{
#ifdef BSP_SHARE_PREFS
    BIKE_SENSOR_BLE_PEER tPeer;
    int32_t lLength;

    if (NULL == pManager)
    {
        return;
    }
    l_pBikeSensorPrefs = share_prefs_open(l_aBikeSensorPrefName,
                                          SHAREPREFS_MODE_PRIVATE);
    if (NULL == l_pBikeSensorPrefs)
    {
        LOG_W("peer storage unavailable");
        return;
    }

    (void)memset(&tPeer, 0, sizeof(tPeer));
    lLength = share_prefs_get_block(l_pBikeSensorPrefs,
                                    BIKE_SENSOR_BLE_PEER_KEY_HEART_RATE,
                                    &tPeer, sizeof(tPeer));
    if ((int32_t)sizeof(tPeer) == lLength &&
        BikeSensorBle_PeerValid(&tPeer, BIKE_BLE_SERVICE_HEART_RATE))
    {
        BikeSensorBle_AddressFromPeer(&tPeer, &pManager->tHeartRateAddress);
        pManager->bHeartRatePeerKnown = true;
    }

    (void)memset(&tPeer, 0, sizeof(tPeer));
    lLength = share_prefs_get_block(l_pBikeSensorPrefs,
                                    BIKE_SENSOR_BLE_PEER_KEY_CSC,
                                    &tPeer, sizeof(tPeer));
    if ((int32_t)sizeof(tPeer) == lLength &&
        BikeSensorBle_PeerValid(&tPeer, BIKE_BLE_SERVICE_CSC))
    {
        BikeSensorBle_AddressFromPeer(&tPeer, &pManager->tCscAddress);
        pManager->bCscPeerKnown = true;
    }

    (void)memset(&tPeer, 0, sizeof(tPeer));
    lLength = share_prefs_get_block(l_pBikeSensorPrefs,
                                    BIKE_SENSOR_BLE_PEER_KEY_POWER,
                                    &tPeer, sizeof(tPeer));
    if ((int32_t)sizeof(tPeer) == lLength &&
        BikeSensorBle_PeerValid(&tPeer, BIKE_BLE_SERVICE_POWER))
    {
        BikeSensorBle_AddressFromPeer(&tPeer, &pManager->tPowerAddress);
        pManager->bPowerPeerKnown = true;
    }
#else
    (void)pManager;
#endif

    return;
}

/* BikeSensorBle_SavePeer: 保存一个已成功建立链路的传感器地址。
 * 参数：
 *   - pAddress: 传感器地址
 *   - ucServiceMask: 该链路承载的服务位
 * 返回值：无
 */
static void BikeSensorBle_SavePeer(const ble_gap_addr_t *pAddress,
                                   uint8_t ucServiceMask)
{
#ifdef BSP_SHARE_PREFS
    BIKE_SENSOR_BLE_PEER tPeer;
    rt_err_t eResult;

    if ((NULL == pAddress) || (NULL == l_pBikeSensorPrefs))
    {
        return;
    }
    BikeSensorBle_PeerFromAddress(pAddress, ucServiceMask, &tPeer);
    if (0U != (ucServiceMask & BIKE_BLE_SERVICE_HEART_RATE))
    {
        eResult = share_prefs_set_block(l_pBikeSensorPrefs,
                                        BIKE_SENSOR_BLE_PEER_KEY_HEART_RATE,
                                        &tPeer, sizeof(tPeer));
        if (RT_EOK != eResult)
        {
            LOG_E("save HR peer failed: %d", eResult);
        }
    }
    if (0U != (ucServiceMask & BIKE_BLE_SERVICE_CSC))
    {
        eResult = share_prefs_set_block(l_pBikeSensorPrefs,
                                        BIKE_SENSOR_BLE_PEER_KEY_CSC,
                                        &tPeer, sizeof(tPeer));
        if (RT_EOK != eResult)
        {
            LOG_E("save CSC peer failed: %d", eResult);
        }
    }
    if (0U != (ucServiceMask & BIKE_BLE_SERVICE_POWER))
    {
        eResult = share_prefs_set_block(l_pBikeSensorPrefs,
                                        BIKE_SENSOR_BLE_PEER_KEY_POWER,
                                        &tPeer, sizeof(tPeer));
        if (RT_EOK != eResult)
        {
            LOG_E("save power peer failed: %d", eResult);
        }
    }
#else
    (void)pAddress;
    (void)ucServiceMask;
#endif

    return;
}

/* BikeSensorBle_RemovePeers: 删除本模块保存的传感器地址，不影响手机配对。
 * 返回值：无
 */
static void BikeSensorBle_RemovePeers(void)
{
#ifdef BSP_SHARE_PREFS
    rt_err_t eResult;

    if (NULL != l_pBikeSensorPrefs)
    {
        eResult = share_prefs_remove(l_pBikeSensorPrefs,
                                     BIKE_SENSOR_BLE_PEER_KEY_HEART_RATE);
        if ((RT_EOK != eResult) && (-RT_ERROR != eResult))
        {
            LOG_W("remove HR peer failed: %d", eResult);
        }
        eResult = share_prefs_remove(l_pBikeSensorPrefs,
                                     BIKE_SENSOR_BLE_PEER_KEY_CSC);
        if ((RT_EOK != eResult) && (-RT_ERROR != eResult))
        {
            LOG_W("remove CSC peer failed: %d", eResult);
        }
        eResult = share_prefs_remove(l_pBikeSensorPrefs,
                                     BIKE_SENSOR_BLE_PEER_KEY_POWER);
        if ((RT_EOK != eResult) && (-RT_ERROR != eResult))
        {
            LOG_W("remove power peer failed: %d", eResult);
        }
    }
#endif

    return;
}

/* BikeSensorBle_PostMessage: 非阻塞投递连接管理事件。
 * 参数：
 *   - pMessage: 固定长度事件
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeSensorBle_PostMessage(const BIKE_SENSOR_BLE_MESSAGE *pMessage)
{
    BIKE_SENSOR_BLE_MESSAGE tMessage;

    if ((NULL == pMessage) || (!l_bBikeSensorReady))
    {
        BikeSensorBle_CountDroppedEvent();
        return false;
    }
    tMessage = *pMessage;
    if (RT_EOK != rt_mq_send(&l_tBikeSensorQueue, &tMessage, sizeof(tMessage)))
    {
        BikeSensorBle_CountDroppedEvent();
        return false;
    }

    return true;
}

/* BikeSensorBle_PublishManager: 将线程私有连接状态发布到 UI 快照。
 * 参数：
 *   - pManager: 连接管理状态
 * 返回值：无
 */
static void BikeSensorBle_PublishManager(const BIKE_SENSOR_BLE_MANAGER *pManager)
{
    if ((NULL == pManager) || (!BikeSensorBle_LockForever()))
    {
        return;
    }
    l_tBikeSensorSnapshot.bPowerOn = pManager->bPowerOn;
    l_tBikeSensorSnapshot.bScanning = pManager->bScanning;
    l_tBikeSensorSnapshot.bConnecting = pManager->bConnecting;
    l_tBikeSensorSnapshot.bHeartRateConnected =
        (BIKE_SENSOR_BLE_INVALID_CONN_INDEX != pManager->ucHeartRateConnIndex);
    l_tBikeSensorSnapshot.bCscConnected =
        (BIKE_SENSOR_BLE_INVALID_CONN_INDEX != pManager->ucCscConnIndex);
    l_tBikeSensorSnapshot.bPowerConnected =
        (BIKE_SENSOR_BLE_INVALID_CONN_INDEX != pManager->ucPowerConnIndex);
    l_tBikeSensorSnapshot.ucHeartRateConnIndex = pManager->ucHeartRateConnIndex;
    l_tBikeSensorSnapshot.ucCscConnIndex = pManager->ucCscConnIndex;
    l_tBikeSensorSnapshot.ucPowerConnIndex = pManager->ucPowerConnIndex;
    l_tBikeSensorSnapshot.cLastRssi = pManager->cPendingRssi;
    l_tBikeSensorSnapshot.ulConnectionAttemptCount =
        pManager->ulConnectionAttemptCount;
    BikeSensorBle_Unlock();

    return;
}

/* BikeSensorBle_GetNeededMask: 返回尚未建立链路的目标服务位。
 * 参数：
 *   - pManager: 连接管理状态
 * 返回值：BIKE_BLE_SERVICE_* 位
 */
static uint8_t BikeSensorBle_GetNeededMask(const BIKE_SENSOR_BLE_MANAGER *pManager)
{
    uint8_t ucMask;

    if (NULL == pManager)
    {
        return 0U;
    }
    ucMask = 0U;
    if (BIKE_SENSOR_BLE_INVALID_CONN_INDEX == pManager->ucHeartRateConnIndex)
    {
        ucMask |= BIKE_BLE_SERVICE_HEART_RATE;
    }
    if (BIKE_SENSOR_BLE_INVALID_CONN_INDEX == pManager->ucCscConnIndex)
    {
        ucMask |= BIKE_BLE_SERVICE_CSC;
    }
    if (BIKE_SENSOR_BLE_INVALID_CONN_INDEX == pManager->ucPowerConnIndex)
    {
        ucMask |= BIKE_BLE_SERVICE_POWER;
    }

    return ucMask;
}

/* BikeSensorBle_TimeReached: 使用有符号差值处理 32 位毫秒计数回绕。
 * 参数：
 *   - ulNowMs: 当前时间
 *   - ulDeadlineMs: 截止时间
 * 返回值：已到期返回 true，否则返回 false
 */
static bool BikeSensorBle_TimeReached(uint32_t ulNowMs, uint32_t ulDeadlineMs)
{
    return (0 <= (int32_t)(ulNowMs - ulDeadlineMs));
}

/* BikeSensorBle_GetWaitTicks: 计算下一次重连动作前的线程阻塞时间。
 * 参数：
 *   - pManager: 连接管理状态
 * 返回值：RT-Thread 等待 tick；无计划动作时永久等待
 */
static rt_int32_t BikeSensorBle_GetWaitTicks(
    const BIKE_SENSOR_BLE_MANAGER *pManager)
{
    uint32_t ulNowMs;
    uint32_t ulDelayMs;
    uint8_t ucNeededMask;
    bool bKnownPeerNeeded;

    if ((NULL == pManager) || (!pManager->bPowerOn) || pManager->bScanning ||
        pManager->bConnecting)
    {
        return RT_WAITING_FOREVER;
    }
    ucNeededMask = BikeSensorBle_GetNeededMask(pManager);
    if (0U == ucNeededMask)
    {
        return RT_WAITING_FOREVER;
    }
    bKnownPeerNeeded =
        ((0U != (ucNeededMask & BIKE_BLE_SERVICE_HEART_RATE)) &&
         pManager->bHeartRatePeerKnown) ||
        ((0U != (ucNeededMask & BIKE_BLE_SERVICE_CSC)) &&
         pManager->bCscPeerKnown) ||
        ((0U != (ucNeededMask & BIKE_BLE_SERVICE_POWER)) &&
         pManager->bPowerPeerKnown);
    if ((!pManager->bScanRequested) && (!bKnownPeerNeeded))
    {
        return RT_WAITING_FOREVER;
    }

    ulNowMs = (uint32_t)rt_tick_get_millisecond();
    if (BikeSensorBle_TimeReached(ulNowMs, pManager->ulNextActionMs))
    {
        return RT_WAITING_NO;
    }
    ulDelayMs = pManager->ulNextActionMs - ulNowMs;

    return (rt_int32_t)rt_tick_from_millisecond(ulDelayMs);
}

/* BikeSensorBle_StartScan: 启动有时限的低占空比主动扫描。
 * 参数：
 *   - pManager: 连接管理状态
 * 返回值：命令提交成功返回 true，否则返回 false
 */
static bool BikeSensorBle_StartScan(BIKE_SENSOR_BLE_MANAGER *pManager)
{
    ble_gap_scan_start_t tScan;
    uint8_t ucResult;

    if ((NULL == pManager) || (!pManager->bPowerOn) || pManager->bScanning ||
        pManager->bConnecting || (0U == BikeSensorBle_GetNeededMask(pManager)))
    {
        return false;
    }
    (void)memset(&tScan, 0, sizeof(tScan));
    tScan.own_addr_type = GAPM_STATIC_ADDR;
    tScan.type = GAPM_SCAN_TYPE_OBSERVER;
    tScan.prop = GAPM_SCAN_PROP_PHY_1M_BIT | GAPM_SCAN_PROP_ACTIVE_1M_BIT |
                 GAPM_SCAN_PROP_FILT_TRUNC_BIT;
    tScan.dup_filt_pol = 1U;
    tScan.scan_param_1m.scan_intv = BIKE_SENSOR_BLE_SCAN_INTERVAL_625US;
    tScan.scan_param_1m.scan_wd = BIKE_SENSOR_BLE_SCAN_WINDOW_625US;
    tScan.duration = BIKE_SENSOR_BLE_SCAN_DURATION_10MS;
    tScan.period = 0U;
    ucResult = ble_gap_scan_start(&tScan);
    if (HL_ERR_NO_ERROR != ucResult)
    {
        LOG_E("scan start failed: %u", ucResult);
        pManager->ulNextActionMs = (uint32_t)rt_tick_get_millisecond() +
                                   BIKE_SENSOR_BLE_RETRY_DELAY_MS;
        return false;
    }

    pManager->bScanning = true;
    pManager->bCandidatePending = false;
    BikeSensorBle_PublishManager(pManager);
    LOG_I("sensor scan requested mask=0x%02x", BikeSensorBle_GetNeededMask(pManager));

    return true;
}

/* BikeSensorBle_StartConnection: 向一个目标地址发起直接连接。
 * 参数：
 *   - pManager: 连接管理状态
 *   - pAddress: 目标地址
 *   - ucServiceMask: 该地址候选服务位
 *   - cRssi: 最近发现 RSSI
 * 返回值：命令提交成功返回 true，否则返回 false
 */
static bool BikeSensorBle_StartConnection(BIKE_SENSOR_BLE_MANAGER *pManager,
                                          const ble_gap_addr_t *pAddress,
                                          uint8_t ucServiceMask, int8_t cRssi)
{
    ble_gap_connection_create_param_t tConnection;
    uint8_t ucResult;

    if ((NULL == pManager) || (NULL == pAddress) || pManager->bScanning ||
        pManager->bConnecting || (0U == ucServiceMask))
    {
        return false;
    }
    (void)memset(&tConnection, 0, sizeof(tConnection));
    tConnection.own_addr_type = GAPM_STATIC_ADDR;
    tConnection.type = GAPM_INIT_TYPE_DIRECT_CONN_EST;
    tConnection.conn_to = BIKE_SENSOR_BLE_CONNECT_TIMEOUT_10MS;
    tConnection.conn_param_1m.scan_intv = BIKE_SENSOR_BLE_CONNECT_SCAN_625US;
    tConnection.conn_param_1m.scan_wd = BIKE_SENSOR_BLE_CONNECT_SCAN_625US;
    tConnection.conn_param_1m.conn_intv_min =
        BIKE_SENSOR_BLE_CONNECT_INTERVAL_MIN_1250US;
    tConnection.conn_param_1m.conn_intv_max =
        BIKE_SENSOR_BLE_CONNECT_INTERVAL_MAX_1250US;
    tConnection.conn_param_1m.conn_latency = 0U;
    tConnection.conn_param_1m.supervision_to =
        BIKE_SENSOR_BLE_CONNECT_SUPERVISION_10MS;
    tConnection.conn_param_1m.ce_len_min = 0U;
    tConnection.conn_param_1m.ce_len_max = BIKE_SENSOR_BLE_CONNECT_CE_MAX_625US;
    tConnection.peer_addr = *pAddress;

    pManager->tPendingAddress = *pAddress;
    pManager->ucPendingServiceMask = ucServiceMask;
    pManager->cPendingRssi = cRssi;
    ucResult = ble_gap_create_connection(&tConnection);
    if (HL_ERR_NO_ERROR != ucResult)
    {
        LOG_E("connect submit failed: %u", ucResult);
        pManager->ulNextActionMs = (uint32_t)rt_tick_get_millisecond() +
                                   BIKE_SENSOR_BLE_RETRY_DELAY_MS;
        return false;
    }

    pManager->bConnecting = true;
    if (UINT32_MAX != pManager->ulConnectionAttemptCount)
    {
        pManager->ulConnectionAttemptCount++;
    }
    BikeSensorBle_PublishManager(pManager);
    LOG_I("sensor connect requested mask=0x%02x rssi=%d",
          ucServiceMask, cRssi);

    return true;
}

/* BikeSensorBle_GetConnectionForAddress: 查找目标地址已有的传感器连接。
 * 参数：
 *   - pManager: 连接管理状态
 *   - pAddress: 目标地址
 * 返回值：SDK 连接索引；未找到时返回 0xFF
 */
static uint8_t BikeSensorBle_GetConnectionForAddress(
    const BIKE_SENSOR_BLE_MANAGER *pManager, const ble_gap_addr_t *pAddress)
{
    if ((NULL == pManager) || (NULL == pAddress))
    {
        return BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
    }
    if ((BIKE_SENSOR_BLE_INVALID_CONN_INDEX !=
         pManager->ucHeartRateConnIndex) &&
        BikeSensorBle_AddressEqual(&pManager->tHeartRateAddress, pAddress))
    {
        return pManager->ucHeartRateConnIndex;
    }
    if ((BIKE_SENSOR_BLE_INVALID_CONN_INDEX != pManager->ucCscConnIndex) &&
        BikeSensorBle_AddressEqual(&pManager->tCscAddress, pAddress))
    {
        return pManager->ucCscConnIndex;
    }
    if ((BIKE_SENSOR_BLE_INVALID_CONN_INDEX != pManager->ucPowerConnIndex) &&
        BikeSensorBle_AddressEqual(&pManager->tPowerAddress, pAddress))
    {
        return pManager->ucPowerConnIndex;
    }

    return BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
}

/* BikeSensorBle_AssignServiceMask: 将服务位绑定到一个已建立连接。
 * 参数：
 *   - pManager: 连接管理状态
 *   - pAddress: 传感器地址
 *   - ucConnIndex: SDK 连接索引
 *   - ucServiceMask: 需要绑定的服务位
 * 返回值：无
 */
static void BikeSensorBle_AssignServiceMask(BIKE_SENSOR_BLE_MANAGER *pManager,
                                             const ble_gap_addr_t *pAddress,
                                             uint8_t ucConnIndex,
                                             uint8_t ucServiceMask)
{
    if ((NULL == pManager) || (NULL == pAddress))
    {
        return;
    }
    if (0U != (ucServiceMask & BIKE_BLE_SERVICE_HEART_RATE))
    {
        pManager->ucHeartRateConnIndex = ucConnIndex;
        pManager->tHeartRateAddress = *pAddress;
        pManager->bHeartRatePeerKnown = true;
    }
    if (0U != (ucServiceMask & BIKE_BLE_SERVICE_CSC))
    {
        pManager->ucCscConnIndex = ucConnIndex;
        pManager->tCscAddress = *pAddress;
        pManager->bCscPeerKnown = true;
    }
    if (0U != (ucServiceMask & BIKE_BLE_SERVICE_POWER))
    {
        pManager->ucPowerConnIndex = ucConnIndex;
        pManager->tPowerAddress = *pAddress;
        pManager->bPowerPeerKnown = true;
    }

    return;
}

/* BikeSensorBle_TryAttachExisting: 在同地址已有链路上增加传感器服务。
 * 参数：
 *   - pManager: 连接管理状态
 *   - pAddress: 目标地址
 *   - ucServiceMask: 待增加的单个服务位
 * 返回值：已处理同地址链路返回 true，否则返回 false
 */
static bool BikeSensorBle_TryAttachExisting(BIKE_SENSOR_BLE_MANAGER *pManager,
                                             const ble_gap_addr_t *pAddress,
                                             uint8_t ucServiceMask)
{
    uint8_t ucConnIndex;

    ucConnIndex = BikeSensorBle_GetConnectionForAddress(pManager, pAddress);
    if (BIKE_SENSOR_BLE_INVALID_CONN_INDEX == ucConnIndex)
    {
        return false;
    }
    BikeSensorBle_AssignServiceMask(pManager, pAddress, ucConnIndex,
                                    ucServiceMask);
    BikeSensorBle_SavePeer(pAddress, ucServiceMask);
    BikeSensorBle_PublishManager(pManager);
    if (!BIKE_BLE_GATT_Attach(ucConnIndex, ucServiceMask))
    {
        LOG_E("GATT add service failed conn=%u mask=0x%02x",
              ucConnIndex, ucServiceMask);
        connection_manager_disconnect(ucConnIndex);
    }

    return true;
}
#endif

#ifdef BSP_BLE_SIBLES
/* BikeSensorBle_HandleHeartRate: 提交项目 GATT 客户端已校验的心率通知。
 * 参数：
 *   - ucConnIndex: 通知所属连接
 *   - usHeartRateBpm: 心率，单位 bpm
 * 返回值：无
 */
static void BikeSensorBle_HandleHeartRate(uint8_t ucConnIndex,
                                          uint16_t usHeartRateBpm)
{
    if (!BikeSensorBle_Lock())
    {
        BikeSensorBle_CountDroppedEvent();
        return;
    }
    if (ucConnIndex != l_tBikeSensorSnapshot.ucHeartRateConnIndex)
    {
        BikeSensorBle_Unlock();
        return;
    }

    l_tBikeSensorSnapshot.usHeartRateBpm = usHeartRateBpm;
    l_tBikeSensorSnapshot.ulHeartRateUpdateMs =
        (uint32_t)rt_tick_get_millisecond();
    l_tBikeSensorSnapshot.bHeartRateValid = true;
    BikeSensorBle_Unlock();

    return;
}

/* BikeSensorBle_HandleCsc: 提交项目 GATT 客户端已校验的 CSC 通知。
 * 参数：
 *   - ucConnIndex: 通知所属连接
 *   - pMeasurement: 标准 CSC 累计测量值
 * 返回值：无
 */
static void BikeSensorBle_HandleCsc(
    uint8_t ucConnIndex, const BIKE_CSC_MEASUREMENT *pMeasurement)
{
    BIKE_SETTINGS_SNAPSHOT tSettings;

    if (NULL == pMeasurement)
    {
        return;
    }
    if (RT_EOK != BIKE_SETTINGS_GetSnapshot(&tSettings))
    {
        tSettings.usWheelCircumferenceMm = BIKE_SETTINGS_DEFAULT_WHEEL_MM;
    }
    if (!BikeSensorBle_Lock())
    {
        BikeSensorBle_CountDroppedEvent();
        return;
    }
    if (ucConnIndex != l_tBikeSensorSnapshot.ucCscConnIndex)
    {
        BikeSensorBle_Unlock();
        return;
    }
    BIKE_CSC_Update(&l_tBikeCscState, pMeasurement,
                    tSettings.usWheelCircumferenceMm);
    l_tBikeSensorSnapshot.bWheelSpeedValid = l_tBikeCscState.bWheelSpeedValid;
    l_tBikeSensorSnapshot.bCadenceValid = l_tBikeCscState.bCadenceValid;
    l_tBikeSensorSnapshot.usWheelSpeedCentiKph =
        l_tBikeCscState.usWheelSpeedCentiKph;
    l_tBikeSensorSnapshot.usCadenceRpm = l_tBikeCscState.usCadenceRpm;
    l_tBikeSensorSnapshot.ulCscUpdateMs = (uint32_t)rt_tick_get_millisecond();
    BikeSensorBle_Unlock();

    return;
}

/* BikeSensorBle_HandlePower: 提交项目 GATT 客户端已校验的功率通知。
 * 参数：
 *   - ucConnIndex: 通知所属连接
 *   - sPowerWatts: 瞬时功率，单位 W
 * 返回值：无
 */
static void BikeSensorBle_HandlePower(uint8_t ucConnIndex,
                                      int16_t sPowerWatts)
{
    if (!BikeSensorBle_Lock())
    {
        BikeSensorBle_CountDroppedEvent();
        return;
    }
    if (ucConnIndex != l_tBikeSensorSnapshot.ucPowerConnIndex)
    {
        BikeSensorBle_Unlock();
        return;
    }

    l_tBikeSensorSnapshot.sPowerWatts = sPowerWatts;
    l_tBikeSensorSnapshot.ulPowerUpdateMs =
        (uint32_t)rt_tick_get_millisecond();
    l_tBikeSensorSnapshot.bPowerValid = true;
    BikeSensorBle_Unlock();

    return;
}

/* BikeSensorBle_HandleBattery: 将 BAS 电量写入同一连接对应的传感器槽。
 * 参数：
 *   - ucConnIndex: 电量所属连接
 *   - ucBatteryPercent: 0~100 电量百分比
 * 返回值：无
 */
static void BikeSensorBle_HandleBattery(uint8_t ucConnIndex,
                                        uint8_t ucBatteryPercent)
{
    bool bMatched;

    if (100U < ucBatteryPercent)
    {
        return;
    }
    if (!BikeSensorBle_Lock())
    {
        BikeSensorBle_CountDroppedEvent();
        return;
    }
    bMatched = false;
    if (ucConnIndex == l_tBikeSensorSnapshot.ucHeartRateConnIndex)
    {
        l_tBikeSensorSnapshot.ucHeartRateBatteryPercent = ucBatteryPercent;
        l_tBikeSensorSnapshot.bHeartRateBatteryValid = true;
        bMatched = true;
    }
    if (ucConnIndex == l_tBikeSensorSnapshot.ucCscConnIndex)
    {
        l_tBikeSensorSnapshot.ucCscBatteryPercent = ucBatteryPercent;
        l_tBikeSensorSnapshot.bCscBatteryValid = true;
        bMatched = true;
    }
    if (ucConnIndex == l_tBikeSensorSnapshot.ucPowerConnIndex)
    {
        l_tBikeSensorSnapshot.ucPowerBatteryPercent = ucBatteryPercent;
        l_tBikeSensorSnapshot.bPowerBatteryValid = true;
        bMatched = true;
    }
    BikeSensorBle_Unlock();
    if (!bMatched)
    {
        LOG_W("battery from unknown conn=%u", ucConnIndex);
    }

    return;
}

/* BikeSensorBle_HandleGattReady: 将 GATT 订阅结果投递给连接管理线程。
 * 参数：
 *   - ucConnIndex: 服务所属连接
 *   - ucServiceMask: HR 或 CSC 服务位
 *   - bReady: 订阅是否成功
 * 返回值：无
 */
static void BikeSensorBle_HandleGattReady(uint8_t ucConnIndex,
                                          uint8_t ucServiceMask,
                                          bool bReady)
{
    BIKE_SENSOR_BLE_MESSAGE tMessage;

    (void)memset(&tMessage, 0, sizeof(tMessage));
    tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_GATT_READY;
    tMessage.ucConnIndex = ucConnIndex;
    tMessage.ucServiceMask = ucServiceMask;
    tMessage.ucStatus = bReady ? HL_ERR_NO_ERROR : 1U;
    if (!BikeSensorBle_PostMessage(&tMessage))
    {
        connection_manager_disconnect(ucConnIndex);
    }

    return;
}

/* BikeSensorBle_TryNextAction: 优先重连已知设备，否则扫描目标服务。
 * 参数：
 *   - pManager: 连接管理状态
 * 返回值：无
 */
static void BikeSensorBle_TryNextAction(BIKE_SENSOR_BLE_MANAGER *pManager)
{
    uint32_t ulNowMs;
    uint8_t ucMask;

    if ((NULL == pManager) || (!pManager->bPowerOn) || pManager->bScanning ||
        pManager->bConnecting)
    {
        return;
    }
    ucMask = BikeSensorBle_GetNeededMask(pManager);
    if (0U == ucMask)
    {
        return;
    }
    ulNowMs = (uint32_t)rt_tick_get_millisecond();
    if (!BikeSensorBle_TimeReached(ulNowMs, pManager->ulNextActionMs))
    {
        return;
    }

    if ((0U != (ucMask & BIKE_BLE_SERVICE_HEART_RATE)) &&
        pManager->bHeartRatePeerKnown &&
        BikeSensorBle_TryAttachExisting(pManager,
                                        &pManager->tHeartRateAddress,
                                        BIKE_BLE_SERVICE_HEART_RATE))
    {
        return;
    }
    if ((0U != (ucMask & BIKE_BLE_SERVICE_CSC)) &&
        pManager->bCscPeerKnown &&
        BikeSensorBle_TryAttachExisting(pManager, &pManager->tCscAddress,
                                        BIKE_BLE_SERVICE_CSC))
    {
        return;
    }
    if ((0U != (ucMask & BIKE_BLE_SERVICE_POWER)) &&
        pManager->bPowerPeerKnown &&
        BikeSensorBle_TryAttachExisting(pManager, &pManager->tPowerAddress,
                                        BIKE_BLE_SERVICE_POWER))
    {
        return;
    }

    if ((0U != (ucMask & BIKE_BLE_SERVICE_HEART_RATE)) &&
        pManager->bHeartRatePeerKnown &&
        (0U == (pManager->ucKnownAttemptMask & BIKE_BLE_SERVICE_HEART_RATE)))
    {
        ucMask = BIKE_BLE_SERVICE_HEART_RATE;
        if ((BIKE_SENSOR_BLE_INVALID_CONN_INDEX == pManager->ucCscConnIndex) &&
            pManager->bCscPeerKnown &&
            BikeSensorBle_AddressEqual(&pManager->tHeartRateAddress,
                                       &pManager->tCscAddress))
        {
            ucMask |= BIKE_BLE_SERVICE_CSC;
        }
        if ((BIKE_SENSOR_BLE_INVALID_CONN_INDEX ==
             pManager->ucPowerConnIndex) && pManager->bPowerPeerKnown &&
            BikeSensorBle_AddressEqual(&pManager->tHeartRateAddress,
                                       &pManager->tPowerAddress))
        {
            ucMask |= BIKE_BLE_SERVICE_POWER;
        }
        pManager->ucKnownAttemptMask |= ucMask;
        pManager->bPendingFromScan = false;
        (void)BikeSensorBle_StartConnection(pManager,
                                            &pManager->tHeartRateAddress,
                                            ucMask, -127);
        return;
    }
    if ((0U != (ucMask & BIKE_BLE_SERVICE_CSC)) && pManager->bCscPeerKnown &&
        (0U == (pManager->ucKnownAttemptMask & BIKE_BLE_SERVICE_CSC)))
    {
        ucMask = BIKE_BLE_SERVICE_CSC;
        if ((BIKE_SENSOR_BLE_INVALID_CONN_INDEX ==
             pManager->ucPowerConnIndex) && pManager->bPowerPeerKnown &&
            BikeSensorBle_AddressEqual(&pManager->tCscAddress,
                                       &pManager->tPowerAddress))
        {
            ucMask |= BIKE_BLE_SERVICE_POWER;
        }
        pManager->ucKnownAttemptMask |= ucMask;
        pManager->bPendingFromScan = false;
        (void)BikeSensorBle_StartConnection(pManager, &pManager->tCscAddress,
                                            ucMask, -127);
        return;
    }
    if ((0U != (ucMask & BIKE_BLE_SERVICE_POWER)) &&
        pManager->bPowerPeerKnown &&
        (0U == (pManager->ucKnownAttemptMask & BIKE_BLE_SERVICE_POWER)))
    {
        pManager->ucKnownAttemptMask |= BIKE_BLE_SERVICE_POWER;
        pManager->bPendingFromScan = false;
        (void)BikeSensorBle_StartConnection(pManager,
                                            &pManager->tPowerAddress,
                                            BIKE_BLE_SERVICE_POWER, -127);
        return;
    }

    if (pManager->bScanRequested)
    {
        pManager->bScanRequested = false;
        (void)BikeSensorBle_StartScan(pManager);
    }
    else
    {
        pManager->ucKnownAttemptMask = 0U;
        pManager->ulNextActionMs = ulNowMs + BIKE_SENSOR_BLE_RETRY_DELAY_MS;
    }
    return;
}

/* BikeSensorBle_AssignConnection: 记录已连接传感器并持久化地址。
 * 参数：
 *   - pManager: 连接管理状态
 *   - pMessage: 主设备角色连接事件
 * 返回值：无
 */
static void BikeSensorBle_AssignConnection(BIKE_SENSOR_BLE_MANAGER *pManager,
                                           const BIKE_SENSOR_BLE_MESSAGE *pMessage)
{
    uint8_t ucMask;
    uint8_t ucResult;

    if ((NULL == pManager) || (NULL == pMessage) || (!pManager->bConnecting))
    {
        return;
    }

    ucMask = pManager->ucPendingServiceMask & BikeSensorBle_GetNeededMask(pManager);
    BikeSensorBle_AssignServiceMask(pManager, &pMessage->tAddress,
                                    pMessage->ucConnIndex, ucMask);
    pManager->bConnecting = false;
    pManager->bCandidatePending = false;
    pManager->ucKnownAttemptMask &= (uint8_t)(~ucMask);
    if (pManager->bPendingFromScan &&
        (0U != BikeSensorBle_GetNeededMask(pManager)))
    {
        pManager->bScanRequested = true;
    }
    pManager->bPendingFromScan = false;
    pManager->ulNextActionMs = (uint32_t)rt_tick_get_millisecond();
    BikeSensorBle_SavePeer(&pMessage->tAddress, ucMask);
    BikeSensorBle_PublishManager(pManager);

    if (!BIKE_BLE_GATT_Attach(pMessage->ucConnIndex, ucMask))
    {
        LOG_E("GATT attach failed conn=%u mask=0x%02x",
              pMessage->ucConnIndex, ucMask);
        connection_manager_disconnect(pMessage->ucConnIndex);
        return;
    }
    ucResult = sibles_exchange_mtu(pMessage->ucConnIndex);
    if (HL_ERR_NO_ERROR != ucResult)
    {
        LOG_W("MTU exchange submit failed: %u", ucResult);
    }
    connection_manager_create_bond(pMessage->ucConnIndex);
    LOG_I("sensor connected conn=%u mask=0x%02x", pMessage->ucConnIndex,
          ucMask);

    return;
}

/* BikeSensorBle_HandleAdvertisement: 选取当前缺失服务的第一个强信号候选。
 * 参数：
 *   - pManager: 连接管理状态
 *   - pMessage: 已解析广播事件
 * 返回值：无
 */
static void BikeSensorBle_HandleAdvertisement(BIKE_SENSOR_BLE_MANAGER *pManager,
                                              const BIKE_SENSOR_BLE_MESSAGE *pMessage)
{
    uint8_t ucMask;
    uint8_t ucConnIndex;
    uint8_t ucResult;

    if ((NULL == pManager) || (NULL == pMessage) || (!pManager->bScanning) ||
        pManager->bCandidatePending ||
        (BIKE_SENSOR_BLE_MIN_RSSI_DBM > pMessage->cRssi))
    {
        return;
    }
    ucMask = pMessage->ucServiceMask & BikeSensorBle_GetNeededMask(pManager);
    if (0U == ucMask)
    {
        return;
    }

    ucConnIndex = BikeSensorBle_GetConnectionForAddress(
        pManager, &pMessage->tAddress);
    if (BIKE_SENSOR_BLE_INVALID_CONN_INDEX != ucConnIndex)
    {
        BikeSensorBle_AssignServiceMask(pManager, &pMessage->tAddress,
                                        ucConnIndex, ucMask);
        BikeSensorBle_SavePeer(&pMessage->tAddress, ucMask);
        BikeSensorBle_PublishManager(pManager);
        if (!BIKE_BLE_GATT_Attach(ucConnIndex, ucMask))
        {
            connection_manager_disconnect(ucConnIndex);
        }
        return;
    }

    pManager->tPendingAddress = pMessage->tAddress;
    pManager->ucPendingServiceMask = ucMask;
    pManager->cPendingRssi = pMessage->cRssi;
    pManager->bCandidatePending = true;
    pManager->bPendingFromScan = true;
    ucResult = ble_gap_scan_stop();
    if (HL_ERR_NO_ERROR != ucResult)
    {
        LOG_W("scan stop failed: %u", ucResult);
        pManager->bScanning = false;
        pManager->bCandidatePending = false;
        (void)BikeSensorBle_StartConnection(pManager, &pMessage->tAddress,
                                            ucMask, pMessage->cRssi);
    }

    return;
}

/* BikeSensorBle_HandleDisconnect: 清理对应传感器链路并安排重连。
 * 参数：
 *   - pManager: 连接管理状态
 *   - ucConnIndex: 已断开的 SDK 连接索引
 * 返回值：无
 */
static void BikeSensorBle_HandleDisconnect(BIKE_SENSOR_BLE_MANAGER *pManager,
                                           uint8_t ucConnIndex)
{
    uint8_t ucDisconnectedMask;

    if (NULL == pManager)
    {
        return;
    }
    ucDisconnectedMask = 0U;
    if (pManager->ucHeartRateConnIndex == ucConnIndex)
    {
        pManager->ucHeartRateConnIndex = BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
        ucDisconnectedMask |= BIKE_BLE_SERVICE_HEART_RATE;
    }
    if (pManager->ucCscConnIndex == ucConnIndex)
    {
        pManager->ucCscConnIndex = BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
        ucDisconnectedMask |= BIKE_BLE_SERVICE_CSC;
    }
    if (pManager->ucPowerConnIndex == ucConnIndex)
    {
        pManager->ucPowerConnIndex = BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
        ucDisconnectedMask |= BIKE_BLE_SERVICE_POWER;
    }
    if (0U == ucDisconnectedMask)
    {
        return;
    }
    BIKE_BLE_GATT_Detach(ucConnIndex);
    pManager->ucKnownAttemptMask &= (uint8_t)(~ucDisconnectedMask);
    pManager->ulNextActionMs = (uint32_t)rt_tick_get_millisecond() +
                               BIKE_SENSOR_BLE_RETRY_DELAY_MS;
    if (BikeSensorBle_LockForever())
    {
        if (0U != (ucDisconnectedMask & BIKE_BLE_SERVICE_HEART_RATE))
        {
            l_tBikeSensorSnapshot.bHeartRateValid = false;
            l_tBikeSensorSnapshot.bHeartRateBatteryValid = false;
            l_tBikeSensorSnapshot.usHeartRateBpm = 0U;
            l_tBikeSensorSnapshot.ucHeartRateBatteryPercent = 0U;
            l_tBikeSensorSnapshot.ulHeartRateUpdateMs = 0U;
        }
        if (0U != (ucDisconnectedMask & BIKE_BLE_SERVICE_CSC))
        {
            l_tBikeSensorSnapshot.bWheelSpeedValid = false;
            l_tBikeSensorSnapshot.bCadenceValid = false;
            l_tBikeSensorSnapshot.bCscBatteryValid = false;
            l_tBikeSensorSnapshot.usWheelSpeedCentiKph = 0U;
            l_tBikeSensorSnapshot.usCadenceRpm = 0U;
            l_tBikeSensorSnapshot.ucCscBatteryPercent = 0U;
            l_tBikeSensorSnapshot.ulCscUpdateMs = 0U;
            BIKE_CSC_Init(&l_tBikeCscState);
        }
        if (0U != (ucDisconnectedMask & BIKE_BLE_SERVICE_POWER))
        {
            l_tBikeSensorSnapshot.bPowerValid = false;
            l_tBikeSensorSnapshot.bPowerBatteryValid = false;
            l_tBikeSensorSnapshot.sPowerWatts = 0;
            l_tBikeSensorSnapshot.ucPowerBatteryPercent = 0U;
            l_tBikeSensorSnapshot.ulPowerUpdateMs = 0U;
        }
        BikeSensorBle_Unlock();
    }
    BikeSensorBle_PublishManager(pManager);
    LOG_W("sensor disconnected conn=%u mask=0x%02x", ucConnIndex,
          ucDisconnectedMask);

    return;
}

/* BikeSensorBle_ClearManagerPeers: 断开传感器并清除本模块保存的地址。
 * 参数：
 *   - pManager: 连接管理状态
 * 返回值：无
 */
static void BikeSensorBle_ClearManagerPeers(BIKE_SENSOR_BLE_MANAGER *pManager)
{
    uint8_t ucHeartRateIndex;
    uint8_t ucCscIndex;
    uint8_t ucPowerIndex;

    if (NULL == pManager)
    {
        return;
    }
    ucHeartRateIndex = pManager->ucHeartRateConnIndex;
    ucCscIndex = pManager->ucCscConnIndex;
    ucPowerIndex = pManager->ucPowerConnIndex;
    if (BIKE_SENSOR_BLE_INVALID_CONN_INDEX != ucHeartRateIndex)
    {
        BIKE_BLE_GATT_Detach(ucHeartRateIndex);
        connection_manager_disconnect(ucHeartRateIndex);
    }
    if ((BIKE_SENSOR_BLE_INVALID_CONN_INDEX != ucCscIndex) &&
        (ucHeartRateIndex != ucCscIndex))
    {
        BIKE_BLE_GATT_Detach(ucCscIndex);
        connection_manager_disconnect(ucCscIndex);
    }
    if ((BIKE_SENSOR_BLE_INVALID_CONN_INDEX != ucPowerIndex) &&
        (ucHeartRateIndex != ucPowerIndex) && (ucCscIndex != ucPowerIndex))
    {
        BIKE_BLE_GATT_Detach(ucPowerIndex);
        connection_manager_disconnect(ucPowerIndex);
    }
    pManager->ucHeartRateConnIndex = BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
    pManager->ucCscConnIndex = BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
    pManager->ucPowerConnIndex = BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
    pManager->bHeartRatePeerKnown = false;
    pManager->bCscPeerKnown = false;
    pManager->bPowerPeerKnown = false;
    pManager->ucKnownAttemptMask = 0U;
    pManager->ulNextActionMs = (uint32_t)rt_tick_get_millisecond();
    if (BikeSensorBle_LockForever())
    {
        l_tBikeSensorSnapshot.bHeartRateValid = false;
        l_tBikeSensorSnapshot.bWheelSpeedValid = false;
        l_tBikeSensorSnapshot.bCadenceValid = false;
        l_tBikeSensorSnapshot.bHeartRateBatteryValid = false;
        l_tBikeSensorSnapshot.bCscBatteryValid = false;
        l_tBikeSensorSnapshot.bPowerValid = false;
        l_tBikeSensorSnapshot.bPowerBatteryValid = false;
        l_tBikeSensorSnapshot.usHeartRateBpm = 0U;
        l_tBikeSensorSnapshot.usWheelSpeedCentiKph = 0U;
        l_tBikeSensorSnapshot.usCadenceRpm = 0U;
        l_tBikeSensorSnapshot.sPowerWatts = 0;
        l_tBikeSensorSnapshot.ucHeartRateBatteryPercent = 0U;
        l_tBikeSensorSnapshot.ucCscBatteryPercent = 0U;
        l_tBikeSensorSnapshot.ucPowerBatteryPercent = 0U;
        l_tBikeSensorSnapshot.ulHeartRateUpdateMs = 0U;
        l_tBikeSensorSnapshot.ulCscUpdateMs = 0U;
        l_tBikeSensorSnapshot.ulPowerUpdateMs = 0U;
        BIKE_CSC_Init(&l_tBikeCscState);
        BikeSensorBle_Unlock();
    }
    BikeSensorBle_RemovePeers();
    BikeSensorBle_PublishManager(pManager);
    LOG_I("sensor peers cleared");

    return;
}

/* BikeSensorBle_ProcessMessage: 串行处理扫描、连接和重连事件。
 * 参数：
 *   - pManager: 连接管理状态
 *   - pMessage: 固定事件
 * 返回值：无
 */
static void BikeSensorBle_ProcessMessage(BIKE_SENSOR_BLE_MANAGER *pManager,
                                         const BIKE_SENSOR_BLE_MESSAGE *pMessage)
{
    ble_gap_addr_t tPendingAddress;
    uint8_t ucPendingMask;
    int8_t cPendingRssi;

    if ((NULL == pManager) || (NULL == pMessage))
    {
        return;
    }

    switch (pMessage->eEvent)
    {
    case BIKE_SENSOR_BLE_EVENT_POWER_ON:
        if (!pManager->bPowerOn)
        {
            pManager->bPowerOn = true;
            pManager->ulNextActionMs = (uint32_t)rt_tick_get_millisecond();
            BikeSensorBle_LoadPeers(pManager);
            BikeSensorBle_PublishManager(pManager);
        }
        break;

    case BIKE_SENSOR_BLE_EVENT_ADVERTISEMENT:
        BikeSensorBle_HandleAdvertisement(pManager, pMessage);
        break;

    case BIKE_SENSOR_BLE_EVENT_SCAN_STARTED:
        if (HL_ERR_NO_ERROR != pMessage->ucStatus)
        {
            pManager->bScanning = false;
            pManager->ulNextActionMs = (uint32_t)rt_tick_get_millisecond() +
                                       BIKE_SENSOR_BLE_RETRY_DELAY_MS;
            BikeSensorBle_PublishManager(pManager);
        }
        break;

    case BIKE_SENSOR_BLE_EVENT_SCAN_STOPPED:
        pManager->bScanning = false;
        if (pManager->bCandidatePending)
        {
            tPendingAddress = pManager->tPendingAddress;
            ucPendingMask = pManager->ucPendingServiceMask;
            cPendingRssi = pManager->cPendingRssi;
            pManager->bCandidatePending = false;
            (void)BikeSensorBle_StartConnection(pManager, &tPendingAddress,
                                                ucPendingMask, cPendingRssi);
        }
        else
        {
            pManager->ucKnownAttemptMask = 0U;
            pManager->ulNextActionMs = (uint32_t)rt_tick_get_millisecond() +
                                       BIKE_SENSOR_BLE_RETRY_DELAY_MS;
            BikeSensorBle_PublishManager(pManager);
        }
        break;

    case BIKE_SENSOR_BLE_EVENT_CONNECT_RESULT:
        if (HL_ERR_NO_ERROR != pMessage->ucStatus)
        {
            pManager->bConnecting = false;
            if (pManager->bPendingFromScan)
            {
                pManager->bScanRequested = true;
                pManager->bPendingFromScan = false;
            }
            pManager->ulNextActionMs = (uint32_t)rt_tick_get_millisecond() +
                                       BIKE_SENSOR_BLE_RETRY_DELAY_MS;
            BikeSensorBle_PublishManager(pManager);
            LOG_W("sensor connect failed: %u", pMessage->ucStatus);
        }
        break;

    case BIKE_SENSOR_BLE_EVENT_CONNECTED:
        BikeSensorBle_AssignConnection(pManager, pMessage);
        break;

    case BIKE_SENSOR_BLE_EVENT_DISCONNECTED:
        BikeSensorBle_HandleDisconnect(pManager, pMessage->ucConnIndex);
        break;

    case BIKE_SENSOR_BLE_EVENT_GATT_READY:
        if (HL_ERR_NO_ERROR != pMessage->ucStatus)
        {
            LOG_E("GATT service failed conn=%u mask=0x%02x",
                  pMessage->ucConnIndex, pMessage->ucServiceMask);
            BIKE_BLE_GATT_Detach(pMessage->ucConnIndex);
            connection_manager_disconnect(pMessage->ucConnIndex);
        }
        else
        {
            LOG_I("GATT service ready conn=%u mask=0x%02x",
                  pMessage->ucConnIndex, pMessage->ucServiceMask);
        }
        break;

    case BIKE_SENSOR_BLE_EVENT_FORCE_SCAN:
        pManager->bScanRequested = true;
        pManager->ucKnownAttemptMask = BikeSensorBle_GetNeededMask(pManager);
        pManager->ulNextActionMs = (uint32_t)rt_tick_get_millisecond();
        break;

    case BIKE_SENSOR_BLE_EVENT_CLEAR_PEERS:
        BikeSensorBle_ClearManagerPeers(pManager);
        break;

    default:
        break;
    }

    return;
}

/* BikeSensorBle_ThreadEntry: 管理传感器扫描、配对和断线重连。
 * 参数：
 *   - pParameter: 未使用
 * 返回值：无
 */
static void BikeSensorBle_ThreadEntry(void *pParameter)
{
    BIKE_SENSOR_BLE_MESSAGE tMessage;
    rt_err_t eResult;
    rt_int32_t lWaitTicks;

    (void)pParameter;
    while (true)
    {
        lWaitTicks = BikeSensorBle_GetWaitTicks(&l_tBikeSensorManager);
        eResult = rt_mq_recv(&l_tBikeSensorQueue, &tMessage, sizeof(tMessage),
                             lWaitTicks);
        if (RT_EOK == eResult)
        {
            BikeSensorBle_ProcessMessage(&l_tBikeSensorManager, &tMessage);
        }
        else if (-RT_ETIMEOUT != eResult)
        {
            LOG_W("sensor queue receive failed: %d", eResult);
        }
        BikeSensorBle_TryNextAction(&l_tBikeSensorManager);
    }
}

/* BikeSensorBle_EventHandler: 接收连接管理和 GAP 事件。
 * 参数：
 *   - usEventId: BLE 事件编号
 *   - pData: 事件数据
 *   - usLength: 事件数据长度
 *   - ulContext: 未使用注册上下文
 * 返回值：0
 */
static int BikeSensorBle_EventHandler(uint16_t usEventId, uint8_t *pData,
                                      uint16_t usLength, uint32_t ulContext)
{
    BIKE_SENSOR_BLE_MESSAGE tMessage;

    (void)ulContext;
    (void)memset(&tMessage, 0, sizeof(tMessage));

    switch (usEventId)
    {
    case BLE_POWER_ON_IND:
        tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_POWER_ON;
        (void)BikeSensorBle_PostMessage(&tMessage);
        break;

    case BLE_GAP_SCAN_START_CNF:
        if ((NULL != pData) && (sizeof(ble_gap_start_scan_cnf_t) <= usLength))
        {
            const ble_gap_start_scan_cnf_t *pConfirm;

            pConfirm = (const ble_gap_start_scan_cnf_t *)pData;
            tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_SCAN_STARTED;
            tMessage.ucStatus = pConfirm->status;
            (void)BikeSensorBle_PostMessage(&tMessage);
        }
        else
        {
            BikeSensorBle_CountDroppedEvent();
        }
        break;

    case BLE_GAP_SCAN_STOPPED_IND:
        if ((NULL != pData) && (sizeof(ble_gap_scan_stopped_ind_t) <= usLength))
        {
            const ble_gap_scan_stopped_ind_t *pStopped;

            pStopped = (const ble_gap_scan_stopped_ind_t *)pData;
            tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_SCAN_STOPPED;
            tMessage.ucStatus = pStopped->reason;
            (void)BikeSensorBle_PostMessage(&tMessage);
        }
        else
        {
            BikeSensorBle_CountDroppedEvent();
        }
        break;

    case BLE_GAP_EXT_ADV_REPORT_IND:
        if ((NULL != pData) &&
            (offsetof(ble_gap_ext_adv_report_ind_t, data) <= usLength))
        {
            const ble_gap_ext_adv_report_ind_t *pReport;
            size_t ulRequiredLength;

            pReport = (const ble_gap_ext_adv_report_ind_t *)pData;
            ulRequiredLength = offsetof(ble_gap_ext_adv_report_ind_t, data) +
                               pReport->length;
            if ((ulRequiredLength <= usLength) &&
                (0U != (pReport->info & GAPM_REPORT_INFO_CONN_ADV_BIT)))
            {
                tMessage.ucServiceMask = BIKE_BLE_ADV_GetServiceMask(
                    pReport->data, pReport->length);
                if (0U != tMessage.ucServiceMask)
                {
                    tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_ADVERTISEMENT;
                    tMessage.tAddress = pReport->addr;
                    tMessage.cRssi = pReport->rssi;
                    (void)BikeSensorBle_PostMessage(&tMessage);
                }
            }
            else if (ulRequiredLength > usLength)
            {
                BikeSensorBle_CountDroppedEvent();
            }
        }
        else
        {
            BikeSensorBle_CountDroppedEvent();
        }
        break;

    case BLE_GAP_CREATE_CONNECTION_CNF:
        if ((NULL != pData) &&
            (sizeof(ble_gap_create_connection_cnf_t) <= usLength))
        {
            const ble_gap_create_connection_cnf_t *pConfirm;

            pConfirm = (const ble_gap_create_connection_cnf_t *)pData;
            tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_CONNECT_RESULT;
            tMessage.ucStatus = pConfirm->status;
            (void)BikeSensorBle_PostMessage(&tMessage);
        }
        else
        {
            BikeSensorBle_CountDroppedEvent();
        }
        break;

    case BLE_GAP_CREATE_CONNECTION_STOP_IND:
        if ((NULL != pData) &&
            (sizeof(ble_gap_create_connection_stop_ind_t) <= usLength))
        {
            const ble_gap_create_connection_stop_ind_t *pStopped;

            pStopped = (const ble_gap_create_connection_stop_ind_t *)pData;
            tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_CONNECT_RESULT;
            tMessage.ucStatus = pStopped->reason;
            (void)BikeSensorBle_PostMessage(&tMessage);
        }
        else
        {
            BikeSensorBle_CountDroppedEvent();
        }
        break;

    case CONNECTION_MANAGER_CONNCTED_IND:
        if ((NULL != pData) &&
            (sizeof(connection_manager_connect_ind_t) <= usLength))
        {
            const connection_manager_connect_ind_t *pConnected;

            pConnected = (const connection_manager_connect_ind_t *)pData;
            if (0U == pConnected->role)
            {
                tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_CONNECTED;
                tMessage.ucConnIndex = pConnected->conn_idx;
                tMessage.tAddress.addr_type = pConnected->peer_addr_type;
                tMessage.tAddress.addr = pConnected->peer_addr;
                (void)BikeSensorBle_PostMessage(&tMessage);
            }
        }
        else
        {
            BikeSensorBle_CountDroppedEvent();
        }
        break;

    case BLE_GAP_DISCONNECTED_IND:
        if ((NULL != pData) && (sizeof(ble_gap_disconnected_ind_t) <= usLength))
        {
            const ble_gap_disconnected_ind_t *pDisconnected;

            pDisconnected = (const ble_gap_disconnected_ind_t *)pData;
            tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_DISCONNECTED;
            tMessage.ucConnIndex = pDisconnected->conn_idx;
            tMessage.ucStatus = pDisconnected->reason;
            (void)BikeSensorBle_PostMessage(&tMessage);
        }
        else
        {
            BikeSensorBle_CountDroppedEvent();
        }
        break;

    default:
        break;
    }

    return 0;
}
BLE_EVENT_REGISTER(BikeSensorBle_EventHandler, NULL);
#endif

/* BIKE_SENSOR_BLE_Init: 初始化 BLE 骑行传感器固定容量快照。
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_SENSOR_BLE_Init(void)
{
    rt_err_t eResult;

    if (l_bBikeSensorReady)
    {
        return true;
    }
    (void)memset(&l_tBikeSensorSnapshot, 0, sizeof(l_tBikeSensorSnapshot));
    (void)memset(&l_tBikeSensorManager, 0, sizeof(l_tBikeSensorManager));
    l_tBikeSensorSnapshot.ucHeartRateConnIndex =
        BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
    l_tBikeSensorSnapshot.ucCscConnIndex = BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
    l_tBikeSensorSnapshot.ucPowerConnIndex = BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
    l_tBikeSensorSnapshot.cLastRssi = -127;
    l_tBikeSensorManager.ucHeartRateConnIndex =
        BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
    l_tBikeSensorManager.ucCscConnIndex = BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
    l_tBikeSensorManager.ucPowerConnIndex = BIKE_SENSOR_BLE_INVALID_CONN_INDEX;
    l_tBikeSensorManager.cPendingRssi = -127;
    BIKE_CSC_Init(&l_tBikeCscState);
    eResult = rt_mutex_init(&l_tBikeSensorMutex, "bike_ble", RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex init failed: %d", eResult);
        return false;
    }
    eResult = rt_mq_init(&l_tBikeSensorQueue, "bike_ble_q",
                         l_aBikeSensorQueuePool, sizeof(BIKE_SENSOR_BLE_MESSAGE),
                         sizeof(l_aBikeSensorQueuePool), RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        LOG_E("queue init failed: %d", eResult);
        (void)rt_mutex_detach(&l_tBikeSensorMutex);
        return false;
    }
#ifdef BSP_BLE_SIBLES
    if (!BIKE_BLE_GATT_Init(BikeSensorBle_HandleGattReady,
                            BikeSensorBle_HandleHeartRate,
                            BikeSensorBle_HandleCsc,
                            BikeSensorBle_HandlePower,
                            BikeSensorBle_HandleBattery))
    {
        LOG_E("GATT client init failed");
        (void)rt_mq_detach(&l_tBikeSensorQueue);
        (void)rt_mutex_detach(&l_tBikeSensorMutex);
        return false;
    }
    eResult = rt_thread_init(&l_tBikeSensorThread, "bike_ble",
                             BikeSensorBle_ThreadEntry, NULL,
                             l_aBikeSensorThreadStack,
                             sizeof(l_aBikeSensorThreadStack),
                             BIKE_SENSOR_BLE_THREAD_PRIORITY, 10U);
    if (RT_EOK != eResult)
    {
        LOG_E("thread init failed: %d", eResult);
        (void)rt_mq_detach(&l_tBikeSensorQueue);
        (void)rt_mutex_detach(&l_tBikeSensorMutex);
        return false;
    }
    l_bBikeSensorReady = true;
    eResult = rt_thread_startup(&l_tBikeSensorThread);
    if (RT_EOK != eResult)
    {
        LOG_E("thread startup failed: %d", eResult);
        l_bBikeSensorReady = false;
        (void)rt_thread_detach(&l_tBikeSensorThread);
        (void)rt_mq_detach(&l_tBikeSensorQueue);
        (void)rt_mutex_detach(&l_tBikeSensorMutex);
        return false;
    }
#else
    l_bBikeSensorReady = true;
#endif

    return true;
}
/* BikeSensorBle_AppInit: RT-Thread 应用初始化适配入口。
 * 返回值：成功返回 RT_EOK，否则返回 RT_ERROR
 */
static int BikeSensorBle_AppInit(void)
{
    return BIKE_SENSOR_BLE_Init() ? RT_EOK : RT_ERROR;
}
INIT_APP_EXPORT(BikeSensorBle_AppInit);

/* BIKE_SENSOR_BLE_GetSnapshot: 获取传感器快照并执行 5 秒数据超时。
 * 参数：
 *   - pSnapshot: 输出快照
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_SENSOR_BLE_GetSnapshot(BIKE_SENSOR_BLE_SNAPSHOT *pSnapshot)
{
    uint32_t ulNowMs;

    if ((NULL == pSnapshot) || (!BikeSensorBle_LockForever()))
    {
        return false;
    }
    *pSnapshot = l_tBikeSensorSnapshot;
    BikeSensorBle_Unlock();
    pSnapshot->ulDroppedEventCount = BikeSensorBle_GetDroppedEventCount();

    ulNowMs = (uint32_t)rt_tick_get_millisecond();
    if ((0U == pSnapshot->ulHeartRateUpdateMs) ||
        (BIKE_SENSOR_BLE_TIMEOUT_MS <
         (ulNowMs - pSnapshot->ulHeartRateUpdateMs)))
    {
        pSnapshot->bHeartRateValid = false;
    }
    if ((0U == pSnapshot->ulCscUpdateMs) ||
        (BIKE_SENSOR_BLE_TIMEOUT_MS < (ulNowMs - pSnapshot->ulCscUpdateMs)))
    {
        pSnapshot->bWheelSpeedValid = false;
        pSnapshot->bCadenceValid = false;
    }
    if ((0U == pSnapshot->ulPowerUpdateMs) ||
        (BIKE_SENSOR_BLE_TIMEOUT_MS <
         (ulNowMs - pSnapshot->ulPowerUpdateMs)))
    {
        pSnapshot->bPowerValid = false;
    }

    return true;
}

/* BIKE_SENSOR_BLE_RequestScan: 请求立即扫描未连接的骑行传感器。
 * 返回值：事件入队成功返回 true，否则返回 false
 */
bool BIKE_SENSOR_BLE_RequestScan(void)
{
#ifdef BSP_BLE_SIBLES
    BIKE_SENSOR_BLE_MESSAGE tMessage;

    (void)memset(&tMessage, 0, sizeof(tMessage));
    tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_FORCE_SCAN;
    return BikeSensorBle_PostMessage(&tMessage);
#else
    return false;
#endif
}

/* BIKE_SENSOR_BLE_ClearPeers: 请求断开传感器并清除本模块保存的地址。
 * 返回值：事件入队成功返回 true，否则返回 false
 */
bool BIKE_SENSOR_BLE_ClearPeers(void)
{
#ifdef BSP_BLE_SIBLES
    BIKE_SENSOR_BLE_MESSAGE tMessage;

    (void)memset(&tMessage, 0, sizeof(tMessage));
    tMessage.eEvent = BIKE_SENSOR_BLE_EVENT_CLEAR_PEERS;
    return BikeSensorBle_PostMessage(&tMessage);
#else
    return false;
#endif
}

/* BikeSensorBle_Command: 查询连接状态、触发扫描或清除传感器地址。
 * 参数：
 *   - lArgumentCount: shell 参数数量
 *   - pArguments: shell 参数数组
 * 返回值：无
 */
static void BikeSensorBle_Command(int lArgumentCount, char **pArguments)
{
    BIKE_SENSOR_BLE_SNAPSHOT tSnapshot;
    bool bResult;

    if ((2 > lArgumentCount) || (0 == strcmp(pArguments[1], "status")))
    {
        (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
        bResult = BIKE_SENSOR_BLE_GetSnapshot(&tSnapshot);
        rt_kprintf("bike sensor ret=%u power=%u scan=%u connecting=%u "
                   "hr=%u/%u/%u hb=%u/%u csc=%u/%u/%u rpm=%u cb=%u/%u "
                   "cp=%u/%u/%d pb=%u/%u attempts=%lu "
                   "rssi=%d dropped=%lu\n",
                   bResult, tSnapshot.bPowerOn, tSnapshot.bScanning,
                   tSnapshot.bConnecting, tSnapshot.bHeartRateConnected,
                   tSnapshot.ucHeartRateConnIndex, tSnapshot.usHeartRateBpm,
                   tSnapshot.bHeartRateBatteryValid,
                   tSnapshot.ucHeartRateBatteryPercent,
                   tSnapshot.bCscConnected, tSnapshot.ucCscConnIndex,
                   tSnapshot.usWheelSpeedCentiKph, tSnapshot.usCadenceRpm,
                   tSnapshot.bCscBatteryValid,
                   tSnapshot.ucCscBatteryPercent,
                   tSnapshot.bPowerConnected,
                   tSnapshot.ucPowerConnIndex,
                   tSnapshot.sPowerWatts,
                   tSnapshot.bPowerBatteryValid,
                   tSnapshot.ucPowerBatteryPercent,
                   (unsigned long)tSnapshot.ulConnectionAttemptCount,
                   tSnapshot.cLastRssi,
                   (unsigned long)tSnapshot.ulDroppedEventCount);
        rt_kprintf("usage: bikesensor status | scan | clear\n");
        return;
    }

    bResult = false;
    if ((2 == lArgumentCount) && (0 == strcmp(pArguments[1], "scan")))
    {
        bResult = BIKE_SENSOR_BLE_RequestScan();
    }
    else if ((2 == lArgumentCount) && (0 == strcmp(pArguments[1], "clear")))
    {
        bResult = BIKE_SENSOR_BLE_ClearPeers();
    }
    rt_kprintf("bike sensor command queued=%u\n", bResult);

    return;
}
MSH_CMD_EXPORT_ALIAS(BikeSensorBle_Command, bikesensor, bike BLE sensor manager);
