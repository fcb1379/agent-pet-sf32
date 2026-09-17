#ifndef BIKE_HISTORY_H
#define BIKE_HISTORY_H

#include <stdbool.h>
#include <stdint.h>

#include "bike_ride_model.h"

#ifndef BIKE_HISTORY_HOST_BUILD
#include <rtdef.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_HISTORY_RECORD_VERSION (0x00010001UL)

/* BIKE_HISTORY_RECORD: 带版本、序号和校验和的累计骑行记录。
 * 成员说明：
 *   - udDistanceMm: 历史总里程，单位毫米
 *   - udMovingTimeMs/udElapsedTimeMs: 历史移动/总时间，单位毫秒
 *   - udCaloriesMilliKcal: 历史估算消耗，单位 0.001 kcal
 *   - ulVersion: 持久化结构版本
 *   - ulSequence: A/B 双槽单调序号，支持自然回绕
 *   - ulRideCount: 已计入累计值的骑行次数
 *   - ulChecksum: 整个记录在该字段置零时的 FNV-1a 校验和
 *   - usMaximumSpeedCentiKph: 历史最高速度，单位 0.01 km/h
 *   - usReserved: 保留并固定为 0
 */
typedef struct _BIKE_HISTORY_RECORD
{
    uint64_t udDistanceMm;
    uint64_t udMovingTimeMs;
    uint64_t udElapsedTimeMs;
    uint64_t udCaloriesMilliKcal;
    uint32_t ulVersion;
    uint32_t ulSequence;
    uint32_t ulRideCount;
    uint32_t ulChecksum;
    uint16_t usMaximumSpeedCentiKph;
    uint16_t usReserved;
} BIKE_HISTORY_RECORD;

/* BIKE_HISTORY_SNAPSHOT: UI/调试使用的累计数据快照。 */
typedef struct _BIKE_HISTORY_SNAPSHOT
{
    BIKE_HISTORY_RECORD tRecord;
    bool bStorageReady;
    bool bRideActive;
} BIKE_HISTORY_SNAPSHOT;

void BIKE_HISTORY_InitRecord(BIKE_HISTORY_RECORD *pRecord);
bool BIKE_HISTORY_IsRecordValid(const BIKE_HISTORY_RECORD *pRecord);
bool BIKE_HISTORY_MergeRide(BIKE_HISTORY_RECORD *pRecord,
                            const BIKE_RIDE_STATE *pRide);
bool BIKE_HISTORY_SelectRecord(const BIKE_HISTORY_RECORD *pFirst,
                               const BIKE_HISTORY_RECORD *pSecond,
                               BIKE_HISTORY_RECORD *pSelected);

#ifndef BIKE_HISTORY_HOST_BUILD
rt_err_t BIKE_HISTORY_Init(void);
rt_err_t BIKE_HISTORY_GetSnapshot(BIKE_HISTORY_SNAPSHOT *pSnapshot);
rt_err_t BIKE_HISTORY_BeginRide(void);
rt_err_t BIKE_HISTORY_CheckpointRide(const BIKE_RIDE_STATE *pRide);
rt_err_t BIKE_HISTORY_FinishRide(const BIKE_RIDE_STATE *pRide);
rt_err_t BIKE_HISTORY_DiscardRide(void);
bool BIKE_HISTORY_RequestBeginRide(void);
bool BIKE_HISTORY_RequestCheckpoint(const BIKE_RIDE_STATE *pRide);
bool BIKE_HISTORY_RequestFinish(const BIKE_RIDE_STATE *pRide);
bool BIKE_HISTORY_RequestDiscard(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* BIKE_HISTORY_H */
