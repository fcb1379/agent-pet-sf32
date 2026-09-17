#include "bike_sound.h"

#include <stddef.h>

#define BIKE_SOUND_PATTERN_NODE_MAX (5U)

/* l_atBikeSoundPatterns: X-TRACK MusicCode 的固定提示音序列。
 * 频率范围 0~4000 Hz，时长范围 40~180 ms；未使用的尾部节点为 0。
 */
static const BIKE_SOUND_NODE
    l_atBikeSoundPatterns[BIKE_SOUND_EVENT_COUNT][BIKE_SOUND_PATTERN_NODE_MAX] =
{
    [BIKE_SOUND_EVENT_STARTUP] = {{523U, 80U}, {880U, 80U}, {659U, 80U}},
    [BIKE_SOUND_EVENT_ERROR] = {{100U, 80U}, {0U, 80U}, {100U, 80U}},
    [BIKE_SOUND_EVENT_CONNECT] = {{1046U, 80U}, {1175U, 80U}, {1318U, 80U}},
    [BIKE_SOUND_EVENT_DISCONNECT] = {{1318U, 80U}, {1175U, 80U}, {1046U, 80U}},
    [BIKE_SOUND_EVENT_UNSTABLE] = {{1046U, 80U}, {0U, 80U}, {1046U, 80U}},
    [BIKE_SOUND_EVENT_CHARGE_START] = {{262U, 80U}, {330U, 80U}},
    [BIKE_SOUND_EVENT_CHARGE_END] = {{330U, 80U}, {262U, 80U}},
    [BIKE_SOUND_EVENT_NO_OPERATION] =
        {{4000U, 40U}, {0U, 80U}, {4000U, 40U}, {0U, 80U}, {4000U, 40U}},
};

/* l_aBikeSoundPatternLengths: 各提示音有效节点数量，索引与事件枚举一致。 */
static const uint8_t l_aBikeSoundPatternLengths[BIKE_SOUND_EVENT_COUNT] =
{
    3U, 3U, 3U, 3U, 3U, 2U, 2U, 5U
};

/* l_apBikeSoundEventNames: shell 与日志使用的稳定事件名称。 */
static const char *const l_apBikeSoundEventNames[BIKE_SOUND_EVENT_COUNT] =
{
    "startup",
    "error",
    "connect",
    "disconnect",
    "unstable",
    "charge_start",
    "charge_end",
    "no_operation",
};

/* BIKE_SOUND_GetPattern: 获取提示音固定序列。
 * 参数：
 *   - eEvent: 提示音事件
 *   - pNodeCount: 输出有效节点数量
 * 返回值：事件合法时返回只读序列，否则返回 NULL
 */
const BIKE_SOUND_NODE *BIKE_SOUND_GetPattern(BIKE_SOUND_EVENT eEvent,
                                              uint8_t *pNodeCount)
{
    if (NULL == pNodeCount)
    {
        return NULL;
    }
    *pNodeCount = 0U;
    if ((uint32_t)eEvent >= (uint32_t)BIKE_SOUND_EVENT_COUNT)
    {
        return NULL;
    }
    *pNodeCount = l_aBikeSoundPatternLengths[eEvent];

    return l_atBikeSoundPatterns[eEvent];
}

/* BIKE_SOUND_GetEventName: 获取提示音事件名称。
 * 参数：
 *   - eEvent: 提示音事件
 * 返回值：事件合法时返回静态名称，否则返回 NULL
 */
const char *BIKE_SOUND_GetEventName(BIKE_SOUND_EVENT eEvent)
{
    if ((uint32_t)eEvent >= (uint32_t)BIKE_SOUND_EVENT_COUNT)
    {
        return NULL;
    }

    return l_apBikeSoundEventNames[eEvent];
}

#ifdef BIKE_SOUND_HOST_BUILD

/* BIKE_SOUND_Request: 主机测试不访问目标音频设备。
 * 参数：
 *   - eEvent: 提示音事件
 * 返回值：主机测试始终返回 false
 */
bool BIKE_SOUND_Request(BIKE_SOUND_EVENT eEvent)
{
    (void)eEvent;
    return false;
}

#else

#include <string.h>

#include <rtthread.h>

#include "audio_server.h"
#include "bike_settings.h"

