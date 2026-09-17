#include "bike_service.h"

#include <board.h>
#include <bf0_hal.h>
#include <drivers/rtc.h>
#include <rtdevice.h>
#include <rtthread.h>
#include <string.h>

#include "bike_auto_pause.h"
#include "bike_compass.h"
#include "bike_history.h"
#include "bike_pedometer.h"
#include "bike_settings.h"
#include "bike_storage.h"
#include "bike_time.h"

#define LOG_TAG "bike.gnss"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#define BIKE_GNSS_UART_NAME "uart2"
#define BIKE_GNSS_BAUD_RATE (9600U)
#define BIKE_GNSS_RX_BUFFER_SIZE (64U)
#define BIKE_GNSS_THREAD_STACK_SIZE (3072U)
#define BIKE_GNSS_THREAD_PRIORITY (20U)
#define BIKE_GNSS_STALE_TIMEOUT_MS (3000U)
#define BIKE_RIDE_UPDATE_PERIOD_MS (1000U)
#define BIKE_HISTORY_CHECKPOINT_PERIOD_MS (60000U)
#define BIKE_DEMO_PERIOD_MS (1000U)
#define BIKE_DEMO_DAY_SECONDS (86400U)
#define BIKE_DEMO_MAX_SEQUENCE ((28U * BIKE_DEMO_DAY_SECONDS) - 1U)
#define BIKE_DEMO_LATITUDE_E7 (225430960)
#define BIKE_DEMO_LONGITUDE_E7 (1140578650)

/* l_tBikeSnapshot: 码表服务共享快照，只能在 l_tBikeMutex 保护下访问。 */
static BIKE_SERVICE_SNAPSHOT l_tBikeSnapshot;

/* l_tBikeParser: UART 接收线程独占的固定容量 NMEA 解析器。 */
static BIKE_NMEA_PARSER l_tBikeParser;

/* l_tBikeMutex: UI 与 GNSS 接收线程之间的快照互斥锁。 */
static struct rt_mutex l_tBikeMutex;

/* l_tBikeRxSemaphore: UART 接收回调唤醒解析线程的信号量。 */
static struct rt_semaphore l_tBikeRxSemaphore;

/* l_tBikeThread: 静态 GNSS 接收线程控制块。 */
static struct rt_thread l_tBikeThread;

/* l_aBikeThreadStack: GNSS 接收线程栈，固定 3072 字节。 */
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikeThreadStack[BIKE_GNSS_THREAD_STACK_SIZE];

/* l_pBikeUart: UART2 RT-Thread 设备句柄，初始化前为 NULL。 */
static rt_device_t l_pBikeUart;

/* l_bBikeServiceReady: 共享对象已经初始化的标志。 */
static bool l_bBikeServiceReady;

/* l_bBikeThreadReady: GNSS/模拟数据工作线程已经启动。 */
static bool l_bBikeThreadReady;

/* l_bBikeRtcSynchronized: 防止每秒重复写 RTC 的一次性校时标志。 */
static bool l_bBikeRtcSynchronized;

/* l_tBikeAutoPause: GNSS 线程独占的自动暂停防抖状态机。 */
static BIKE_AUTO_PAUSE_CONTROLLER l_tBikeAutoPause;

/* l_ulBikeLastRmcUpdateMs: 最近有效解析 RMC 的单调时间戳，范围
 * 0~UINT32_MAX，用于限制 GNSS 速度有效期并支持计数器自然回绕。
 */
static uint32_t l_ulBikeLastRmcUpdateMs;

/* l_bBikeHasRmcUpdate: 已收到至少一帧 RMC，避免时间戳回绕到 0 时误判。 */
static bool l_bBikeHasRmcUpdate;

/* l_ulBikeLastRideUpdateMs: 最近一次速度融合和骑行统计的单调时间戳，
 * 范围 0~UINT32_MAX，用于限制无新 RMC 时的周期更新频率。
 */
static uint32_t l_ulBikeLastRideUpdateMs;

/* l_ulBikeLastHistoryCheckpointMs: 最近一次累计骑行异步检查点时间。 */
static uint32_t l_ulBikeLastHistoryCheckpointMs;

/* l_bBikeHasRideUpdate: 已执行至少一次速度融合更新。 */
static bool l_bBikeHasRideUpdate;

