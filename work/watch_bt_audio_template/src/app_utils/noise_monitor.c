#include "noise_monitor.h"

#include <math.h>
#include <stdint.h>

#ifndef BSP_USING_PC_SIMULATOR
    #include "audio_server.h"
#endif

#define NOISEMONITOR_SAMPLE_RATE_HZ             (16000U)
#define NOISEMONITOR_BITS_PER_SAMPLE            (16U)
#define NOISEMONITOR_CHANNEL_COUNT              (1U)
#define NOISEMONITOR_AUDIO_CACHE_BYTES          (2048U)
#define NOISEMONITOR_THREAD_STACK_BYTES         (2048U)
#define NOISEMONITOR_THREAD_PRIORITY            (18U)
#define NOISEMONITOR_THREAD_TICK                 (10U)
#define NOISEMONITOR_WINDOW_MS                   (1000U)
#define NOISEMONITOR_MIN_WINDOW_SAMPLES          (8000U)
#define NOISEMONITOR_MAX_WINDOW_SAMPLES          (NOISEMONITOR_SAMPLE_RATE_HZ * 2U)
#define NOISEMONITOR_MIN_DB                      (30.0f)
#define NOISEMONITOR_MAX_DB                      (120.0f)
#define NOISEMONITOR_PCM_FULL_SCALE              (32768.0f)
/* 初始通用偏移为 120 dB；板上两轮声级计对比后，当前校准偏移修正为
 * 108 dB。不同麦克风、结构和增益会改变该值，量产前仍需使用声级计和
 * 校准音源复核多个声压点。 */
#define NOISEMONITOR_SPL_CALIBRATION_DB          (108.0f)
#define NOISEMONITOR_EVENT_START                 (1UL << 0)
#define NOISEMONITOR_EVENT_STOP                  (1UL << 1)

/* l_tSnapshotMutex: 快照互斥锁，仅在线程上下文使用，用于保护 l_tSnapshot。 */
static struct rt_mutex l_tSnapshotMutex;
/* l_tSnapshot: 最近一次噪声测量结果，范围由 NOISE_MONITOR_SNAPSHOT 定义，UI 只能通过接口复制读取。 */
static NOISE_MONITOR_SNAPSHOT l_tSnapshot;
/* l_bServiceReady: 服务初始化完成标志，false/true；接口调用前用于拒绝未就绪请求。 */
static bool l_bServiceReady;

#ifndef BSP_USING_PC_SIMULATOR
/* l_tControlEvent: 采集线程控制事件，仅承载 START/STOP 位，不传输 PCM 数据。 */
static struct rt_event l_tControlEvent;
/* l_tWorkerThread: 噪声计算线程控制块，负责音频开关和每秒一次的浮点计算。 */
static struct rt_thread l_tWorkerThread;
/* l_aWorkerStack: 噪声计算线程静态栈，固定 2048 字节，避免运行时动态分配。 */
static uint8_t l_aWorkerStack[NOISEMONITOR_THREAD_STACK_BYTES];
/* l_pAudioClient: Audio Manager 录音句柄，NULL 表示麦克风链路未打开，仅工作线程负责开关。 */
static audio_client_t l_pAudioClient;
/* l_bRunRequested: UI 请求的运行状态，false/true；接口与工作线程通过短临界区访问。 */
static volatile bool l_bRunRequested;
/* l_bAudioSuspended: Audio Manager 挂起状态，false/true；由音频回调更新、工作线程读取。 */
static volatile bool l_bAudioSuspended;
/* l_dSampleSum: 当前窗口 PCM 有符号样本和，用于去除直流分量，范围受最大窗口样本数限制。 */
static volatile int64_t l_dSampleSum;
/* l_udSampleSquareSum: 当前窗口 PCM 平方和，用于 RMS 计算，最大值小于 3.5e13。 */
static volatile uint64_t l_udSampleSquareSum;
/* l_ulSampleCount: 当前统计窗口样本数，范围 0~NOISEMONITOR_MAX_WINDOW_SAMPLES。 */
static volatile uint32_t l_ulSampleCount;
#endif

/***************************
 * NoiseMonitor_UpdateSnapshot: 在线程上下文更新对外快照。
 * 参数：
 *   - bRunning: 采集请求状态。
 *   - bValid: 分贝值是否有效。
 *   - ucDb: 估算声压级，范围 30~120 dB。
 *   - eState: 当前服务状态。
 *   - lError: 最近错误码。
 *   - ulSampleCount: 当前测量使用的样本数。
 * 返回值：无。
 ***************************/
