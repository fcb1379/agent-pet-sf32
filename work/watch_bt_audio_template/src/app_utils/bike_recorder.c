#include "bike_recorder.h"
#include "bike_storage.h"

#include <rtthread.h>
#include <stdio.h>
#include <string.h>

#define LOG_TAG "bike.rec"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#define BIKE_RECORDER_THREAD_STACK_SIZE (3072U)
#define BIKE_RECORDER_THREAD_PRIORITY (21U)
#define BIKE_RECORDER_QUEUE_DEPTH (12U)

/* BIKE_RECORDER_COMMAND: 记录线程接收的固定命令类型。 */
typedef enum _BIKE_RECORDER_COMMAND
{
    BIKE_RECORDER_COMMAND_START = 0,
    BIKE_RECORDER_COMMAND_PAUSE,
    BIKE_RECORDER_COMMAND_RESUME,
    BIKE_RECORDER_COMMAND_STOP,
    BIKE_RECORDER_COMMAND_DISCARD,
    BIKE_RECORDER_COMMAND_POINT
} BIKE_RECORDER_COMMAND;

/* BIKE_RECORDER_MESSAGE: 固定容量记录线程消息。 */
typedef struct _BIKE_RECORDER_MESSAGE
{
    BIKE_RECORDER_COMMAND eCommand;
    BIKE_GNSS_DATA tGnss;
} BIKE_RECORDER_MESSAGE;

/* l_tBikeRecorderSnapshot: 仅在记录器互斥锁保护下访问的 UI 快照。 */
static BIKE_RECORDER_SNAPSHOT l_tBikeRecorderSnapshot;

/* l_tBikeRecorderMutex: 保护轨迹记录状态快照。 */
static struct rt_mutex l_tBikeRecorderMutex;

/* l_tBikeRecorderQueue: 隔离 GNSS/UI 与文件 IO 的静态消息队列。 */
static struct rt_messagequeue l_tBikeRecorderQueue;

/* l_tBikeRecorderThread: 轨迹文件写入线程控制块。 */
static struct rt_thread l_tBikeRecorderThread;

/* l_aBikeRecorderThreadStack: 轨迹线程固定 3072 字节栈。 */
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikeRecorderThreadStack[BIKE_RECORDER_THREAD_STACK_SIZE];

/* l_aBikeRecorderQueuePool: 十二条固定消息的静态队列存储。 */
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikeRecorderQueuePool[BIKE_RECORDER_QUEUE_DEPTH *
                                        sizeof(BIKE_RECORDER_MESSAGE)];

/* l_bBikeRecorderReady: 记录线程和同步对象已初始化标志。 */
static bool l_bBikeRecorderReady;

/* BikeRecorder_Lock: 获取记录快照互斥锁。
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeRecorder_Lock(void)
{
    return l_bBikeRecorderReady &&
           (RT_EOK == rt_mutex_take(&l_tBikeRecorderMutex, RT_WAITING_FOREVER));
}

/* BikeRecorder_Unlock: 释放记录快照互斥锁。
 * 返回值：无
 */
static void BikeRecorder_Unlock(void)
{
    rt_err_t eResult;

    eResult = rt_mutex_release(&l_tBikeRecorderMutex);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex release failed: %d", eResult);
    }

    return;
}

/* BikeRecorder_UpdateSnapshot: 发布记录线程私有状态。
 * 参数：
 *   - eStatus: 服务状态
 *   - pWriter: 可选 GPX 状态
 *   - pFilePath: 可选覆盖文件路径
 * 返回值：无
 */
static void BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS eStatus,
                                        const BIKE_GPX_WRITER *pWriter,
                                        const char *pFilePath)
{
    int lLength;

    if (!BikeRecorder_Lock())
    {
        return;
    }

    l_tBikeRecorderSnapshot.eStatus = eStatus;
    if (NULL != pWriter)
    {
        l_tBikeRecorderSnapshot.eError = pWriter->eError;
        l_tBikeRecorderSnapshot.ulPointCount = pWriter->ulPointCount;
        if ('\0' != pWriter->aFinalPath[0])
        {
            lLength = snprintf(l_tBikeRecorderSnapshot.aFilePath,
                               sizeof(l_tBikeRecorderSnapshot.aFilePath), "%s",
                               pWriter->aFinalPath);
            if ((0 >= lLength) ||
                    ((size_t)lLength >= sizeof(l_tBikeRecorderSnapshot.aFilePath)))
            {
                l_tBikeRecorderSnapshot.aFilePath[0] = '\0';
            }
        }
    }
    if (NULL != pFilePath)
    {
        lLength = snprintf(l_tBikeRecorderSnapshot.aFilePath,
                           sizeof(l_tBikeRecorderSnapshot.aFilePath), "%s", pFilePath);
        if ((0 >= lLength) ||
                ((size_t)lLength >= sizeof(l_tBikeRecorderSnapshot.aFilePath)))
        {
            l_tBikeRecorderSnapshot.aFilePath[0] = '\0';
        }
    }

    BikeRecorder_Unlock();

    return;
}