/* l_ulBikeDemoSequence: 模拟定位秒序号，饱和在 28 天内以保持 UTC 单调。 */
static uint32_t l_ulBikeDemoSequence;

/* l_ulBikeLastDemoUpdateMs: 最近模拟定位发布时间。 */
static uint32_t l_ulBikeLastDemoUpdateMs;

static bool BikeService_Lock(void);
static void BikeService_Unlock(void);
static void BikeService_UpdateRide(uint32_t ulNowMs, bool bForce);

/* BikeService_SyncRtc: 首次有效 RMC 后按配置时区校准片上 RTC。
 * 参数：
 *   - pGnss: GNSS UTC 日期时间
 * 返回值：成功或已经同步返回 true，否则返回 false
 */
static bool BikeService_SyncRtc(const BIKE_GNSS_DATA *pGnss)
{
    BIKE_SETTINGS_SNAPSHOT tSettings;
    BIKE_LOCAL_TIME tLocalTime;
    rt_err_t eResult;

    if (l_bBikeRtcSynchronized)
    {
        return true;
    }
    eResult = BIKE_SETTINGS_GetSnapshot(&tSettings);
    if (RT_EOK != eResult)
    {
        LOG_E("settings read failed: %d", eResult);
        return false;
    }
    if (!BIKE_TIME_ConvertUtc(pGnss, tSettings.sTimeZoneMinutes, &tLocalTime))
    {
        return false;
    }

    eResult = set_date(tLocalTime.usYear, tLocalTime.ucMonth, tLocalTime.ucDay);
    if (RT_EOK == eResult)
    {
        eResult = set_time(tLocalTime.ucHour, tLocalTime.ucMinute, tLocalTime.ucSecond);
    }
    if (RT_EOK != eResult)
    {
        LOG_E("RTC sync failed: %d", eResult);
        return false;
    }

    l_bBikeRtcSynchronized = true;
    if (BikeService_Lock())
    {
        l_tBikeSnapshot.bRtcSynchronized = true;
        BikeService_Unlock();
    }
    LOG_I("RTC synchronized: %04u-%02u-%02u %02u:%02u:%02u UTC%+dmin",
          tLocalTime.usYear, tLocalTime.ucMonth, tLocalTime.ucDay,
          tLocalTime.ucHour, tLocalTime.ucMinute, tLocalTime.ucSecond,
          tSettings.sTimeZoneMinutes);

    return true;
}

/* BikeService_Lock: 获取共享快照互斥锁。
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeService_Lock(void)
{
    bool bResult;

    bResult = false;
    if (l_bBikeServiceReady && (RT_EOK == rt_mutex_take(&l_tBikeMutex, RT_WAITING_FOREVER)))
    {
        bResult = true;
    }

    return bResult;
}

/* BikeService_Unlock: 释放共享快照互斥锁。
 * 返回值：无
 */
static void BikeService_Unlock(void)
{
    rt_err_t eResult;

    eResult = rt_mutex_release(&l_tBikeMutex);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex release failed: %d", eResult);
    }

    return;
}

/* BikeService_UartRxIndicate: UART 接收中断回调，仅唤醒工作线程。
 * 参数：
 *   - pDevice: UART 设备
 *   - ulSize: 驱动报告的可读字节数
 * 返回值：RT_EOK
 */
static rt_err_t BikeService_UartRxIndicate(rt_device_t pDevice, rt_size_t ulSize)
{
    (void)pDevice;
    if (0U < ulSize)
    {
        return rt_sem_release(&l_tBikeRxSemaphore);
    }

    return RT_EOK;
}

/* BikeService_IsDemoMode: 读取模拟定位开关。
 * 返回值：模拟模式已启用返回 true
 */
static bool BikeService_IsDemoMode(void)
{
    bool bEnabled;

    bEnabled = false;
    if (BikeService_Lock())
    {
        bEnabled = l_tBikeSnapshot.bDemoMode;
        BikeService_Unlock();
    }

    return bEnabled;
}

/* BikeService_SetDemoMode: 切换无 GPS 模组的固定容量模拟定位。
 * 参数：
 *   - bEnabled: 是否启用
 * 返回值：更新成功返回 true
 */
