#include "bike_service.h"

#include <board.h>
#include <bf0_hal.h>
#include <drivers/rtc.h>
#include <rtdevice.h>
#include <rtthread.h>
#include <string.h>

#include "bike_settings.h"
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

/* l_bBikeRtcSynchronized: 防止每秒重复写 RTC 的一次性校时标志。 */
static bool l_bBikeRtcSynchronized;

static bool BikeService_Lock(void);
static void BikeService_Unlock(void);

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
    l_tBikeSnapshot.ulLastUpdateMs = ulNowMs;
    l_tBikeSnapshot.ulAcceptedCount = l_tBikeParser.ulAcceptedCount;
    l_tBikeSnapshot.ulChecksumErrorCount = l_tBikeParser.ulChecksumErrorCount;
    l_tBikeSnapshot.ulOverflowCount = l_tBikeParser.ulOverflowCount;
    if (BIKE_NMEA_RESULT_RMC == eResult)
    {
        BIKE_RIDE_Update(&l_tBikeSnapshot.tRide, pGnss, ulNowMs);
    }
    else
    {
        l_tBikeSnapshot.tRide.bFixValid = pGnss->bFixValid;
        l_tBikeSnapshot.tRide.ucSatellites = pGnss->ucSatellites;
        l_tBikeSnapshot.tRide.lAltitudeCm = pGnss->lAltitudeCm;
    }

    BikeService_Unlock();
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

    (void)pParameter;
    while (true)
    {
        (void)rt_sem_take(&l_tBikeRxSemaphore, rt_tick_from_millisecond(1000));
        do
        {
            ulReadLength = rt_device_read(l_pBikeUart, 0, aBuffer, sizeof(aBuffer));
            for (ulIndex = 0U; ulIndex < ulReadLength; ulIndex++)
            {
                eResult = BIKE_NMEA_Feed(&l_tBikeParser, aBuffer[ulIndex]);
                if ((BIKE_NMEA_RESULT_GGA == eResult) || (BIKE_NMEA_RESULT_RMC == eResult))
                {
                    ulNowMs = (uint32_t)rt_tick_get_millisecond();
                    BikeService_CommitParserData(eResult, ulNowMs);
                }
            }
        }
        while (0U < ulReadLength);

        BikeService_CheckStale((uint32_t)rt_tick_get_millisecond());
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

    (void)memset(&l_tBikeSnapshot, 0, sizeof(l_tBikeSnapshot));
    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        LOG_E("settings init failed: %d", eResult);
    }
    BIKE_NMEA_Init(&l_tBikeParser);
    BIKE_RIDE_Init(&l_tBikeSnapshot.tRide, BIKE_RIDE_DEFAULT_WEIGHT_KG);
    l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_SEARCHING;

    if (!BIKE_RECORDER_Init())
    {
        LOG_E("GPX recorder init failed");
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
        LOG_E("%s not found", BIKE_GNSS_UART_NAME);
        return RT_EOK;
    }

    tConfig = (struct serial_configure)RT_SERIAL_CONFIG_DEFAULT;
    tConfig.baud_rate = BIKE_GNSS_BAUD_RATE;
    eResult = rt_device_control(l_pBikeUart, RT_DEVICE_CTRL_CONFIG, &tConfig);
    if (RT_EOK != eResult)
    {
        l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_ERROR;
        LOG_E("uart config failed: %d", eResult);
        return RT_EOK;
    }

    eResult = rt_device_open(l_pBikeUart, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
    if (-RT_EIO == eResult)
    {
        eResult = rt_device_open(l_pBikeUart, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    }
    if (RT_EOK != eResult)
    {
        l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_ERROR;
        LOG_E("uart open failed: %d", eResult);
        return RT_EOK;
    }

    eResult = rt_device_set_rx_indicate(l_pBikeUart, BikeService_UartRxIndicate);
    if (RT_EOK != eResult)
    {
        (void)rt_device_close(l_pBikeUart);
        l_pBikeUart = NULL;
        l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_ERROR;
        LOG_E("uart callback failed: %d", eResult);
        return RT_EOK;
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
        (void)rt_device_set_rx_indicate(l_pBikeUart, NULL);
        (void)rt_device_close(l_pBikeUart);
        l_pBikeUart = NULL;
        l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_ERROR;
        LOG_E("thread start failed: %d", eResult);
        return RT_EOK;
    }

    l_tBikeSnapshot.ePortStatus = BIKE_GNSS_PORT_READY;
    LOG_I("DX-GP10 ready on %s at %u", BIKE_GNSS_UART_NAME, BIKE_GNSS_BAUD_RATE);

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
            if (BikeService_Lock())
            {
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
    bool bResult;

    bResult = false;
    if (BIKE_RECORDER_Pause() && BikeService_Lock())
    {
        BIKE_RIDE_Pause(&l_tBikeSnapshot.tRide);
        BikeService_Unlock();
        bResult = true;
    }

    return bResult;
}

/* BIKE_SERVICE_StopRide: 结束本次骑行并保留总结数据。
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_SERVICE_StopRide(void)
{
    bool bResult;

    bResult = false;
    if (BIKE_RECORDER_Stop() && BikeService_Lock())
    {
        BIKE_RIDE_Stop(&l_tBikeSnapshot.tRide);
        BikeService_Unlock();
        bResult = true;
    }

    return bResult;
}
