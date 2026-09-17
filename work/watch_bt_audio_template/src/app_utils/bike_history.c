#include "bike_history.h"

#include <stddef.h>
#include <string.h>

#define BIKE_HISTORY_FNV_OFFSET (2166136261UL)
#define BIKE_HISTORY_FNV_PRIME (16777619UL)

/* BikeHistory_Checksum: 计算记录整体的 FNV-1a 校验和。
 * 参数：
 *   - pRecord: 待校验记录
 * 返回值：校验和；空指针返回 0
 */
static uint32_t BikeHistory_Checksum(const BIKE_HISTORY_RECORD *pRecord)
{
    BIKE_HISTORY_RECORD tCopy;
    const uint8_t *pData;
    uint32_t ulChecksum;
    size_t ulIndex;

    if (NULL == pRecord)
    {
        return 0U;
    }
    tCopy = *pRecord;
    tCopy.ulChecksum = 0U;
    pData = (const uint8_t *)&tCopy;
    ulChecksum = BIKE_HISTORY_FNV_OFFSET;
    for (ulIndex = 0U; ulIndex < sizeof(tCopy); ulIndex++)
    {
        ulChecksum ^= pData[ulIndex];
        ulChecksum *= BIKE_HISTORY_FNV_PRIME;
    }

    return ulChecksum;
}

/* BikeHistory_AddSaturated: 64 位无符号饱和加法。 */
static uint64_t BikeHistory_AddSaturated(uint64_t udFirst, uint64_t udSecond)
{
    if (UINT64_MAX - udFirst < udSecond)
    {
        return UINT64_MAX;
    }

    return udFirst + udSecond;
}

/* BikeHistory_IsFirstSequenceNewer: 使用无符号差值判断支持自然回绕的序号新旧。 */
static bool BikeHistory_IsFirstSequenceNewer(uint32_t ulFirst,
                                             uint32_t ulSecond)
{
    uint32_t ulDifference;

    ulDifference = ulFirst - ulSecond;
    return (0U != ulDifference) && (0x80000000UL > ulDifference);
}

/* BIKE_HISTORY_InitRecord: 初始化一份可校验的空累计记录。 */
void BIKE_HISTORY_InitRecord(BIKE_HISTORY_RECORD *pRecord)
{
    if (NULL != pRecord)
    {
        (void)memset(pRecord, 0, sizeof(*pRecord));
        pRecord->ulVersion = BIKE_HISTORY_RECORD_VERSION;
        pRecord->ulChecksum = BikeHistory_Checksum(pRecord);
    }

    return;
}

/* BIKE_HISTORY_IsRecordValid: 校验版本、保留字段和整体校验和。 */
bool BIKE_HISTORY_IsRecordValid(const BIKE_HISTORY_RECORD *pRecord)
{
    return (NULL != pRecord) &&
           (BIKE_HISTORY_RECORD_VERSION == pRecord->ulVersion) &&
           (0U == pRecord->usReserved) &&
           (pRecord->ulChecksum == BikeHistory_Checksum(pRecord));
}

/* BIKE_HISTORY_MergeRide: 将一次有效骑行饱和合并到累计记录。 */
bool BIKE_HISTORY_MergeRide(BIKE_HISTORY_RECORD *pRecord,
                            const BIKE_RIDE_STATE *pRide)
{
    if ((NULL == pRecord) || (NULL == pRide) ||
        (!BIKE_HISTORY_IsRecordValid(pRecord)) ||
        ((0U == pRide->ulDistanceMm) && (0U == pRide->ulMovingTimeMs) &&
         (0U == pRide->ulElapsedTimeMs) &&
         (0U == pRide->ulCaloriesMilliKcal)))
    {
        return false;
    }
    pRecord->udDistanceMm = BikeHistory_AddSaturated(
        pRecord->udDistanceMm, pRide->ulDistanceMm);
    pRecord->udMovingTimeMs = BikeHistory_AddSaturated(
        pRecord->udMovingTimeMs, pRide->ulMovingTimeMs);
    pRecord->udElapsedTimeMs = BikeHistory_AddSaturated(
        pRecord->udElapsedTimeMs, pRide->ulElapsedTimeMs);
    pRecord->udCaloriesMilliKcal = BikeHistory_AddSaturated(
        pRecord->udCaloriesMilliKcal, pRide->ulCaloriesMilliKcal);
    if (pRecord->usMaximumSpeedCentiKph < pRide->usMaximumSpeedCentiKph)
    {
        pRecord->usMaximumSpeedCentiKph =
            pRide->usMaximumSpeedCentiKph;
    }
    if (UINT32_MAX > pRecord->ulRideCount)
    {
        pRecord->ulRideCount++;
    }
    pRecord->ulSequence++;
    pRecord->ulChecksum = BikeHistory_Checksum(pRecord);

    return true;
}

