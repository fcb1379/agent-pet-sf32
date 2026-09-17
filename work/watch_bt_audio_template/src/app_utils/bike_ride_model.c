#include "bike_ride_model.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define BIKE_RIDE_EARTH_RADIUS_M (6371000.0)
#define BIKE_RIDE_DEGREE_TO_RADIAN (0.017453292519943295)
#define BIKE_RIDE_MIN_MOVING_SPEED_CM_S (80U)
#define BIKE_RIDE_MAX_VALID_SPEED_CM_S (6000U)
#define BIKE_RIDE_MAX_SAMPLE_INTERVAL_MS (5000U)
#define BIKE_RIDE_MAX_SEGMENT_MM (100000U)

/* BIKE_RIDE_CalculateDistanceMm: 使用 Haversine 计算两点表面距离。
 * 参数：
 *   - lLatitude1E7/lLongitude1E7: 起点坐标，单位 1e-7 度
 *   - lLatitude2E7/lLongitude2E7: 终点坐标，单位 1e-7 度
 * 返回值：距离毫米数；超过 uint32_t 范围时返回 UINT32_MAX
 */
uint32_t BIKE_RIDE_CalculateDistanceMm(int32_t lLatitude1E7, int32_t lLongitude1E7,
                                      int32_t lLatitude2E7, int32_t lLongitude2E7)
{
    double dLatitude1;
    double dLatitude2;
    double dDeltaLatitude;
    double dDeltaLongitude;
    double dSinLatitude;
    double dSinLongitude;
    double dHaversine;
    double dDistanceMm;
    uint32_t ulDistanceMm;

    dLatitude1 = ((double)lLatitude1E7 / 10000000.0) * BIKE_RIDE_DEGREE_TO_RADIAN;
    dLatitude2 = ((double)lLatitude2E7 / 10000000.0) * BIKE_RIDE_DEGREE_TO_RADIAN;
    dDeltaLatitude = dLatitude2 - dLatitude1;
    dDeltaLongitude = ((double)(lLongitude2E7 - lLongitude1E7) / 10000000.0) *
                      BIKE_RIDE_DEGREE_TO_RADIAN;
    dSinLatitude = sin(dDeltaLatitude / 2.0);
    dSinLongitude = sin(dDeltaLongitude / 2.0);
    dHaversine = (dSinLatitude * dSinLatitude) +
                 (cos(dLatitude1) * cos(dLatitude2) * dSinLongitude * dSinLongitude);
    if (1.0 < dHaversine)
    {
        dHaversine = 1.0;
    }

    dDistanceMm = 2.0 * BIKE_RIDE_EARTH_RADIUS_M * atan2(sqrt(dHaversine), sqrt(1.0 - dHaversine)) * 1000.0;
    if (0.0 >= dDistanceMm)
    {
        ulDistanceMm = 0U;
    }
    else if ((double)UINT32_MAX <= dDistanceMm)
    {
        ulDistanceMm = UINT32_MAX;
    }
    else
    {
        ulDistanceMm = (uint32_t)(dDistanceMm + 0.5);
    }

    return ulDistanceMm;
}

/* BikeRide_UpdateStatistics: 更新平均速度、最大速度和卡路里估算。
 * 参数：
 *   - pState: 骑行状态
 *   - ulDeltaMs: 当前有效样本与上一样本的间隔
 * 返回值：无
 */
static void BikeRide_UpdateStatistics(BIKE_RIDE_STATE *pState, uint32_t ulDeltaMs)
{
    uint64_t udAverageSpeed;
    uint64_t udCalories;

    if (NULL == pState)
    {
        return;
    }

    if (pState->usSpeedCentiKph > pState->usMaximumSpeedCentiKph)
    {
        pState->usMaximumSpeedCentiKph = pState->usSpeedCentiKph;
    }

    if (0U < pState->ulMovingTimeMs)
    {
        udAverageSpeed = ((uint64_t)pState->ulDistanceMm * 360ULL) /
                         (uint64_t)pState->ulMovingTimeMs;
        if (UINT16_MAX < udAverageSpeed)
        {
            udAverageSpeed = UINT16_MAX;
        }
        pState->usAverageSpeedCentiKph = (uint16_t)udAverageSpeed;
    }

    /* 沿用 X-TRACK 的轻量估算关系：speed(km/h) * weight * 0.5 * hours。 */
    udCalories = (uint64_t)pState->usSpeedCentiKph * (uint64_t)pState->ucWeightKg *
                 (uint64_t)ulDeltaMs;
    udCalories /= 720000ULL;
    if ((uint64_t)UINT32_MAX - pState->ulCaloriesMilliKcal < udCalories)
    {
        pState->ulCaloriesMilliKcal = UINT32_MAX;
    }
    else
    {
        pState->ulCaloriesMilliKcal += (uint32_t)udCalories;
    }

    return;
}

/* BIKE_RIDE_Init: 初始化骑行状态。
 * 参数：
 *   - pState: 骑行状态
 *   - ucWeightKg: 骑手体重，0 时使用默认值
 * 返回值：无
 */
void BIKE_RIDE_Init(BIKE_RIDE_STATE *pState, uint8_t ucWeightKg)
{
    if (NULL != pState)
    {
        (void)memset(pState, 0, sizeof(*pState));
        pState->eMode = BIKE_RIDE_MODE_STOPPED;
        pState->ucWeightKg = (0U == ucWeightKg) ? BIKE_RIDE_DEFAULT_WEIGHT_KG : ucWeightKg;
    }

    return;
}

/* BIKE_RIDE_Start: 开始新骑行或从暂停恢复。
 * 参数：
 *   - pState: 骑行状态
 *   - ulNowMs: 当前单调时钟毫秒数
 * 返回值：无
 */