static void NoiseMonitor_UpdateSnapshot(bool bRunning,
                                        bool bValid,
                                        uint8_t ucDb,
                                        NOISE_MONITOR_STATE eState,
                                        int32_t lError,
                                        uint32_t ulSampleCount)
{
    rt_err_t tResult;

    tResult = rt_mutex_take(&l_tSnapshotMutex, RT_WAITING_FOREVER);
    if (RT_EOK == tResult)
    {
        l_tSnapshot.bRunning = bRunning;
        l_tSnapshot.bValid = bValid;
        l_tSnapshot.ucDb = ucDb;
        l_tSnapshot.eState = eState;
        l_tSnapshot.lLastError = lError;
        l_tSnapshot.ulSampleCount = ulSampleCount;
        if (true == bValid)
        {
            l_tSnapshot.ulGeneration++;
        }
        (void)rt_mutex_release(&l_tSnapshotMutex);
    }

    return;
}

#ifndef BSP_USING_PC_SIMULATOR
/***************************
 * NoiseMonitor_ResetAccumulator: 清空当前 PCM 能量统计窗口。
 * 参数：无。
 * 返回值：无。
 ***************************/
static void NoiseMonitor_ResetAccumulator(void)
{
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    l_dSampleSum = 0;
    l_udSampleSquareSum = 0U;
    l_ulSampleCount = 0U;
    rt_hw_interrupt_enable(tLevel);

    return;
}

/***************************
 * NoiseMonitor_TakeAccumulator: 原子取出并清空当前 PCM 能量统计窗口。
 * 参数：
 *   - pSampleSum: 输出有符号样本和。
 *   - pSampleSquareSum: 输出样本平方和。
 *   - pSampleCount: 输出样本数。
 * 返回值：无。
 ***************************/
static void NoiseMonitor_TakeAccumulator(int64_t *pSampleSum,
                                         uint64_t *pSampleSquareSum,
                                         uint32_t *pSampleCount)
{
    rt_base_t tLevel;

    if ((NULL == pSampleSum) || (NULL == pSampleSquareSum) ||
            (NULL == pSampleCount))
    {
        return;
    }

    tLevel = rt_hw_interrupt_disable();
    *pSampleSum = l_dSampleSum;
    *pSampleSquareSum = l_udSampleSquareSum;
    *pSampleCount = l_ulSampleCount;
    l_dSampleSum = 0;
    l_udSampleSquareSum = 0U;
    l_ulSampleCount = 0U;
    rt_hw_interrupt_enable(tLevel);

    return;
}

/***************************
 * NoiseMonitor_AudioCallback: 处理 Audio Manager 麦克风 PCM 回调。
 * 参数：
 *   - eCommand: Audio Manager 回调事件。
 *   - pUserData: 未使用的用户数据。
 *   - ulReserved: 数据事件中为 audio_server_coming_data_t 指针。
 * 返回值：始终返回 0。
 ***************************/
static int NoiseMonitor_AudioCallback(audio_server_callback_cmt_t eCommand,
                                      void *pUserData,
                                      uint32_t ulReserved)
{
    audio_server_coming_data_t *pComingData;
    const int16_t *pSamples;
    uint32_t ulSampleCount;
    uint32_t ulIndex;
    int64_t dLocalSum;
    uint64_t udLocalSquareSum;
    rt_base_t tLevel;

    (void)pUserData;
    if (as_callback_cmd_suspended == eCommand)
    {
        l_bAudioSuspended = true;
        return 0;
    }
    if (as_callback_cmd_resumed == eCommand)
    {
        l_bAudioSuspended = false;
        return 0;
    }
    if ((as_callback_cmd_data_coming != eCommand) || (0U == ulReserved))
    {
        return 0;
    }

    pComingData = (audio_server_coming_data_t *)(uintptr_t)ulReserved;
    if ((NULL == pComingData->data) ||
            (sizeof(int16_t) > pComingData->data_len))
    {
        return 0;
    }

    pSamples = (const int16_t *)pComingData->data;
    ulSampleCount = pComingData->data_len / sizeof(int16_t);
    dLocalSum = 0;
    udLocalSquareSum = 0U;
    for (ulIndex = 0U; ulIndex < ulSampleCount; ulIndex++)
    {
        int32_t lSample;

        lSample = pSamples[ulIndex];
        dLocalSum += lSample;
        udLocalSquareSum += (uint64_t)((int64_t)lSample * (int64_t)lSample);
    }

    /* 浮点与日志均不在回调中执行，临界区只合并三个累加量。 */
    tLevel = rt_hw_interrupt_disable();
    if ((l_ulSampleCount + ulSampleCount) <= NOISEMONITOR_MAX_WINDOW_SAMPLES)
    {
        l_dSampleSum += dLocalSum;
        l_udSampleSquareSum += udLocalSquareSum;
        l_ulSampleCount += ulSampleCount;
    }
    rt_hw_interrupt_enable(tLevel);

    return 0;
}