/* BIKE_HISTORY_SelectRecord: 从 A/B 槽中选择校验通过且序号最新的记录。 */
bool BIKE_HISTORY_SelectRecord(const BIKE_HISTORY_RECORD *pFirst,
                               const BIKE_HISTORY_RECORD *pSecond,
                               BIKE_HISTORY_RECORD *pSelected)
{
    bool bFirstValid;
    bool bSecondValid;

    if (NULL == pSelected)
    {
        return false;
    }
    bFirstValid = BIKE_HISTORY_IsRecordValid(pFirst);
    bSecondValid = BIKE_HISTORY_IsRecordValid(pSecond);
    if ((!bFirstValid) && (!bSecondValid))
    {
        BIKE_HISTORY_InitRecord(pSelected);
        return false;
    }
    if (bFirstValid &&
        ((!bSecondValid) ||
         BikeHistory_IsFirstSequenceNewer(pFirst->ulSequence,
                                          pSecond->ulSequence)))
    {
        *pSelected = *pFirst;
    }
    else
    {
        *pSelected = *pSecond;
    }

    return true;
}

#ifndef BIKE_HISTORY_HOST_BUILD

#include <rtthread.h>

#ifdef BSP_SHARE_PREFS
#include "share_prefs.h"
#endif

#define LOG_TAG "bike.history"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#define BIKE_HISTORY_SLOT_FIRST_KEY "slot_a"
#define BIKE_HISTORY_SLOT_SECOND_KEY "slot_b"
#define BIKE_HISTORY_THREAD_STACK_SIZE (1536U)
#define BIKE_HISTORY_THREAD_PRIORITY (25U)
#define BIKE_HISTORY_QUEUE_DEPTH (6U)

/* BIKE_HISTORY_ACTION: 持久化工作线程处理的顺序动作。 */
typedef enum _BIKE_HISTORY_ACTION
{
    BIKE_HISTORY_ACTION_BEGIN = 0,
    BIKE_HISTORY_ACTION_CHECKPOINT,
    BIKE_HISTORY_ACTION_FINISH,
    BIKE_HISTORY_ACTION_DISCARD
} BIKE_HISTORY_ACTION;

/* BIKE_HISTORY_MESSAGE: 固定队列中的骑行快照消息。 */
typedef struct _BIKE_HISTORY_MESSAGE
{
    BIKE_HISTORY_ACTION eAction;
    BIKE_RIDE_STATE tRide;
} BIKE_HISTORY_MESSAGE;

/* l_aBikeHistoryPrefName: 固定 32 字节的 FlashDB 命名空间名。 */
static const char l_aBikeHistoryPrefName[32] = "bike_history_double_slot_v1";

/* l_tBikeHistoryMutex: 累计记录、骑行基线和槽位状态互斥锁。 */
static struct rt_mutex l_tBikeHistoryMutex;

/* l_tBikeHistoryThread/l_aBikeHistoryThreadStack: 避免在 GNSS 或 GUI 回调中写 Flash 的静态工作线程。 */
static struct rt_thread l_tBikeHistoryThread;
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikeHistoryThreadStack[BIKE_HISTORY_THREAD_STACK_SIZE];

/* l_tBikeHistoryQueue/l_aBikeHistoryQueuePool: 固定深度非阻塞持久化队列。 */
static struct rt_messagequeue l_tBikeHistoryQueue;
static uint8_t l_aBikeHistoryQueuePool[BIKE_HISTORY_QUEUE_DEPTH *
                                       sizeof(BIKE_HISTORY_MESSAGE)];