void BIKE_RIDE_Start(BIKE_RIDE_STATE *pState, uint32_t ulNowMs)
{
    uint8_t ucWeightKg;

    if (NULL == pState)
    {
        return;
    }

    if (BIKE_RIDE_MODE_STOPPED == pState->eMode)
    {
        ucWeightKg = pState->ucWeightKg;
        BIKE_RIDE_Init(pState, ucWeightKg);
    }

    pState->eMode = BIKE_RIDE_MODE_RUNNING;
    pState->bHasPreviousPoint = false;
    pState->ulLastSampleMs = ulNowMs;

    return;
}

/* BIKE_RIDE_Pause: 暂停当前骑行并断开里程点链。
 * 参数：
 *   - pState: 骑行状态
 * 返回值：无
 */
void BIKE_RIDE_Pause(BIKE_RIDE_STATE *pState)
{
    if ((NULL != pState) && (BIKE_RIDE_MODE_RUNNING == pState->eMode))
    {
        pState->eMode = BIKE_RIDE_MODE_PAUSED;
        pState->bHasPreviousPoint = false;
        pState->usSpeedCentiKph = 0U;
    }

    return;
}

/* BIKE_RIDE_Stop: 结束当前骑行并保留统计供总结显示。
 * 参数：
 *   - pState: 骑行状态
 * 返回值：无
 */
void BIKE_RIDE_Stop(BIKE_RIDE_STATE *pState)
{
    if (NULL != pState)
    {
        pState->eMode = BIKE_RIDE_MODE_STOPPED;
        pState->bHasPreviousPoint = false;
        pState->usSpeedCentiKph = 0U;
    }

    return;
}

/* BIKE_RIDE_Update: 用一帧 RMC/GGA 合并快照更新骑行统计。
 * 参数：
 *   - pState: 骑行状态
 *   - pGnss: GNSS 快照，仅输入
 *   - ulNowMs: 当前单调时钟毫秒数
 * 返回值：无
 */
void BIKE_RIDE_Update(BIKE_RIDE_STATE *pState, const BIKE_GNSS_DATA *pGnss, uint32_t ulNowMs)
{
    uint32_t ulDeltaMs;
    uint32_t ulSegmentMm;
    uint32_t ulSpeedCentiKph;

    if ((NULL == pState) || (NULL == pGnss))
    {
        return;
    }

    pState->bFixValid = pGnss->bFixValid;
    pState->ucSatellites = pGnss->ucSatellites;
    pState->lAltitudeCm = pGnss->lAltitudeCm;
    pState->usCourseDeg10 = pGnss->usCourseDeg10;

    if ((!pGnss->bFixValid) || (BIKE_RIDE_MAX_VALID_SPEED_CM_S < pGnss->ulSpeedCmPerSec))
    {
        pState->usSpeedCentiKph = 0U;
        pState->bHasPreviousPoint = false;
        pState->ulLastSampleMs = ulNowMs;
        return;
    }

    ulSpeedCentiKph = (pGnss->ulSpeedCmPerSec * 36U + 5U) / 10U;
    if (UINT16_MAX < ulSpeedCentiKph)
    {
        ulSpeedCentiKph = UINT16_MAX;
    }
    pState->usSpeedCentiKph = (uint16_t)ulSpeedCentiKph;

    ulDeltaMs = ulNowMs - pState->ulLastSampleMs;
    pState->ulLastSampleMs = ulNowMs;
    if (BIKE_RIDE_MODE_RUNNING != pState->eMode)
    {
        pState->bHasPreviousPoint = false;
        return;
    }

    if (BIKE_RIDE_MAX_SAMPLE_INTERVAL_MS < ulDeltaMs)
    {
        pState->bHasPreviousPoint = false;
        return;
    }

    if (UINT32_MAX - pState->ulElapsedTimeMs < ulDeltaMs)
    {
        pState->ulElapsedTimeMs = UINT32_MAX;
    }
    else
    {
        pState->ulElapsedTimeMs += ulDeltaMs;
    }

    if (pGnss->ulSpeedCmPerSec >= BIKE_RIDE_MIN_MOVING_SPEED_CM_S)
    {
        if (UINT32_MAX - pState->ulMovingTimeMs < ulDeltaMs)
        {
            pState->ulMovingTimeMs = UINT32_MAX;
        }
        else
        {
            pState->ulMovingTimeMs += ulDeltaMs;
        }

        if (pState->bHasPreviousPoint)
        {
            ulSegmentMm = BIKE_RIDE_CalculateDistanceMm(pState->lPreviousLatitudeE7,
                                                       pState->lPreviousLongitudeE7,
                                                       pGnss->lLatitudeE7,
                                                       pGnss->lLongitudeE7);
            if ((BIKE_RIDE_MAX_SEGMENT_MM >= ulSegmentMm) &&
                    (UINT32_MAX - pState->ulDistanceMm >= ulSegmentMm))
            {
                pState->ulDistanceMm += ulSegmentMm;
            }
        }

        BikeRide_UpdateStatistics(pState, ulDeltaMs);
    }

    pState->lPreviousLatitudeE7 = pGnss->lLatitudeE7;
    pState->lPreviousLongitudeE7 = pGnss->lLongitudeE7;
    pState->bHasPreviousPoint = true;

    return;
}

/* BIKE_RIDE_InvalidateFix: GNSS 超时后清除误导性实时数据。
 * 参数：
 *   - pState: 骑行状态
 * 返回值：无
 */
void BIKE_RIDE_InvalidateFix(BIKE_RIDE_STATE *pState)
{
    if (NULL != pState)
    {
        pState->bFixValid = false;
        pState->bHasPreviousPoint = false;
        pState->usSpeedCentiKph = 0U;
    }

    return;
}