static bool BikeService_SetDemoMode(bool bEnabled)
{
    uint32_t ulNowMs;

    if ((!l_bBikeThreadReady) || (!BikeService_Lock()))
    {
        return false;
    }
    ulNowMs = (uint32_t)rt_tick_get_millisecond();
    l_tBikeSnapshot.bDemoMode = bEnabled;
    l_ulBikeDemoSequence = 0U;
    l_ulBikeLastDemoUpdateMs = ulNowMs - BIKE_DEMO_PERIOD_MS;
    if (!bEnabled)
    {
        l_tBikeSnapshot.tGnss.bFixValid = false;
        l_tBikeSnapshot.tGnss.ulSpeedCmPerSec = 0U;
        l_tBikeSnapshot.ulLastUpdateMs = 0U;
        l_bBikeHasRmcUpdate = false;
        BIKE_RIDE_InvalidateFix(&l_tBikeSnapshot.tRide);
    }
    BikeService_Unlock();
    if (bEnabled)
    {
        (void)rt_sem_release(&l_tBikeRxSemaphore);
    }

    return true;
}

/* BikeService_PublishDemo: 每秒发布一个单调 UTC 的模拟定位点。
 * 参数：
 *   - ulNowMs: 当前单调时钟毫秒数
 * 返回值：无
 */
static void BikeService_PublishDemo(uint32_t ulNowMs)
{
    BIKE_GNSS_DATA tGnss;
    uint32_t ulDaySeconds;
    uint32_t ulSequence;

    if (!BikeService_Lock())
    {
        return;
    }
    if ((!l_tBikeSnapshot.bDemoMode) ||
        (BIKE_DEMO_PERIOD_MS > (ulNowMs - l_ulBikeLastDemoUpdateMs)))
    {
        BikeService_Unlock();
        return;
    }

    ulSequence = l_ulBikeDemoSequence;
    (void)memset(&tGnss, 0, sizeof(tGnss));
    ulDaySeconds = ulSequence % BIKE_DEMO_DAY_SECONDS;
    tGnss.bFixValid = true;
    tGnss.ucFixQuality = 1U;
    tGnss.ucSatellites = 12U;
    tGnss.usYear = 2026U;
    tGnss.ucMonth = 1U;
    tGnss.ucDay = (uint8_t)(1U + (ulSequence / BIKE_DEMO_DAY_SECONDS));
    tGnss.ucHour = (uint8_t)(ulDaySeconds / 3600U);
    tGnss.ucMinute = (uint8_t)((ulDaySeconds % 3600U) / 60U);
    tGnss.ucSecond = (uint8_t)(ulDaySeconds % 60U);
    tGnss.lLatitudeE7 = BIKE_DEMO_LATITUDE_E7 +
                        (int32_t)((ulSequence % 600U) * 20U);
    tGnss.lLongitudeE7 = BIKE_DEMO_LONGITUDE_E7 +
                         (int32_t)((ulSequence % 600U) * 20U);
    tGnss.lAltitudeCm = 4320 + (int32_t)((ulSequence % 20U) * 5U);
    tGnss.ulSpeedCmPerSec = 500U + ((ulSequence % 20U) * 25U);
    tGnss.usCourseDeg10 = (uint16_t)((ulSequence * 30U) % 3600U);
    l_tBikeSnapshot.tGnss = tGnss;
    l_tBikeSnapshot.ulLastUpdateMs = ulNowMs;
    l_ulBikeLastRmcUpdateMs = ulNowMs;
    l_bBikeHasRmcUpdate = true;
    l_ulBikeLastDemoUpdateMs = ulNowMs;
    if (BIKE_DEMO_MAX_SEQUENCE > l_ulBikeDemoSequence)
    {
        l_ulBikeDemoSequence++;
    }
    BikeService_Unlock();

    BikeService_UpdateRide(ulNowMs, true);
    (void)BIKE_RECORDER_SubmitPoint(&tGnss);

    return;
}

/* BikeService_UpdateRide: 每秒选择 CSC/GNSS 速度并更新骑行和自动暂停。
 * 参数：
 *   - ulNowMs: 当前单调时钟毫秒数
 *   - bForce: RMC/VTG 到达时强制立即更新
 * 返回值：无
 */
