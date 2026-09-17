#ifndef BIKE_RIDE_MODEL_H
#define BIKE_RIDE_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "bike_nmea.h"
#include "bike_speed_source.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_RIDE_DEFAULT_WEIGHT_KG (70U)

/* BIKE_RIDE_MODE: 本次骑行的控制状态。 */
typedef enum _BIKE_RIDE_MODE
{
    BIKE_RIDE_MODE_STOPPED = 0,
    BIKE_RIDE_MODE_RUNNING,
    BIKE_RIDE_MODE_PAUSED
} BIKE_RIDE_MODE;

/* BIKE_RIDE_STATE: 从 X-TRACK SportStatus 重写的固定容量骑行状态。
 * 成员说明：
 *   - eMode: 停止、骑行或暂停状态
 *   - eSpeedSource: 当前速度来源
 *   - bFixValid: 当前 GNSS 数据是否有效
 *   - bHasPreviousPoint: 是否存在可用于累计里程的前一点
 *   - ucSatellites: 当前参与定位卫星数
 *   - ucWeightKg: 卡路里估算使用的骑手体重
 *   - usCourseDeg10: 航向，单位 0.1 度
 *   - usSpeedCentiKph: 当前速度，单位 0.01 km/h
 *   - usAverageSpeedCentiKph: 移动平均速度，单位 0.01 km/h
 *   - usMaximumSpeedCentiKph: 最大速度，单位 0.01 km/h
 *   - lAltitudeCm: 当前海拔，单位厘米
 *   - lPreviousLatitudeE7/lPreviousLongitudeE7: 上一有效点
 *   - ulDistanceMm: 本次累计里程，单位毫米
 *   - ulElapsedTimeMs: 本次总经过时间，单位毫秒
 *   - ulMovingTimeMs: 本次移动时间，单位毫秒
 *   - ulCaloriesMilliKcal: 估算消耗，单位 0.001 kcal
 *   - ulLastSampleMs: 最近一次 RMC 样本时间戳
 */
typedef struct _BIKE_RIDE_STATE
{
    BIKE_RIDE_MODE eMode;
    BIKE_SPEED_SOURCE eSpeedSource;
    bool bFixValid;
    bool bHasPreviousPoint;
    uint8_t ucSatellites;
    uint8_t ucWeightKg;
    uint16_t usCourseDeg10;
    uint16_t usSpeedCentiKph;
    uint16_t usAverageSpeedCentiKph;
    uint16_t usMaximumSpeedCentiKph;
    int32_t lAltitudeCm;
    int32_t lPreviousLatitudeE7;
    int32_t lPreviousLongitudeE7;
    uint32_t ulDistanceMm;
    uint32_t ulElapsedTimeMs;
    uint32_t ulMovingTimeMs;
    uint32_t ulCaloriesMilliKcal;
    uint32_t ulLastSampleMs;
} BIKE_RIDE_STATE;

void BIKE_RIDE_Init(BIKE_RIDE_STATE *pState, uint8_t ucWeightKg);
void BIKE_RIDE_Start(BIKE_RIDE_STATE *pState, uint32_t ulNowMs);
void BIKE_RIDE_Pause(BIKE_RIDE_STATE *pState);
void BIKE_RIDE_Stop(BIKE_RIDE_STATE *pState);
void BIKE_RIDE_Update(BIKE_RIDE_STATE *pState, const BIKE_GNSS_DATA *pGnss, uint32_t ulNowMs);
void BIKE_RIDE_UpdateWithSpeed(BIKE_RIDE_STATE *pState,
                               const BIKE_GNSS_DATA *pGnss,
                               const BIKE_SPEED_SELECTION *pSpeed,
                               uint32_t ulNowMs);
void BIKE_RIDE_InvalidateFix(BIKE_RIDE_STATE *pState);
uint32_t BIKE_RIDE_CalculateDistanceMm(int32_t lLatitude1E7, int32_t lLongitude1E7,
                                      int32_t lLatitude2E7, int32_t lLongitude2E7);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_RIDE_MODEL_H */