#define LOG_TAG "bike.sound"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#define BIKE_SOUND_SAMPLE_RATE_HZ (16000U)
#define BIKE_SOUND_BITS_PER_SAMPLE (16U)
#define BIKE_SOUND_CHANNEL_COUNT (1U)
#define BIKE_SOUND_FRAME_MS (20U)
#define BIKE_SOUND_FRAME_SAMPLES \
    ((BIKE_SOUND_SAMPLE_RATE_HZ * BIKE_SOUND_FRAME_MS) / 1000U)
#define BIKE_SOUND_AMPLITUDE (4096)
#define BIKE_SOUND_WRITE_RETRY_MAX (20U)
#define BIKE_SOUND_QUEUE_DEPTH (8U)
#define BIKE_SOUND_THREAD_STACK_SIZE (2048U)
#define BIKE_SOUND_THREAD_PRIORITY (21U)
#define BIKE_SOUND_QUEUE_POOL_SIZE \
    (BIKE_SOUND_QUEUE_DEPTH * \
     (RT_ALIGN(sizeof(BIKE_SOUND_EVENT), RT_ALIGN_SIZE) + sizeof(void *)))

/* l_tBikeSoundQueue: 提示音请求静态消息队列，不使用运行期堆内存。 */
static struct rt_messagequeue l_tBikeSoundQueue;

/* l_aBikeSoundQueuePool: 八条提示音请求及消息链指针的固定存储池。 */
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikeSoundQueuePool[BIKE_SOUND_QUEUE_POOL_SIZE];

/* l_tBikeSoundThread: 串行生成并输出提示音的静态线程控制块。 */
static struct rt_thread l_tBikeSoundThread;

/* l_aBikeSoundThreadStack: 提示音线程固定 2048 字节栈。 */
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikeSoundThreadStack[BIKE_SOUND_THREAD_STACK_SIZE];

/* l_asBikeSoundPcm: 20 ms、16 kHz、单声道 16-bit 固定 PCM 缓冲。 */
ALIGN(RT_ALIGN_SIZE)
static int16_t l_asBikeSoundPcm[BIKE_SOUND_FRAME_SAMPLES];

/* l_bBikeSoundReady: 队列和播放线程已经启动的标志。 */
static bool l_bBikeSoundReady;

/* BikeSound_WriteFrame: 将一帧 PCM 完整写入音频服务。
 * 参数：
 *   - pClient: 已打开的通知音频客户端
 *   - pData: PCM 字节缓冲
 *   - ulLength: 待写字节数
 * 返回值：完整写入返回 true，挂起、错误或重试超限返回 false
 */
static bool BikeSound_WriteFrame(audio_client_t pClient, uint8_t *pData,
                                 uint32_t ulLength)
{
    uint32_t ulOffset;
    uint32_t ulRetryCount;
    int lWritten;

    if ((NULL == pClient) || (NULL == pData) || (0U == ulLength))
    {
        return false;
    }
    ulOffset = 0U;
    ulRetryCount = 0U;
    while (ulLength > ulOffset)
    {
        lWritten = audio_write(pClient, &pData[ulOffset],
                               ulLength - ulOffset);
        if ((0 < lWritten) &&
            ((uint32_t)lWritten <= (ulLength - ulOffset)))
        {
            ulOffset += (uint32_t)lWritten;
            ulRetryCount = 0U;
        }
        else if ((0 == lWritten) &&
                 (BIKE_SOUND_WRITE_RETRY_MAX > ulRetryCount))
        {
            ulRetryCount++;
            rt_thread_mdelay(1U);
        }
        else
        {
            return false;
        }
    }

    return true;
}

/* BikeSound_PlayNode: 生成并播放一段方波或静音。
 * 参数：
 *   - pClient: 已打开的通知音频客户端
 *   - pNode: 频率和时长节点
 * 返回值：全部帧写入成功返回 true，否则返回 false
 */
