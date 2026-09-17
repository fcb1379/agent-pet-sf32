#include "bike_sensor_ble.h"

#include <stddef.h>
#include <string.h>

#include <rtthread.h>

#include "bike_settings.h"

#if defined(BSP_BLE_HRPC) || defined(BSP_BLE_CSCPC)
#include "bf0_ble_common.h"
#endif
#ifdef BSP_BLE_HRPC
#include "bf0_ble_hrpc.h"
#endif
#ifdef BSP_BLE_CSCPC
#include "bf0_ble_cscpc.h"
#endif

#define LOG_TAG "bike.sensor"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#define BIKE_SENSOR_BLE_TIMEOUT_MS (5000U)

/* l_tBikeSensorSnapshot: BLE 回调与 UI 共享的传感器快照。 */
static BIKE_SENSOR_BLE_SNAPSHOT l_tBikeSensorSnapshot;

/* l_tBikeCscState: BLE 回调独占的 CSC 累计值和回绕计算状态。 */
static BIKE_CSC_STATE l_tBikeCscState;

/* l_tBikeSensorMutex: 保护传感器快照的互斥锁。 */
static struct rt_mutex l_tBikeSensorMutex;

/* l_bBikeSensorReady: 互斥锁和固定容量状态已完成初始化。 */
static bool l_bBikeSensorReady;

/* l_ulBikeSensorDroppedEventCount: 锁竞争或事件长度异常的累计计数，
 * 范围 0~UINT32_MAX，达到上限后保持饱和。
 */
static uint32_t l_ulBikeSensorDroppedEventCount;

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

#ifdef BSP_BLE_HRPC
/* BikeSensorBle_HandleHeartRate: 提交 SDK 已解码的心率通知。
 * 参数：
 *   - pHeartRate: SDK 心率通知
 * 返回值：无
 */
static void BikeSensorBle_HandleHeartRate(const ble_hrpc_heart_rate_t *pHeartRate)
{
    if (NULL == pHeartRate)
    {
        return;
    }
    if (!BikeSensorBle_Lock())
    {
        BikeSensorBle_CountDroppedEvent();
        return;
    }

    l_tBikeSensorSnapshot.usHeartRateBpm = pHeartRate->heart_rate;
    l_tBikeSensorSnapshot.ulHeartRateUpdateMs =
        (uint32_t)rt_tick_get_millisecond();
    l_tBikeSensorSnapshot.bHeartRateValid = true;
    BikeSensorBle_Unlock();

    return;
}
#endif

#ifdef BSP_BLE_CSCPC
/* BikeSensorBle_HandleCsc: 提交 SDK 已解码的 CSC 通知并计算轮速/踏频。
 * 参数：
 *   - pCsc: SDK CSC 通知
 * 返回值：无
 */
static void BikeSensorBle_HandleCsc(const ble_csc_meas_value_ind *pCsc)
{
    BIKE_CSC_MEASUREMENT tMeasurement;
    BIKE_SETTINGS_SNAPSHOT tSettings;

    if (NULL == pCsc)
    {
        return;
    }
    (void)memset(&tMeasurement, 0, sizeof(tMeasurement));
    tMeasurement.ucFlags = pCsc->csc_meas.flags;
    tMeasurement.usCumulativeCrankRevolutions =
        pCsc->csc_meas.cumul_crank_rev;
    tMeasurement.usLastCrankEventTime = pCsc->csc_meas.last_crank_evt_time;
    tMeasurement.usLastWheelEventTime = pCsc->csc_meas.last_wheel_evt_time;
    tMeasurement.ulCumulativeWheelRevolutions =
        pCsc->csc_meas.cumul_wheel_rev;
    if (RT_EOK != BIKE_SETTINGS_GetSnapshot(&tSettings))
    {
        tSettings.usWheelCircumferenceMm = BIKE_SETTINGS_DEFAULT_WHEEL_MM;
    }
    BIKE_CSC_Update(&l_tBikeCscState, &tMeasurement,
                    tSettings.usWheelCircumferenceMm);

    if (!BikeSensorBle_Lock())
    {
        BikeSensorBle_CountDroppedEvent();
        return;
    }
    l_tBikeSensorSnapshot.bWheelSpeedValid = l_tBikeCscState.bWheelSpeedValid;
    l_tBikeSensorSnapshot.bCadenceValid = l_tBikeCscState.bCadenceValid;
    l_tBikeSensorSnapshot.usWheelSpeedCentiKph =
        l_tBikeCscState.usWheelSpeedCentiKph;
    l_tBikeSensorSnapshot.usCadenceRpm = l_tBikeCscState.usCadenceRpm;
    l_tBikeSensorSnapshot.ulCscUpdateMs = (uint32_t)rt_tick_get_millisecond();
    BikeSensorBle_Unlock();

    return;
}
#endif

#if defined(BSP_BLE_HRPC) || defined(BSP_BLE_CSCPC)
/* BikeSensorBle_EventHandler: 接收 SiFli HRPC/CSCPC 发布的已解码事件。
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
    (void)ulContext;

    switch (usEventId)
    {
    case BLE_POWER_ON_IND:
#ifdef BSP_BLE_HRPC
        ble_hrpc_init(true);
#endif
#ifdef BSP_BLE_CSCPC
        ble_cscpc_init(true);
#endif
        break;

#ifdef BSP_BLE_HRPC
    case BLE_HRPC_HREAT_RATE_NOTIFY:
        if (sizeof(ble_hrpc_heart_rate_t) <= usLength)
        {
            BikeSensorBle_HandleHeartRate((const ble_hrpc_heart_rate_t *)pData);
        }
        else
        {
            BikeSensorBle_CountDroppedEvent();
        }
        break;
#endif

#ifdef BSP_BLE_CSCPC
    case BLE_CSCPC_CSC_MEASUREMENT_NOTIFY:
        if (sizeof(ble_csc_meas_value_ind) <= usLength)
        {
            BikeSensorBle_HandleCsc((const ble_csc_meas_value_ind *)pData);
        }
        else
        {
            BikeSensorBle_CountDroppedEvent();
        }
        break;
#endif

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
    BIKE_CSC_Init(&l_tBikeCscState);
    eResult = rt_mutex_init(&l_tBikeSensorMutex, "bike_ble", RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex init failed: %d", eResult);
        return false;
    }
    l_bBikeSensorReady = true;

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

    if ((NULL == pSnapshot) || (!BikeSensorBle_Lock()))
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

    return true;
}