/* BikeRecorder_HasUsableFix: 判断定位点是否可建立 GPX 会话。
 * 参数：
 *   - pGnss: 定位数据
 * 返回值：有效返回 true，否则返回 false
 */
static bool BikeRecorder_HasUsableFix(const BIKE_GNSS_DATA *pGnss)
{
    return (NULL != pGnss) && pGnss->bFixValid &&
           (2000U <= pGnss->usYear) && (2099U >= pGnss->usYear) &&
           (1U <= pGnss->ucMonth) && (12U >= pGnss->ucMonth) &&
           (1U <= pGnss->ucDay) && (31U >= pGnss->ucDay) &&
           (24U > pGnss->ucHour) && (60U > pGnss->ucMinute) &&
           (60U > pGnss->ucSecond);
}

/* BikeRecorder_HandlePoint: 开始会话或追加一个有效定位点。
 * 参数：
 *   - pWriter: GPX 写入器
 *   - pMessage: 定位点消息
 *   - pSessionRequested: 会话请求标志
 *   - pPaused: 暂停标志
 * 返回值：无
 */
static void BikeRecorder_HandlePoint(BIKE_GPX_WRITER *pWriter,
                                     const BIKE_RECORDER_MESSAGE *pMessage,
                                     bool *pSessionRequested,
                                     const bool *pPaused)
{
    bool bResult;

    if ((NULL == pWriter) || (NULL == pMessage) || (NULL == pSessionRequested) ||
            (NULL == pPaused) || (!*pSessionRequested) || (*pPaused) ||
            (!BikeRecorder_HasUsableFix(&pMessage->tGnss)))
    {
        return;
    }

    if ((BIKE_GPX_STATE_IDLE == pWriter->eState) ||
            (BIKE_GPX_STATE_COMPLETE == pWriter->eState))
    {
        bResult = BIKE_GPX_Start(pWriter,
                                 BIKE_STORAGE_GetTrackDirectory(),
                                 &pMessage->tGnss);
    }
    else
    {
        bResult = BIKE_GPX_Append(pWriter, &pMessage->tGnss);
    }

    if (bResult)
    {
        BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_RECORDING, pWriter, NULL);
    }
    else
    {
        *pSessionRequested = false;
        BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_ERROR, pWriter, NULL);
        LOG_E("GPX write failed: %d", pWriter->eError);
    }

    return;
}

/* BikeRecorder_ThreadEntry: 串行处理文件创建、写点、同步和恢复。
 * 参数：
 *   - pParameter: 未使用
 * 返回值：无
 */