static void BikeService_UpdateRide(uint32_t ulNowMs, bool bForce)
{
    BIKE_SENSOR_BLE_SNAPSHOT tSensors;
    BIKE_SETTINGS_SNAPSHOT tSettings;
    BIKE_SPEED_INPUT tSpeedInput;
    BIKE_SPEED_SELECTION tSpeed;
    BIKE_AUTO_PAUSE_ACTION eAutoPauseAction;
    BIKE_RIDE_STATE tHistoryRide;
    rt_err_t eSettingsResult;
    bool bSensorResult;
    bool bHistoryCheckpoint;

    if ((!bForce) && l_bBikeHasRideUpdate &&
        (BIKE_RIDE_UPDATE_PERIOD_MS >
         (ulNowMs - l_ulBikeLastRideUpdateMs)))
    {
        return;
    }

    (void)memset(&tSensors, 0, sizeof(tSensors));
    (void)memset(&tSpeedInput, 0, sizeof(tSpeedInput));
    (void)memset(&tHistoryRide, 0, sizeof(tHistoryRide));
    bHistoryCheckpoint = false;
    eAutoPauseAction = BIKE_AUTO_PAUSE_ACTION_NONE;
    bSensorResult = BIKE_SENSOR_BLE_GetSnapshot(&tSensors);
    eSettingsResult = BIKE_SETTINGS_GetSnapshot(&tSettings);
    if (!BikeService_Lock())
    {
        return;
    }

    tSpeedInput.bGnssValid = l_tBikeSnapshot.tGnss.bFixValid &&
                             l_bBikeHasRmcUpdate &&
                             (BIKE_GNSS_STALE_TIMEOUT_MS >=
                              (ulNowMs - l_ulBikeLastRmcUpdateMs));
    tSpeedInput.ulGnssSpeedCmPerSec =
        l_tBikeSnapshot.tGnss.ulSpeedCmPerSec;
    tSpeedInput.bCscValid = bSensorResult &&
                           tSensors.bWheelSpeedValid;
    tSpeedInput.usCscSpeedCentiKph = tSensors.usWheelSpeedCentiKph;
    BIKE_SPEED_Select(&tSpeedInput, &tSpeed);
    BIKE_RIDE_UpdateWithSpeed(&l_tBikeSnapshot.tRide,
                              &l_tBikeSnapshot.tGnss, &tSpeed, ulNowMs);
    l_ulBikeLastRideUpdateMs = ulNowMs;
    l_bBikeHasRideUpdate = true;

    if (RT_EOK == eSettingsResult)
    {
        eAutoPauseAction = BIKE_AUTO_PAUSE_Update(
            &l_tBikeAutoPause, tSettings.bAutoPauseEnabled,
            l_tBikeSnapshot.bAutoPaused, l_tBikeSnapshot.tRide.eMode,
            tSpeed.bValid, l_tBikeSnapshot.tRide.usSpeedCentiKph,
            tSettings.usAutoPauseCentiKph, ulNowMs);
        if ((BIKE_AUTO_PAUSE_ACTION_PAUSE == eAutoPauseAction) &&
            BIKE_RECORDER_Pause())
        {
            LOG_I("auto paused at %u.%02u km/h source=%u",
                  l_tBikeSnapshot.tRide.usSpeedCentiKph / 100U,
                  l_tBikeSnapshot.tRide.usSpeedCentiKph % 100U,
                  tSpeed.eSource);
            BIKE_RIDE_Pause(&l_tBikeSnapshot.tRide);
            l_tBikeSnapshot.bAutoPaused = true;
        }
        else if ((BIKE_AUTO_PAUSE_ACTION_RESUME == eAutoPauseAction) &&
                 BIKE_RECORDER_Resume())
        {
            BIKE_RIDE_Start(&l_tBikeSnapshot.tRide, ulNowMs);
            l_tBikeSnapshot.bAutoPaused = false;
            LOG_I("auto resumed at %u.%02u km/h source=%u",
                  tSpeed.usSpeedCentiKph / 100U,
                  tSpeed.usSpeedCentiKph % 100U, tSpeed.eSource);
        }
    }
    if (((BIKE_RIDE_MODE_RUNNING == l_tBikeSnapshot.tRide.eMode) &&
         (BIKE_HISTORY_CHECKPOINT_PERIOD_MS <=
          (ulNowMs - l_ulBikeLastHistoryCheckpointMs))) ||
        (BIKE_AUTO_PAUSE_ACTION_PAUSE == eAutoPauseAction))
    {
        tHistoryRide = l_tBikeSnapshot.tRide;
        l_ulBikeLastHistoryCheckpointMs = ulNowMs;
        bHistoryCheckpoint = true;
    }
    BikeService_Unlock();
    if (bHistoryCheckpoint &&
        (!BIKE_HISTORY_RequestCheckpoint(&tHistoryRide)))
    {
        LOG_E("history checkpoint queue full");
    }

    return;
}