static bool BikeSound_PlayNode(audio_client_t pClient,
                               const BIKE_SOUND_NODE *pNode)
{
    uint32_t ulRemainingSamples;
    uint32_t ulFrameSamples;
    uint32_t ulIndex;
    uint32_t ulPhase;
    uint32_t ulFrameDelayMs;

    if ((NULL == pClient) || (NULL == pNode) ||
        (0U == pNode->usDurationMs))
    {
        return false;
    }
    ulRemainingSamples = ((uint32_t)pNode->usDurationMs *
                          BIKE_SOUND_SAMPLE_RATE_HZ) / 1000U;
    ulPhase = 0U;
    while (0U < ulRemainingSamples)
    {
        ulFrameSamples = ulRemainingSamples;
        if (BIKE_SOUND_FRAME_SAMPLES < ulFrameSamples)
        {
            ulFrameSamples = BIKE_SOUND_FRAME_SAMPLES;
        }
        for (ulIndex = 0U; ulIndex < ulFrameSamples; ulIndex++)
        {
            if (0U == pNode->usFrequencyHz)
            {
                l_asBikeSoundPcm[ulIndex] = 0;
            }
            else
            {
                ulPhase += pNode->usFrequencyHz;
                while (BIKE_SOUND_SAMPLE_RATE_HZ <= ulPhase)
                {
                    ulPhase -= BIKE_SOUND_SAMPLE_RATE_HZ;
                }
                l_asBikeSoundPcm[ulIndex] =
                    ((BIKE_SOUND_SAMPLE_RATE_HZ / 2U) > ulPhase) ?
                    BIKE_SOUND_AMPLITUDE : -BIKE_SOUND_AMPLITUDE;
            }
        }
        if (!BikeSound_WriteFrame(
                pClient, (uint8_t *)l_asBikeSoundPcm,
                ulFrameSamples * sizeof(l_asBikeSoundPcm[0])))
        {
            return false;
        }
        ulFrameDelayMs = (ulFrameSamples * 1000U) /
                         BIKE_SOUND_SAMPLE_RATE_HZ;
        rt_thread_mdelay(ulFrameDelayMs);
        ulRemainingSamples -= ulFrameSamples;
    }

    return true;
}

/* BikeSound_PlayPattern: 通过黄山派片上 Audio Codec/扬声器播放提示音。
 * 参数：
 *   - eEvent: 提示音事件
 * 返回值：音频打开、写入和关闭均成功返回 true
 */
static bool BikeSound_PlayPattern(BIKE_SOUND_EVENT eEvent)
{
    BIKE_SETTINGS_SNAPSHOT tSettings;
    audio_parameter_t tParameter;
    audio_client_t pClient;
    const BIKE_SOUND_NODE *pPattern;
    uint8_t ucNodeCount;
    uint8_t ucIndex;
    int lCloseResult;
    bool bResult;

    if ((RT_EOK != BIKE_SETTINGS_GetSnapshot(&tSettings)) ||
        (!tSettings.bSoundEnabled))
    {
        return true;
    }
    pPattern = BIKE_SOUND_GetPattern(eEvent, &ucNodeCount);
    if ((NULL == pPattern) || (0U == ucNodeCount))
    {
        return false;
    }
    (void)memset(&tParameter, 0, sizeof(tParameter));
    tParameter.write_samplerate = BIKE_SOUND_SAMPLE_RATE_HZ;
    tParameter.write_cache_size = BIKE_SOUND_FRAME_SAMPLES *
                                  sizeof(l_asBikeSoundPcm[0]) * 2U;
    tParameter.write_channnel_num = BIKE_SOUND_CHANNEL_COUNT;
    tParameter.write_bits_per_sample = BIKE_SOUND_BITS_PER_SAMPLE;
    pClient = audio_open2(AUDIO_TYPE_NOTIFY, AUDIO_TX, &tParameter,
                          NULL, NULL, AUDIO_DEVICE_SPEAKER);
    if (NULL == pClient)
    {
        LOG_E("audio open failed event=%u", eEvent);
        return false;
    }
    bResult = true;
    for (ucIndex = 0U; ucIndex < ucNodeCount; ucIndex++)
    {
        if (!BikeSound_PlayNode(pClient, &pPattern[ucIndex]))
        {
            bResult = false;
            break;
        }
    }
    lCloseResult = audio_close(pClient);
    if (0 != lCloseResult)
    {
        bResult = false;
    }

    return bResult;
}

/* BikeSound_ThreadEntry: 串行消费提示音请求，避免调用线程阻塞。
 * 参数：
 *   - pParameter: 未使用
 * 返回值：无
 */
static void BikeSound_ThreadEntry(void *pParameter)
{
    BIKE_SOUND_EVENT eEvent;

    (void)pParameter;
    while (true)
    {
        if (RT_EOK != rt_mq_recv(&l_tBikeSoundQueue, &eEvent,
                                 sizeof(eEvent), RT_WAITING_FOREVER))
        {
            continue;
        }
        if (!BikeSound_PlayPattern(eEvent))
        {
            LOG_W("play failed event=%u", eEvent);
        }
    }
}