/* l_tBikeHistoryCurrent: 当前有效的累计记录。 */
static BIKE_HISTORY_RECORD l_tBikeHistoryCurrent;

/* l_tBikeHistoryBaseline: 当前骑行开始时的累计基线，用于绝对检查点和丢弃回滚。 */
static BIKE_HISTORY_RECORD l_tBikeHistoryBaseline;

/* l_bBikeHistoryReady/l_bBikeHistoryStorageReady: 模块与存储可用状态。 */
static bool l_bBikeHistoryReady;
static bool l_bBikeHistoryStorageReady;

/* l_bBikeHistoryFirstSlot: 当前有效记录是否来自 A 槽。 */
static bool l_bBikeHistoryFirstSlot;

/* l_bBikeHistoryRideActive/l_bBikeHistoryCheckpointed: 当前骑行及是否已落盘检查点。 */
static bool l_bBikeHistoryRideActive;
static bool l_bBikeHistoryCheckpointed;

#ifdef BSP_SHARE_PREFS
/* l_pBikeHistoryPrefs: 双槽 FlashDB 偏好句柄。 */
static share_prefs_t *l_pBikeHistoryPrefs;
#endif

/* BikeHistory_ThreadEntry: 顺序处理骑行基线、检查点、结束和丢弃。 */
static void BikeHistory_ThreadEntry(void *pParameter)
{
    BIKE_HISTORY_MESSAGE tMessage;
    rt_err_t eResult;

    (void)pParameter;
    while (true)
    {
        eResult = rt_mq_recv(&l_tBikeHistoryQueue, &tMessage,
                             sizeof(tMessage), RT_WAITING_FOREVER);
        if (RT_EOK != eResult)
        {
            LOG_E("queue receive failed: %d", eResult);
            continue;
        }
        switch (tMessage.eAction)
        {
        case BIKE_HISTORY_ACTION_BEGIN:
            eResult = BIKE_HISTORY_BeginRide();
            break;

        case BIKE_HISTORY_ACTION_CHECKPOINT:
            eResult = BIKE_HISTORY_CheckpointRide(&tMessage.tRide);
            break;

        case BIKE_HISTORY_ACTION_FINISH:
            eResult = BIKE_HISTORY_FinishRide(&tMessage.tRide);
            break;

        case BIKE_HISTORY_ACTION_DISCARD:
            eResult = BIKE_HISTORY_DiscardRide();
            break;

        default:
            eResult = -RT_EINVAL;
            break;
        }
        if (RT_EOK != eResult)
        {
            LOG_E("action %u failed: %d", (unsigned int)tMessage.eAction,
                  eResult);
        }
    }
}

/* BikeHistory_Unlock: 释放累计数据互斥锁。 */
static void BikeHistory_Unlock(void)
{
    rt_err_t eResult;

    eResult = rt_mutex_release(&l_tBikeHistoryMutex);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex release failed: %d", eResult);
    }

    return;
}

/* BikeHistory_WriteLocked: 将候选记录写入非当前槽，成功后切换当前槽。 */
static rt_err_t BikeHistory_WriteLocked(const BIKE_HISTORY_RECORD *pRecord)
{
    rt_err_t eResult;

    if ((NULL == pRecord) || (!BIKE_HISTORY_IsRecordValid(pRecord)))
    {
        return -RT_EINVAL;
    }
#ifdef BSP_SHARE_PREFS
    if (NULL != l_pBikeHistoryPrefs)
    {
        eResult = share_prefs_set_block(
            l_pBikeHistoryPrefs,
            l_bBikeHistoryFirstSlot ? BIKE_HISTORY_SLOT_SECOND_KEY :
                                      BIKE_HISTORY_SLOT_FIRST_KEY,
            pRecord, (int32_t)sizeof(*pRecord));
        if (RT_EOK == eResult)
        {
            l_tBikeHistoryCurrent = *pRecord;
            l_bBikeHistoryFirstSlot = !l_bBikeHistoryFirstSlot;
        }
        return eResult;
    }
#endif
    eResult = -RT_ERROR;
    return eResult;
}