/* BikeService_CommitParserData: 将解析器数据提交到线程安全快照。
 * 参数：
 *   - eResult: 本次语句类型
 *   - ulNowMs: 当前单调时钟毫秒数
 * 返回值：无
 */
static void BikeService_CommitParserData(BIKE_NMEA_RESULT eResult, uint32_t ulNowMs)
{
    const BIKE_GNSS_DATA *pGnss;

    pGnss = BIKE_NMEA_GetData(&l_tBikeParser);
    if ((NULL == pGnss) || (!BikeService_Lock()))
    {
        return;
    }

    l_tBikeSnapshot.tGnss = *pGnss;
    if (BIKE_NMEA_RESULT_VTG != eResult)
    {
        l_tBikeSnapshot.ulLastUpdateMs = ulNowMs;
    }
    l_tBikeSnapshot.ulAcceptedCount = l_tBikeParser.ulAcceptedCount;
    l_tBikeSnapshot.ulChecksumErrorCount = l_tBikeParser.ulChecksumErrorCount;
    l_tBikeSnapshot.ulOverflowCount = l_tBikeParser.ulOverflowCount;
    if (BIKE_NMEA_RESULT_RMC == eResult)
    {
        l_ulBikeLastRmcUpdateMs = ulNowMs;
        l_bBikeHasRmcUpdate = true;
    }
    else
    {
        l_tBikeSnapshot.tRide.bFixValid = pGnss->bFixValid;
        l_tBikeSnapshot.tRide.ucSatellites = pGnss->ucSatellites;
        l_tBikeSnapshot.tRide.lAltitudeCm = pGnss->lAltitudeCm;
    }

    BikeService_Unlock();
    if ((BIKE_NMEA_RESULT_RMC == eResult) ||
        (BIKE_NMEA_RESULT_VTG == eResult))
    {
        BikeService_UpdateRide(ulNowMs, true);
    }
    if (BIKE_NMEA_RESULT_RMC == eResult)
    {
        (void)BikeService_SyncRtc(pGnss);
        (void)BIKE_RECORDER_SubmitPoint(pGnss);
    }

    return;
}

/* BikeService_CheckStale: 超过 3 秒无有效语句时清除实时定位状态。
 * 参数：
 *   - ulNowMs: 当前单调时钟毫秒数
 * 返回值：无
 */
static void BikeService_CheckStale(uint32_t ulNowMs)
{
    if (!BikeService_Lock())
    {
        return;
    }

    if ((0U != l_tBikeSnapshot.ulLastUpdateMs) &&
            (BIKE_GNSS_STALE_TIMEOUT_MS < (ulNowMs - l_tBikeSnapshot.ulLastUpdateMs)))
    {
        l_tBikeSnapshot.tGnss.bFixValid = false;
        l_tBikeSnapshot.tGnss.ulSpeedCmPerSec = 0U;
        BIKE_RIDE_InvalidateFix(&l_tBikeSnapshot.tRide);
    }

    BikeService_Unlock();
    BikeService_UpdateRide(ulNowMs, false);

    return;
}

/* BikeService_ThreadEntry: 读取 UART2 并逐字节解析 DX-GP10 NMEA。
 * 参数：
 *   - pParameter: 未使用
 * 返回值：无
 */