/* BIKE_SOUND_Request: 非阻塞提交提示音请求。
 * 参数：
 *   - eEvent: 提示音事件
 * 返回值：事件已入队返回 true，未初始化、参数非法或队列满返回 false
 */
bool BIKE_SOUND_Request(BIKE_SOUND_EVENT eEvent)
{
    uint8_t ucNodeCount;

    if ((!l_bBikeSoundReady) ||
        (NULL == BIKE_SOUND_GetPattern(eEvent, &ucNodeCount)))
    {
        return false;
    }

    return (RT_EOK == rt_mq_send(&l_tBikeSoundQueue, &eEvent,
                                 sizeof(eEvent)));
}

/* BikeSound_ParseEvent: 将 shell 名称解析为提示音事件。
 * 参数：
 *   - pName: 事件名称
 *   - pEvent: 输出事件
 * 返回值：完整匹配返回 true，否则返回 false
 */
static bool BikeSound_ParseEvent(const char *pName,
                                 BIKE_SOUND_EVENT *pEvent)
{
    BIKE_SOUND_EVENT eEvent;
    const char *pEventName;

    if ((NULL == pName) || (NULL == pEvent))
    {
        return false;
    }
    for (eEvent = BIKE_SOUND_EVENT_STARTUP;
         eEvent < BIKE_SOUND_EVENT_COUNT;
         eEvent = (BIKE_SOUND_EVENT)(eEvent + 1))
    {
        pEventName = BIKE_SOUND_GetEventName(eEvent);
        if ((NULL != pEventName) && (0 == strcmp(pName, pEventName)))
        {
            *pEvent = eEvent;
            return true;
        }
    }

    return false;
}

/* BikeSound_Command: 查询或手动触发提示音。
 * 参数：
 *   - lArgumentCount: shell 参数数量
 *   - pArguments: shell 参数数组
 * 返回值：无
 */
static void BikeSound_Command(int lArgumentCount, char **pArguments)
{
    BIKE_SOUND_EVENT eEvent;

    if ((3 == lArgumentCount) &&
        (0 == strcmp(pArguments[1], "play")) &&
        BikeSound_ParseEvent(pArguments[2], &eEvent))
    {
        rt_kprintf("bike sound play %s ret=%u\n", pArguments[2],
                   BIKE_SOUND_Request(eEvent));
        return;
    }
    rt_kprintf("bike sound ready=%u queue=%u/%u\n", l_bBikeSoundReady,
               l_tBikeSoundQueue.entry, l_tBikeSoundQueue.max_msgs);
    rt_kprintf("usage: bikesound play startup|error|connect|disconnect|"
               "unstable|charge_start|charge_end|no_operation\n");
    return;
}
MSH_CMD_EXPORT_ALIAS(BikeSound_Command, bikesound, bike prompt sound control);

/* BikeSound_AppInit: 初始化静态队列与提示音线程。
 * 返回值：成功返回 RT_EOK，否则返回错误码
 */
static int BikeSound_AppInit(void)
{
    rt_err_t eResult;

    if (l_bBikeSoundReady)
    {
        return RT_EOK;
    }
    eResult = rt_mq_init(&l_tBikeSoundQueue, "bike_snd",
                         l_aBikeSoundQueuePool, sizeof(BIKE_SOUND_EVENT),
                         sizeof(l_aBikeSoundQueuePool), RT_IPC_FLAG_FIFO);
    if (RT_EOK == eResult)
    {
        eResult = rt_thread_init(&l_tBikeSoundThread, "bike_snd",
                                 BikeSound_ThreadEntry, NULL,
                                 l_aBikeSoundThreadStack,
                                 sizeof(l_aBikeSoundThreadStack),
                                 BIKE_SOUND_THREAD_PRIORITY, 10U);
    }
    if (RT_EOK == eResult)
    {
        eResult = rt_thread_startup(&l_tBikeSoundThread);
    }
    if (RT_EOK != eResult)
    {
        LOG_E("init failed: %d", eResult);
        return eResult;
    }
    l_bBikeSoundReady = true;
    LOG_I("ready sample=%u Hz frame=%u ms", BIKE_SOUND_SAMPLE_RATE_HZ,
          BIKE_SOUND_FRAME_MS);

    return RT_EOK;
}
INIT_APP_EXPORT(BikeSound_AppInit);

#endif /* BIKE_SOUND_HOST_BUILD */