static void BikeRecorder_ThreadEntry(void *pParameter)
{
    BIKE_RECORDER_MESSAGE tMessage;
    BIKE_GPX_WRITER tWriter;
    BIKE_GPX_RECOVERY_RESULT eRecovery;
    char aRecoveredPath[BIKE_GPX_PATH_MAX];
    bool bSessionRequested;
    bool bPaused;
    bool bResult;
    const char *pTrackDirectory;

    (void)pParameter;
    pTrackDirectory = BIKE_STORAGE_GetTrackDirectory();
    BIKE_GPX_Init(&tWriter);
    bSessionRequested = false;
    bPaused = false;
    aRecoveredPath[0] = '\0';
    eRecovery = BIKE_GPX_Recover(pTrackDirectory, aRecoveredPath,
                                 sizeof(aRecoveredPath));
    if (BIKE_GPX_RECOVERY_DONE == eRecovery)
    {
        BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_SAVED, &tWriter, aRecoveredPath);
        LOG_W("recovered interrupted track: %s", aRecoveredPath);
    }
    else if (BIKE_GPX_RECOVERY_ERROR == eRecovery)
    {
        BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_ERROR, &tWriter, NULL);
        LOG_E("interrupted track recovery failed");
    }

    while (true)
    {
        if (RT_EOK != rt_mq_recv(&l_tBikeRecorderQueue, &tMessage, sizeof(tMessage),
                                 RT_WAITING_FOREVER))
        {
            continue;
        }

        switch (tMessage.eCommand)
        {
        case BIKE_RECORDER_COMMAND_START:
            if ((BIKE_GPX_STATE_ACTIVE == tWriter.eState) ||
                    (BIKE_GPX_STATE_PAUSED == tWriter.eState))
            {
                if (!BIKE_GPX_Stop(&tWriter))
                {
                    BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_ERROR, &tWriter, NULL);
                    break;
                }
            }
            if (BIKE_GPX_STATE_ERROR == tWriter.eState)
            {
                aRecoveredPath[0] = '\0';
                eRecovery = BIKE_GPX_Recover(pTrackDirectory,
                                             aRecoveredPath,
                                             sizeof(aRecoveredPath));
                if (BIKE_GPX_RECOVERY_ERROR == eRecovery)
                {
                    BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_ERROR,
                                                 &tWriter, NULL);
                    break;
                }
            }
            BIKE_GPX_Init(&tWriter);
            bSessionRequested = true;
            bPaused = false;
            BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_WAITING_FIX, &tWriter, "");
            break;

        case BIKE_RECORDER_COMMAND_PAUSE:
            bPaused = true;
            if (BIKE_GPX_STATE_ACTIVE == tWriter.eState)
            {
                bResult = BIKE_GPX_Pause(&tWriter);
                if (!bResult)
                {
                    BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_ERROR, &tWriter, NULL);
                    break;
                }
            }
            BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_PAUSED, &tWriter, NULL);
            break;

        case BIKE_RECORDER_COMMAND_RESUME:
            bPaused = false;
            if (BIKE_GPX_STATE_PAUSED == tWriter.eState)
            {
                bResult = BIKE_GPX_Resume(&tWriter);
                if (!bResult)
                {
                    BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_ERROR, &tWriter, NULL);
                    break;
                }
                BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_RECORDING, &tWriter, NULL);
            }
            else if (BIKE_GPX_STATE_ERROR == tWriter.eState)
            {
                BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_ERROR, &tWriter, NULL);
            }
            else
            {
                BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_WAITING_FIX, &tWriter, NULL);
            }
            break;

        case BIKE_RECORDER_COMMAND_STOP:
            bSessionRequested = false;
            bPaused = false;
            if ((BIKE_GPX_STATE_ACTIVE == tWriter.eState) ||
                    (BIKE_GPX_STATE_PAUSED == tWriter.eState))
            {
                bResult = BIKE_GPX_Stop(&tWriter);
                BikeRecorder_UpdateSnapshot(bResult ? BIKE_RECORDER_STATUS_SAVED :
                                             BIKE_RECORDER_STATUS_ERROR,
                                             &tWriter, NULL);
            }
            else
            {
                BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_IDLE, &tWriter, NULL);
            }
            break;

        case BIKE_RECORDER_COMMAND_DISCARD:
            bSessionRequested = false;
            bPaused = false;
            if ((BIKE_GPX_STATE_ACTIVE == tWriter.eState) ||
                (BIKE_GPX_STATE_PAUSED == tWriter.eState) ||
                (BIKE_GPX_STATE_ERROR == tWriter.eState))
            {
                bResult = BIKE_GPX_Discard(&tWriter);
                BikeRecorder_UpdateSnapshot(bResult ?
                                             BIKE_RECORDER_STATUS_IDLE :
                                             BIKE_RECORDER_STATUS_ERROR,
                                             &tWriter, "");
            }
            else
            {
                BIKE_GPX_Init(&tWriter);
                BikeRecorder_UpdateSnapshot(BIKE_RECORDER_STATUS_IDLE,
                                             &tWriter, "");
            }
            break;

        case BIKE_RECORDER_COMMAND_POINT:
            BikeRecorder_HandlePoint(&tWriter, &tMessage, &bSessionRequested, &bPaused);
            break;

        default:
            break;
        }
    }

    return;
}

/* BikeRecorder_SendCommand: 非阻塞发送控制命令。
 * 参数：
 *   - eCommand: 控制命令
 *   - pGnss: 可选定位数据
 * 返回值：入队成功返回 true，否则返回 false
 */
static bool BikeRecorder_SendCommand(BIKE_RECORDER_COMMAND eCommand,
                                     const BIKE_GNSS_DATA *pGnss)
{
    BIKE_RECORDER_MESSAGE tMessage;
    rt_err_t eResult;

    if (!l_bBikeRecorderReady)
    {
        return false;
    }
    (void)memset(&tMessage, 0, sizeof(tMessage));
    tMessage.eCommand = eCommand;
    if (NULL != pGnss)
    {
        tMessage.tGnss = *pGnss;
    }

    eResult = rt_mq_send(&l_tBikeRecorderQueue, &tMessage, sizeof(tMessage));
    if ((RT_EOK != eResult) && BikeRecorder_Lock())
    {
        l_tBikeRecorderSnapshot.ulDroppedMessageCount++;
        BikeRecorder_Unlock();
    }

    return RT_EOK == eResult;
}