static void BikeService_ThreadEntry(void *pParameter)
{
    uint8_t aBuffer[BIKE_GNSS_RX_BUFFER_SIZE];
    rt_size_t ulReadLength;
    uint32_t ulIndex;
    uint32_t ulNowMs;
    BIKE_NMEA_RESULT eResult;
    bool bDemoMode;

    (void)pParameter;
    while (true)
    {
        (void)rt_sem_take(&l_tBikeRxSemaphore, rt_tick_from_millisecond(1000));
        bDemoMode = BikeService_IsDemoMode();
        if (NULL != l_pBikeUart)
        {
            do
            {
                ulReadLength = rt_device_read(l_pBikeUart, 0, aBuffer,
                                              sizeof(aBuffer));
                for (ulIndex = 0U; ulIndex < ulReadLength; ulIndex++)
                {
                    eResult = BIKE_NMEA_Feed(&l_tBikeParser,
                                             aBuffer[ulIndex]);
                    if ((!bDemoMode) &&
                        ((BIKE_NMEA_RESULT_GGA == eResult) ||
                         (BIKE_NMEA_RESULT_RMC == eResult) ||
                         (BIKE_NMEA_RESULT_VTG == eResult)))
                    {
                        ulNowMs = (uint32_t)rt_tick_get_millisecond();
                        BikeService_CommitParserData(eResult, ulNowMs);
                    }
                }
            }
            while (0U < ulReadLength);
        }

        ulNowMs = (uint32_t)rt_tick_get_millisecond();
        if (bDemoMode)
        {
            BikeService_PublishDemo(ulNowMs);
        }
        else
        {
            BikeService_CheckStale(ulNowMs);
        }
    }

    return;
}

/* BikeService_ConfigurePins: 配置 SF32LB52 UART2 默认引脚。
 * 参数：无
 * 返回值：无
 */
static void BikeService_ConfigurePins(void)
{
#if defined(SOC_SF32LB52X) || defined(SF32LB52X)
    HAL_PIN_Set(PAD_PA20, USART2_RXD, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA27, USART2_TXD, PIN_PULLUP, 1);
#endif

    return;
}

/* BikeService_Init: 初始化固定容量状态、UART2 和 GNSS 接收线程。
 * 返回值：服务对象始终可供 UI 查询；硬件错误通过端口状态报告
 */
static int BikeService_Init(void)
{
    struct serial_configure tConfig;
    rt_err_t eResult;
    bool bUartOpened;

    (void)memset(&l_tBikeSnapshot, 0, sizeof(l_tBikeSnapshot));
    bUartOpened = false;
    l_ulBikeLastRmcUpdateMs = 0U;
    l_ulBikeLastRideUpdateMs = 0U;
    l_ulBikeLastHistoryCheckpointMs = 0U;
    l_ulBikeDemoSequence = 0U;
    l_ulBikeLastDemoUpdateMs = 0U;
    l_bBikeHasRmcUpdate = false;
    l_bBikeHasRideUpdate = false;
    l_bBikeThreadReady = false;
    BIKE_AUTO_PAUSE_Init(&l_tBikeAutoPause);
    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        LOG_E("settings init failed: %d", eResult);
    }
    eResult = BIKE_HISTORY_Init();
    if (RT_EOK != eResult)
    {
        LOG_E("history init failed: %d", eResult);
    }
    BIKE_NMEA_Init(&l_tBikeParser);
    BIKE_RIDE_Init(&l_tBikeSnapshot.tRide, BIKE_RIDE_DEFAULT_WEIGHT_KG);
    l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_SEARCHING;

    if (!BIKE_STORAGE_Init())
    {
        LOG_E("storage init failed");
    }

    if (!BIKE_RECORDER_Init())
    {
        LOG_E("GPX recorder init failed");
    }
    if (!BIKE_PEDOMETER_Init())
    {
        LOG_E("onboard pedometer init failed");
    }
    if (!BIKE_COMPASS_Init())
    {
        LOG_E("onboard compass init failed");
    }

    eResult = rt_mutex_init(&l_tBikeMutex, "bike", RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex init failed: %d", eResult);
        return RT_ERROR;
    }
    l_bBikeServiceReady = true;

    eResult = rt_sem_init(&l_tBikeRxSemaphore, "bike_rx", 0U, RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_ERROR;
        LOG_E("semaphore init failed: %d", eResult);
        return RT_EOK;
    }

    BikeService_ConfigurePins();
    l_pBikeUart = rt_device_find(BIKE_GNSS_UART_NAME);
    if (NULL == l_pBikeUart)
    {
        l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_ERROR;
        LOG_E("%s not found; demo mode remains available",
              BIKE_GNSS_UART_NAME);
    }
    else
    {
        tConfig = (struct serial_configure)RT_SERIAL_CONFIG_DEFAULT;
        tConfig.baud_rate = BIKE_GNSS_BAUD_RATE;
        eResult = rt_device_control(l_pBikeUart, RT_DEVICE_CTRL_CONFIG,
                                    &tConfig);
        if (RT_EOK == eResult)
        {
            eResult = rt_device_open(l_pBikeUart,
                                     RT_DEVICE_OFLAG_RDWR |
                                     RT_DEVICE_FLAG_DMA_RX);
            if (-RT_EIO == eResult)
            {
                eResult = rt_device_open(l_pBikeUart,
                                         RT_DEVICE_OFLAG_RDWR |
                                         RT_DEVICE_FLAG_INT_RX);
            }
        }
        if (RT_EOK == eResult)
        {
            bUartOpened = true;
            eResult = rt_device_set_rx_indicate(l_pBikeUart,
                                                BikeService_UartRxIndicate);
        }
        if (RT_EOK != eResult)
        {
            if (bUartOpened)
            {
                (void)rt_device_close(l_pBikeUart);
            }
            l_pBikeUart = NULL;
            l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_ERROR;
            LOG_E("uart init failed: %d; demo mode remains available",
                  eResult);
        }
        else
        {
            l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_READY;
        }
    }

    eResult = rt_thread_init(&l_tBikeThread, "bike_gnss", BikeService_ThreadEntry, NULL,
                             l_aBikeThreadStack, sizeof(l_aBikeThreadStack),
                             BIKE_GNSS_THREAD_PRIORITY, 10U);
    if (RT_EOK == eResult)
    {
        eResult = rt_thread_startup(&l_tBikeThread);
    }
    if (RT_EOK != eResult)
    {
        if (NULL != l_pBikeUart)
        {
            (void)rt_device_set_rx_indicate(l_pBikeUart, NULL);
            (void)rt_device_close(l_pBikeUart);
            l_pBikeUart = NULL;
        }
        l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_ERROR;
        LOG_E("thread start failed: %d", eResult);
        return RT_EOK;
    }
    l_bBikeThreadReady = true;

    if (NULL != l_pBikeUart)
    {
        LOG_I("DX-GP10 ready on %s at %u", BIKE_GNSS_UART_NAME,
              BIKE_GNSS_BAUD_RATE);
    }

    return RT_EOK;
}
INIT_APP_EXPORT(BikeService_Init);