/***************************
 * NoiseMonitor_OpenAudio: 打开片上 Codec 麦克风采集链路。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，失败返回 -RT_ERROR。
 ***************************/
static rt_err_t NoiseMonitor_OpenAudio(void)
{
    audio_parameter_t tParameters;

    rt_memset(&tParameters, 0, sizeof(tParameters));
    tParameters.read_bits_per_sample = NOISEMONITOR_BITS_PER_SAMPLE;
    tParameters.read_channnel_num = NOISEMONITOR_CHANNEL_COUNT;
    tParameters.read_samplerate = NOISEMONITOR_SAMPLE_RATE_HZ;
    tParameters.read_cache_size = NOISEMONITOR_AUDIO_CACHE_BYTES;
    tParameters.write_bits_per_sample = NOISEMONITOR_BITS_PER_SAMPLE;
    tParameters.write_channnel_num = NOISEMONITOR_CHANNEL_COUNT;
    tParameters.write_samplerate = NOISEMONITOR_SAMPLE_RATE_HZ;
    tParameters.write_cache_size = NOISEMONITOR_AUDIO_CACHE_BYTES;

    NoiseMonitor_ResetAccumulator();
    l_bAudioSuspended = false;
    l_pAudioClient = audio_open(
        AUDIO_TYPE_LOCAL_RECORD,
        AUDIO_RX,
        &tParameters,
        NoiseMonitor_AudioCallback,
        NULL);
    if (NULL == l_pAudioClient)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

/***************************
 * NoiseMonitor_CloseAudio: 关闭片上 Codec 麦克风采集链路。
 * 参数：无。
 * 返回值：Audio Manager 关闭结果。
 ***************************/
static rt_err_t NoiseMonitor_CloseAudio(void)
{
    int lResult;

    lResult = RT_EOK;
    if (NULL != l_pAudioClient)
    {
        lResult = audio_close(l_pAudioClient);
        l_pAudioClient = NULL;
    }
    l_bAudioSuspended = false;
    NoiseMonitor_ResetAccumulator();

    return (rt_err_t)lResult;
}

/***************************
 * NoiseMonitor_PublishWindow: 将最近一秒 PCM 能量换算为估算声压级。
 * 参数：无。
 * 返回值：无。
 ***************************/
static void NoiseMonitor_PublishWindow(void)
{
    int64_t dSampleSum;
    uint64_t udSampleSquareSum;
    uint32_t ulSampleCount;
    float fMean;
    float fMeanSquare;
    float fVariance;
    float fRms;
    float fDb;
    uint8_t ucDb;

    NoiseMonitor_TakeAccumulator(
        &dSampleSum,
        &udSampleSquareSum,
        &ulSampleCount);
    if (NOISEMONITOR_MIN_WINDOW_SAMPLES > ulSampleCount)
    {
        NoiseMonitor_UpdateSnapshot(
            true,
            false,
            0U,
            (true == l_bAudioSuspended) ?
                NOISE_MONITOR_STATE_SUSPENDED : NOISE_MONITOR_STATE_RUNNING,
            0,
            ulSampleCount);
        return;
    }

    fMean = (float)dSampleSum / (float)ulSampleCount;
    fMeanSquare = (float)udSampleSquareSum / (float)ulSampleCount;
    fVariance = fMeanSquare - (fMean * fMean);
    if (1.0f > fVariance)
    {
        fVariance = 1.0f;
    }
    fRms = sqrtf(fVariance);
    fDb = (20.0f * log10f(fRms / NOISEMONITOR_PCM_FULL_SCALE)) +
          NOISEMONITOR_SPL_CALIBRATION_DB;
    if (NOISEMONITOR_MIN_DB > fDb)
    {
        fDb = NOISEMONITOR_MIN_DB;
    }
    else if (NOISEMONITOR_MAX_DB < fDb)
    {
        fDb = NOISEMONITOR_MAX_DB;
    }
    ucDb = (uint8_t)(fDb + 0.5f);

    NoiseMonitor_UpdateSnapshot(
        true,
        true,
        ucDb,
        NOISE_MONITOR_STATE_RUNNING,
        0,
        ulSampleCount);

    return;
}

/***************************
 * NoiseMonitor_ThreadEntry: 管理麦克风生命周期并每秒发布一次测量结果。
 * 参数：
 *   - pParameter: 未使用的线程参数。
 * 返回值：无。
 ***************************/
static void NoiseMonitor_ThreadEntry(void *pParameter)
{
    rt_uint32_t ulEvents;
    rt_err_t tResult;
    bool bRequested;
    rt_base_t tLevel;

    (void)pParameter;
    while (true)
    {
        ulEvents = 0U;
        if (NULL == l_pAudioClient)
        {
            tResult = rt_event_recv(
                &l_tControlEvent,
                NOISEMONITOR_EVENT_START | NOISEMONITOR_EVENT_STOP,
                RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                RT_WAITING_FOREVER,
                &ulEvents);
            if (RT_EOK != tResult)
            {
                continue;
            }

            tLevel = rt_hw_interrupt_disable();
            bRequested = l_bRunRequested;
            rt_hw_interrupt_enable(tLevel);
            if ((false == bRequested) ||
                    (0U == (ulEvents & NOISEMONITOR_EVENT_START)))
            {
                NoiseMonitor_UpdateSnapshot(
                    false, false, 0U, NOISE_MONITOR_STATE_IDLE, 0, 0U);
                continue;
            }

            NoiseMonitor_UpdateSnapshot(
                true, false, 0U, NOISE_MONITOR_STATE_STARTING, 0, 0U);
            tResult = NoiseMonitor_OpenAudio();
            if (RT_EOK != tResult)
            {
                NoiseMonitor_UpdateSnapshot(
                    true, false, 0U, NOISE_MONITOR_STATE_ERROR, tResult, 0U);
                tLevel = rt_hw_interrupt_disable();
                l_bRunRequested = false;
                rt_hw_interrupt_enable(tLevel);
                continue;
            }
        }

        ulEvents = 0U;
        tResult = rt_event_recv(
            &l_tControlEvent,
            NOISEMONITOR_EVENT_START | NOISEMONITOR_EVENT_STOP,
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
            rt_tick_from_millisecond(NOISEMONITOR_WINDOW_MS),
            &ulEvents);
        tLevel = rt_hw_interrupt_disable();
        bRequested = l_bRunRequested;
        rt_hw_interrupt_enable(tLevel);
        if ((false == bRequested) ||
                (0U != (ulEvents & NOISEMONITOR_EVENT_STOP)))
        {
            tResult = NoiseMonitor_CloseAudio();
            NoiseMonitor_UpdateSnapshot(
                false,
                false,
                0U,
                (RT_EOK == tResult) ?
                    NOISE_MONITOR_STATE_IDLE : NOISE_MONITOR_STATE_ERROR,
                tResult,
                0U);
            continue;
        }

        if (RT_EOK != tResult)
        {
            NoiseMonitor_PublishWindow();
        }
    }
}
#endif /* BSP_USING_PC_SIMULATOR */

/***************************
 * NOISEMONITOR_Start: 请求启动麦克风噪声采集。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，服务未就绪返回 -RT_ERROR。
 ***************************/
rt_err_t NOISEMONITOR_Start(void)
{
    if (false == l_bServiceReady)
    {
        return -RT_ERROR;
    }

#ifdef BSP_USING_PC_SIMULATOR
    NoiseMonitor_UpdateSnapshot(
        true, false, 0U, NOISE_MONITOR_STATE_STARTING, 0, 0U);
    return RT_EOK;
#else
    {
        rt_base_t tLevel;

        tLevel = rt_hw_interrupt_disable();
        l_bRunRequested = true;
        rt_hw_interrupt_enable(tLevel);
    }
    return rt_event_send(&l_tControlEvent, NOISEMONITOR_EVENT_START);
#endif
}

/***************************
 * NOISEMONITOR_Stop: 请求停止麦克风噪声采集并释放音频链路。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，服务未就绪返回 -RT_ERROR。
 ***************************/
rt_err_t NOISEMONITOR_Stop(void)
{
    if (false == l_bServiceReady)
    {
        return -RT_ERROR;
    }

#ifdef BSP_USING_PC_SIMULATOR
    NoiseMonitor_UpdateSnapshot(
        false, false, 0U, NOISE_MONITOR_STATE_IDLE, 0, 0U);
    return RT_EOK;
#else
    {
        rt_base_t tLevel;

        tLevel = rt_hw_interrupt_disable();
        l_bRunRequested = false;
        rt_hw_interrupt_enable(tLevel);
    }
    return rt_event_send(&l_tControlEvent, NOISEMONITOR_EVENT_STOP);
#endif
}

/***************************
 * NOISEMONITOR_GetSnapshot: 复制最近一次噪声测量快照。
 * 参数：
 *   - pSnapshot: 输出快照指针，不得为 NULL。
 * 返回值：成功返回 RT_EOK，参数无效返回 -RT_EINVAL，服务未就绪返回 -RT_ERROR。
 ***************************/
rt_err_t NOISEMONITOR_GetSnapshot(NOISE_MONITOR_SNAPSHOT *pSnapshot)
{
    rt_err_t tResult;

    if (NULL == pSnapshot)
    {
        return -RT_EINVAL;
    }
    if (false == l_bServiceReady)
    {
        return -RT_ERROR;
    }

#ifdef BSP_USING_PC_SIMULATOR
    tResult = rt_mutex_take(&l_tSnapshotMutex, RT_WAITING_FOREVER);
    if (RT_EOK == tResult)
    {
        if (true == l_tSnapshot.bRunning)
        {
            uint32_t ulSeconds;

            ulSeconds = rt_tick_get() / RT_TICK_PER_SECOND;
            l_tSnapshot.bValid = true;
            l_tSnapshot.ucDb = (uint8_t)(45U + (ulSeconds % 45U));
            l_tSnapshot.eState = NOISE_MONITOR_STATE_RUNNING;
            l_tSnapshot.ulSampleCount = NOISEMONITOR_SAMPLE_RATE_HZ;
            l_tSnapshot.ulGeneration = ulSeconds;
        }
        *pSnapshot = l_tSnapshot;
        (void)rt_mutex_release(&l_tSnapshotMutex);
    }
    return tResult;
#else
    tResult = rt_mutex_take(&l_tSnapshotMutex, RT_WAITING_FOREVER);
    if (RT_EOK == tResult)
    {
        *pSnapshot = l_tSnapshot;
        (void)rt_mutex_release(&l_tSnapshotMutex);
    }
    return tResult;
#endif
}

/***************************
 * NoiseMonitor_Init: 初始化噪声采集服务的静态 RT-Thread 对象。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回 RT-Thread 错误码。
 ***************************/
static int NoiseMonitor_Init(void)
{
    rt_err_t tResult;

    rt_memset(&l_tSnapshot, 0, sizeof(l_tSnapshot));
    l_tSnapshot.eState = NOISE_MONITOR_STATE_IDLE;
    tResult = rt_mutex_init(
        &l_tSnapshotMutex,
        "noise_snap",
        RT_IPC_FLAG_PRIO);
    if (RT_EOK != tResult)
    {
        return tResult;
    }

#ifndef BSP_USING_PC_SIMULATOR
    tResult = rt_event_init(
        &l_tControlEvent,
        "noise_ctl",
        RT_IPC_FLAG_PRIO);
    if (RT_EOK != tResult)
    {
        (void)rt_mutex_detach(&l_tSnapshotMutex);
        return tResult;
    }
    tResult = rt_thread_init(
        &l_tWorkerThread,
        "noise",
        NoiseMonitor_ThreadEntry,
        NULL,
        l_aWorkerStack,
        sizeof(l_aWorkerStack),
        NOISEMONITOR_THREAD_PRIORITY,
        NOISEMONITOR_THREAD_TICK);
    if (RT_EOK != tResult)
    {
        (void)rt_event_detach(&l_tControlEvent);
        (void)rt_mutex_detach(&l_tSnapshotMutex);
        return tResult;
    }
    tResult = rt_thread_startup(&l_tWorkerThread);
    if (RT_EOK != tResult)
    {
        (void)rt_thread_detach(&l_tWorkerThread);
        (void)rt_event_detach(&l_tControlEvent);
        (void)rt_mutex_detach(&l_tSnapshotMutex);
        return tResult;
    }
#endif

    l_bServiceReady = true;
    return RT_EOK;
}
INIT_APP_EXPORT(NoiseMonitor_Init);