/* BikeHistory_BuildRideLocked: 用骑行基线和当前本次统计生成绝对累计记录。 */
static bool BikeHistory_BuildRideLocked(const BIKE_RIDE_STATE *pRide,
                                        BIKE_HISTORY_RECORD *pRecord)
{
    if ((NULL == pRide) || (NULL == pRecord) ||
        (!l_bBikeHistoryRideActive))
    {
        return false;
    }
    *pRecord = l_tBikeHistoryBaseline;
    if (!BIKE_HISTORY_MergeRide(pRecord, pRide))
    {
        return false;
    }
    pRecord->ulSequence = l_tBikeHistoryCurrent.ulSequence + 1U;
    pRecord->ulChecksum = BikeHistory_Checksum(pRecord);

    return true;
}

/* BIKE_HISTORY_Init: 打开双槽命名空间并选择最新有效记录。 */
rt_err_t BIKE_HISTORY_Init(void)
{
    BIKE_HISTORY_RECORD tFirst;
    BIKE_HISTORY_RECORD tSecond;
    int32_t lFirstLength;
    int32_t lSecondLength;
    rt_err_t eResult;
    bool bFirstValid;
    bool bSecondValid;

    if (l_bBikeHistoryReady)
    {
        return RT_EOK;
    }
    eResult = rt_mutex_init(&l_tBikeHistoryMutex, "bikehist",
                            RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    BIKE_HISTORY_InitRecord(&l_tBikeHistoryCurrent);
    BIKE_HISTORY_InitRecord(&l_tBikeHistoryBaseline);
    l_bBikeHistoryStorageReady = false;
    l_bBikeHistoryFirstSlot = false;
    l_bBikeHistoryRideActive = false;
    l_bBikeHistoryCheckpointed = false;
    lFirstLength = -1;
    lSecondLength = -1;
    (void)memset(&tFirst, 0, sizeof(tFirst));
    (void)memset(&tSecond, 0, sizeof(tSecond));
#ifdef BSP_SHARE_PREFS
    l_pBikeHistoryPrefs = share_prefs_open(l_aBikeHistoryPrefName,
                                           SHAREPREFS_MODE_PRIVATE);
    if (NULL != l_pBikeHistoryPrefs)
    {
        l_bBikeHistoryStorageReady = true;
        lFirstLength = share_prefs_get_block(
            l_pBikeHistoryPrefs, BIKE_HISTORY_SLOT_FIRST_KEY,
            &tFirst, (int32_t)sizeof(tFirst));
        lSecondLength = share_prefs_get_block(
            l_pBikeHistoryPrefs, BIKE_HISTORY_SLOT_SECOND_KEY,
            &tSecond, (int32_t)sizeof(tSecond));
    }
#endif
    bFirstValid = ((int32_t)sizeof(tFirst) == lFirstLength) &&
                  BIKE_HISTORY_IsRecordValid(&tFirst);
    bSecondValid = ((int32_t)sizeof(tSecond) == lSecondLength) &&
                   BIKE_HISTORY_IsRecordValid(&tSecond);
    if (bFirstValid || bSecondValid)
    {
        (void)BIKE_HISTORY_SelectRecord(
            bFirstValid ? &tFirst : NULL,
            bSecondValid ? &tSecond : NULL, &l_tBikeHistoryCurrent);
        l_bBikeHistoryFirstSlot = bFirstValid &&
            ((!bSecondValid) ||
             BikeHistory_IsFirstSequenceNewer(tFirst.ulSequence,
                                               tSecond.ulSequence));
    }
    eResult = rt_mq_init(&l_tBikeHistoryQueue, "bikehistq",
                         l_aBikeHistoryQueuePool,
                         sizeof(BIKE_HISTORY_MESSAGE),
                         sizeof(l_aBikeHistoryQueuePool), RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        (void)rt_mutex_detach(&l_tBikeHistoryMutex);
#ifdef BSP_SHARE_PREFS
        if (NULL != l_pBikeHistoryPrefs)
        {
            (void)share_prefs_close(l_pBikeHistoryPrefs);
            l_pBikeHistoryPrefs = NULL;
        }
#endif
        return eResult;
    }
    eResult = rt_thread_init(&l_tBikeHistoryThread, "bikehist",
                             BikeHistory_ThreadEntry, NULL,
                             l_aBikeHistoryThreadStack,
                             sizeof(l_aBikeHistoryThreadStack),
                             BIKE_HISTORY_THREAD_PRIORITY, 10U);
    if (RT_EOK != eResult)
    {
        (void)rt_mq_detach(&l_tBikeHistoryQueue);
        (void)rt_mutex_detach(&l_tBikeHistoryMutex);
#ifdef BSP_SHARE_PREFS
        if (NULL != l_pBikeHistoryPrefs)
        {
            (void)share_prefs_close(l_pBikeHistoryPrefs);
            l_pBikeHistoryPrefs = NULL;
        }
#endif
        return eResult;
    }
    l_bBikeHistoryReady = true;
    eResult = rt_thread_startup(&l_tBikeHistoryThread);
    if (RT_EOK != eResult)
    {
        l_bBikeHistoryReady = false;
        (void)rt_thread_detach(&l_tBikeHistoryThread);
        (void)rt_mq_detach(&l_tBikeHistoryQueue);
        (void)rt_mutex_detach(&l_tBikeHistoryMutex);
#ifdef BSP_SHARE_PREFS
        if (NULL != l_pBikeHistoryPrefs)
        {
            (void)share_prefs_close(l_pBikeHistoryPrefs);
            l_pBikeHistoryPrefs = NULL;
        }
#endif
        LOG_E("worker init failed: %d", eResult);
        return eResult;
    }
    LOG_I("ready storage=%u seq=%lu rides=%lu distance=%llu",
          l_bBikeHistoryStorageReady,
          (unsigned long)l_tBikeHistoryCurrent.ulSequence,
          (unsigned long)l_tBikeHistoryCurrent.ulRideCount,
          (unsigned long long)l_tBikeHistoryCurrent.udDistanceMm);

    return RT_EOK;
}

/* BIKE_HISTORY_GetSnapshot: 获取线程安全的历史累计快照。 */
rt_err_t BIKE_HISTORY_GetSnapshot(BIKE_HISTORY_SNAPSHOT *pSnapshot)
{
    rt_err_t eResult;

    if (NULL == pSnapshot)
    {
        return -RT_EINVAL;
    }
    eResult = BIKE_HISTORY_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeHistoryMutex, RT_WAITING_NO);
    if (RT_EOK == eResult)
    {
        pSnapshot->tRecord = l_tBikeHistoryCurrent;
        pSnapshot->bStorageReady = l_bBikeHistoryStorageReady;
        pSnapshot->bRideActive = l_bBikeHistoryRideActive;
        BikeHistory_Unlock();
    }

    return eResult;
}