/* BIKE_RECORDER_Init: 初始化静态队列、快照和文件写入线程。
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_RECORDER_Init(void)
{
    rt_err_t eResult;

    if (l_bBikeRecorderReady)
    {
        return true;
    }

    (void)memset(&l_tBikeRecorderSnapshot, 0, sizeof(l_tBikeRecorderSnapshot));
    eResult = rt_mutex_init(&l_tBikeRecorderMutex, "bike_rec", RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        return false;
    }
    eResult = rt_mq_init(&l_tBikeRecorderQueue, "bike_rec_q", l_aBikeRecorderQueuePool,
                         sizeof(BIKE_RECORDER_MESSAGE), sizeof(l_aBikeRecorderQueuePool),
                         RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        (void)rt_mutex_detach(&l_tBikeRecorderMutex);
        return false;
    }
    eResult = rt_thread_init(&l_tBikeRecorderThread, "bike_rec", BikeRecorder_ThreadEntry,
                             NULL, l_aBikeRecorderThreadStack,
                             sizeof(l_aBikeRecorderThreadStack),
                             BIKE_RECORDER_THREAD_PRIORITY, 10U);
    if (RT_EOK != eResult)
    {
        (void)rt_mq_detach(&l_tBikeRecorderQueue);
        (void)rt_mutex_detach(&l_tBikeRecorderMutex);
        return false;
    }

    l_bBikeRecorderReady = true;
    eResult = rt_thread_startup(&l_tBikeRecorderThread);
    if (RT_EOK != eResult)
    {
        l_bBikeRecorderReady = false;
        l_tBikeRecorderSnapshot.eStatus = BIKE_RECORDER_STATUS_ERROR;
        (void)rt_thread_detach(&l_tBikeRecorderThread);
        (void)rt_mq_detach(&l_tBikeRecorderQueue);
        (void)rt_mutex_detach(&l_tBikeRecorderMutex);
        return false;
    }

    return true;
}

/* BIKE_RECORDER_Start: 请求建立新轨迹会话。
 * 返回值：命令入队成功返回 true
 */
bool BIKE_RECORDER_Start(void)
{
    return BikeRecorder_SendCommand(BIKE_RECORDER_COMMAND_START, NULL);
}

/* BIKE_RECORDER_Pause: 请求暂停轨迹写入。
 * 返回值：命令入队成功返回 true
 */
bool BIKE_RECORDER_Pause(void)
{
    return BikeRecorder_SendCommand(BIKE_RECORDER_COMMAND_PAUSE, NULL);
}

/* BIKE_RECORDER_Resume: 请求继续轨迹写入。
 * 返回值：命令入队成功返回 true
 */
bool BIKE_RECORDER_Resume(void)
{
    return BikeRecorder_SendCommand(BIKE_RECORDER_COMMAND_RESUME, NULL);
}

/* BIKE_RECORDER_Stop: 请求闭合并发布轨迹文件。
 * 返回值：命令入队成功返回 true
 */
bool BIKE_RECORDER_Stop(void)
{
    return BikeRecorder_SendCommand(BIKE_RECORDER_COMMAND_STOP, NULL);
}

/* BIKE_RECORDER_Discard: 请求删除当前未发布轨迹。
 * 返回值：命令入队成功返回 true，否则返回 false
 */
bool BIKE_RECORDER_Discard(void)
{
    return BikeRecorder_SendCommand(BIKE_RECORDER_COMMAND_DISCARD, NULL);
}

/* BIKE_RECORDER_SubmitPoint: 非阻塞提交一帧 GNSS 数据。
 * 参数：
 *   - pGnss: 定位数据，仅输入
 * 返回值：消息入队成功返回 true
 */
bool BIKE_RECORDER_SubmitPoint(const BIKE_GNSS_DATA *pGnss)
{
    BIKE_RECORDER_STATUS eStatus;

    if (NULL == pGnss)
    {
        return false;
    }

    if (!BikeRecorder_Lock())
    {
        return false;
    }
    eStatus = l_tBikeRecorderSnapshot.eStatus;
    BikeRecorder_Unlock();
    if ((BIKE_RECORDER_STATUS_WAITING_FIX != eStatus) &&
            (BIKE_RECORDER_STATUS_RECORDING != eStatus))
    {
        return true;
    }

    return BikeRecorder_SendCommand(BIKE_RECORDER_COMMAND_POINT, pGnss);
}

/* BIKE_RECORDER_GetSnapshot: 获取一致性轨迹状态快照。
 * 参数：
 *   - pSnapshot: 输出快照
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_RECORDER_GetSnapshot(BIKE_RECORDER_SNAPSHOT *pSnapshot)
{
    if ((NULL == pSnapshot) || (!BikeRecorder_Lock()))
    {
        return false;
    }

    *pSnapshot = l_tBikeRecorderSnapshot;
    BikeRecorder_Unlock();

    return true;
}