/* BIKE_SERVICE_GetSnapshot: 获取供 UI 使用的一致性快照。
 * 参数：
 *   - pSnapshot: 输出快照
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_SERVICE_GetSnapshot(BIKE_SERVICE_SNAPSHOT *pSnapshot)
{
    bool bResult;

    bResult = false;
    if ((NULL != pSnapshot) && BikeService_Lock())
    {
        *pSnapshot = l_tBikeSnapshot;
        BikeService_Unlock();
        (void)BIKE_RECORDER_GetSnapshot(&pSnapshot->tRecorder);
        (void)BIKE_SENSOR_BLE_GetSnapshot(&pSnapshot->tSensors);
        (void)BIKE_COMPASS_GetSnapshot(&pSnapshot->tCompass);
        (void)BIKE_PEDOMETER_GetSnapshot(&pSnapshot->tPedometer);
        (void)BIKE_HISTORY_GetSnapshot(&pSnapshot->tHistory);
        bResult = true;
    }

    return bResult;
}

/* BIKE_SERVICE_StartRide: 开始或继续本次骑行。
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_SERVICE_StartRide(void)
{
    bool bResult;
    BIKE_RIDE_MODE ePreviousMode;

    bResult = false;
    if (BikeService_Lock())
    {
        ePreviousMode = l_tBikeSnapshot.tRide.eMode;
        BikeService_Unlock();
        if (BIKE_RIDE_MODE_PAUSED == ePreviousMode)
        {
            bResult = BIKE_RECORDER_Resume();
        }
        else
        {
            bResult = BIKE_RECORDER_Start();
        }
        if (bResult)
        {
            if ((BIKE_RIDE_MODE_STOPPED == ePreviousMode) &&
                (!BIKE_HISTORY_RequestBeginRide()))
            {
                LOG_E("history begin queue full");
            }
            if (BikeService_Lock())
            {
                BIKE_AUTO_PAUSE_Init(&l_tBikeAutoPause);
                l_tBikeSnapshot.bAutoPaused = false;
                l_ulBikeLastHistoryCheckpointMs =
                    (uint32_t)rt_tick_get_millisecond();
                BIKE_RIDE_Start(&l_tBikeSnapshot.tRide,
                                (uint32_t)rt_tick_get_millisecond());
                BikeService_Unlock();
            }
            else
            {
                bResult = false;
            }
        }
    }

    return bResult;
}

/* BIKE_SERVICE_PauseRide: 暂停本次骑行。
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_SERVICE_PauseRide(void)
{
    BIKE_RIDE_STATE tHistoryRide;
    bool bResult;
    bool bHistoryQueued;

    bResult = false;
    if (BIKE_RECORDER_Pause() && BikeService_Lock())
    {
        BIKE_AUTO_PAUSE_Init(&l_tBikeAutoPause);
        l_tBikeSnapshot.bAutoPaused = false;
        BIKE_RIDE_Pause(&l_tBikeSnapshot.tRide);
        tHistoryRide = l_tBikeSnapshot.tRide;
        BikeService_Unlock();
        bHistoryQueued = BIKE_HISTORY_RequestCheckpoint(&tHistoryRide);
        if (!bHistoryQueued)
        {
            LOG_E("history pause checkpoint queue full");
        }
        bResult = true;
    }

    return bResult;
}

/* BIKE_SERVICE_StopRide: 结束本次骑行并保留总结数据。
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_SERVICE_StopRide(void)
{
    BIKE_RIDE_STATE tHistoryRide;
    bool bResult;
    bool bHistoryQueued;

    bResult = false;
    if (BIKE_RECORDER_Stop() && BikeService_Lock())
    {
        BIKE_AUTO_PAUSE_Init(&l_tBikeAutoPause);
        l_tBikeSnapshot.bAutoPaused = false;
        BIKE_RIDE_Stop(&l_tBikeSnapshot.tRide);
        tHistoryRide = l_tBikeSnapshot.tRide;
        BikeService_Unlock();
        bHistoryQueued = BIKE_HISTORY_RequestFinish(&tHistoryRide);
        if (!bHistoryQueued)
        {
            LOG_E("history finish queue full");
        }
        bResult = true;
    }

    return bResult;
}

/* BIKE_SERVICE_DiscardRide: 结束骑行并删除当前未发布轨迹。
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_SERVICE_DiscardRide(void)
{
    bool bResult;
    bool bHistoryQueued;

    bResult = false;
    if (BIKE_RECORDER_Discard() && BikeService_Lock())
    {
        BIKE_AUTO_PAUSE_Init(&l_tBikeAutoPause);
        l_tBikeSnapshot.bAutoPaused = false;
        BIKE_RIDE_Stop(&l_tBikeSnapshot.tRide);
        BikeService_Unlock();
        bHistoryQueued = BIKE_HISTORY_RequestDiscard();
        if (!bHistoryQueued)
        {
            LOG_E("history discard queue full");
        }
        bResult = true;
    }

    return bResult;
}

/* BikeService_DemoCommand: 控制无 GNSS 模组时的模拟定位数据。
 * 参数：
 *   - lArgumentCount: shell 参数数量
 *   - pArguments: shell 参数数组
 * 返回值：无
 */
static void BikeService_DemoCommand(int lArgumentCount, char **pArguments)
{
    bool bResult;

    if (!l_bBikeServiceReady)
    {
        rt_kprintf("bike demo service not ready\n");
        return;
    }
    if ((2 > lArgumentCount) || (0 == strcmp(pArguments[1], "status")))
    {
        rt_kprintf("bike demo=%u\n", BikeService_IsDemoMode());
        rt_kprintf("usage: bikedemo on | off | status\n");
        return;
    }

    bResult = false;
    if ((2 == lArgumentCount) && (0 == strcmp(pArguments[1], "on")))
    {
        bResult = BikeService_SetDemoMode(true);
    }
    else if ((2 == lArgumentCount) && (0 == strcmp(pArguments[1], "off")))
    {
        bResult = BikeService_SetDemoMode(false);
    }
    rt_kprintf("bike demo update=%u\n", bResult);

    return;
}
MSH_CMD_EXPORT_ALIAS(BikeService_DemoCommand, bikedemo,
                     bike GNSS demo mode);