/* BIKE_HISTORY_BeginRide: 捕获新骑行的累计基线。 */
rt_err_t BIKE_HISTORY_BeginRide(void)
{
    rt_err_t eResult;

    eResult = BIKE_HISTORY_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeHistoryMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
        l_tBikeHistoryBaseline = l_tBikeHistoryCurrent;
        l_bBikeHistoryRideActive = true;
        l_bBikeHistoryCheckpointed = false;
        BikeHistory_Unlock();
    }

    return eResult;
}

/* BikeHistory_SaveRide: 保存检查点或最终统计。 */
static rt_err_t BikeHistory_SaveRide(const BIKE_RIDE_STATE *pRide,
                                     bool bFinish)
{
    BIKE_HISTORY_RECORD tCandidate;
    rt_err_t eResult;

    if (NULL == pRide)
    {
        return -RT_EINVAL;
    }
    eResult = BIKE_HISTORY_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeHistoryMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
        if ((!l_bBikeHistoryRideActive) ||
            (!BIKE_HISTORY_IsRecordValid(&l_tBikeHistoryBaseline)))
        {
            eResult = -RT_ERROR;
        }
        else if ((0U == pRide->ulDistanceMm) &&
                 (0U == pRide->ulMovingTimeMs) &&
                 (0U == pRide->ulElapsedTimeMs) &&
                 (0U == pRide->ulCaloriesMilliKcal))
        {
            if (bFinish)
            {
                l_bBikeHistoryRideActive = false;
            }
        }
        else if (!BikeHistory_BuildRideLocked(pRide, &tCandidate))
        {
            eResult = -RT_EINVAL;
        }
        else
        {
            eResult = BikeHistory_WriteLocked(&tCandidate);
            if (RT_EOK == eResult)
            {
                l_bBikeHistoryCheckpointed = true;
                if (bFinish)
                {
                    l_bBikeHistoryRideActive = false;
                }
            }
        }
        BikeHistory_Unlock();
    }

    return eResult;
}

rt_err_t BIKE_HISTORY_CheckpointRide(const BIKE_RIDE_STATE *pRide)
{
    return BikeHistory_SaveRide(pRide, false);
}

rt_err_t BIKE_HISTORY_FinishRide(const BIKE_RIDE_STATE *pRide)
{
    return BikeHistory_SaveRide(pRide, true);
}

/* BIKE_HISTORY_DiscardRide: 丢弃骑行时把已写检查点回滚到开始基线。 */
rt_err_t BIKE_HISTORY_DiscardRide(void)
{
    BIKE_HISTORY_RECORD tCandidate;
    rt_err_t eResult;

    eResult = BIKE_HISTORY_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeHistoryMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
        if (!l_bBikeHistoryRideActive)
        {
            eResult = -RT_ERROR;
        }
        else if (!l_bBikeHistoryCheckpointed)
        {
            l_bBikeHistoryRideActive = false;
        }
        else
        {
            tCandidate = l_tBikeHistoryBaseline;
            tCandidate.ulSequence = l_tBikeHistoryCurrent.ulSequence + 1U;
            tCandidate.ulChecksum = BikeHistory_Checksum(&tCandidate);
            eResult = BikeHistory_WriteLocked(&tCandidate);
            if (RT_EOK == eResult)
            {
                l_bBikeHistoryRideActive = false;
                l_bBikeHistoryCheckpointed = false;
            }
        }
        BikeHistory_Unlock();
    }

    return eResult;
}

/* BikeHistory_Post: 非阻塞投递持久化工作，队列满时由调用者显式处理。 */
static bool BikeHistory_Post(BIKE_HISTORY_ACTION eAction,
                             const BIKE_RIDE_STATE *pRide)
{
    BIKE_HISTORY_MESSAGE tMessage;

    if (RT_EOK != BIKE_HISTORY_Init())
    {
        return false;
    }
    (void)memset(&tMessage, 0, sizeof(tMessage));
    tMessage.eAction = eAction;
    if (NULL != pRide)
    {
        tMessage.tRide = *pRide;
    }

    return RT_EOK == rt_mq_send(&l_tBikeHistoryQueue, &tMessage,
                                sizeof(tMessage));
}

/* BIKE_HISTORY_RequestBeginRide: 非阻塞投递新骑行基线捕获请求。 */
bool BIKE_HISTORY_RequestBeginRide(void)
{
    return BikeHistory_Post(BIKE_HISTORY_ACTION_BEGIN, NULL);
}

/* BIKE_HISTORY_RequestCheckpoint: 非阻塞投递本次骑行检查点快照。 */
bool BIKE_HISTORY_RequestCheckpoint(const BIKE_RIDE_STATE *pRide)
{
    if (NULL == pRide)
    {
        return false;
    }
    return BikeHistory_Post(BIKE_HISTORY_ACTION_CHECKPOINT, pRide);
}

/* BIKE_HISTORY_RequestFinish: 非阻塞投递本次骑行最终快照。 */
bool BIKE_HISTORY_RequestFinish(const BIKE_RIDE_STATE *pRide)
{
    if (NULL == pRide)
    {
        return false;
    }
    return BikeHistory_Post(BIKE_HISTORY_ACTION_FINISH, pRide);
}

/* BIKE_HISTORY_RequestDiscard: 非阻塞请求回滚当前骑行已写检查点。 */
bool BIKE_HISTORY_RequestDiscard(void)
{
    return BikeHistory_Post(BIKE_HISTORY_ACTION_DISCARD, NULL);
}

#endif /* BIKE_HISTORY_HOST_BUILD */
