#include "bike_csc.h"

#include <stddef.h>
#include <string.h>

#define BIKE_CSC_EVENT_TIME_HZ (1024ULL)
#define BIKE_CSC_SPEED_SCALE_NUMERATOR (3600ULL * 100ULL)
#define BIKE_CSC_MM_PER_KM (1000000ULL)
#define BIKE_CSC_MAX_SPEED_CENTI_KPH (20000ULL)
#define BIKE_CSC_MAX_CADENCE_RPM (300ULL)

/* BIKE_CSC_Init: 清除 CSC 累计值和派生结果。
 * 参数：
 *   - pState: CSC 状态
 * 返回值：无
 */
void BIKE_CSC_Init(BIKE_CSC_STATE *pState)
{
    if (NULL != pState)
    {
        (void)memset(pState, 0, sizeof(*pState));
    }

    return;
}

/* BikeCsc_UpdateWheel: 根据累计轮转数和 1/1024 秒事件时间计算轮速。
 * 参数：
 *   - pState: CSC 状态
 *   - pMeasurement: 当前测量
 *   - usWheelCircumferenceMm: 轮周，单位毫米
 * 返回值：无
 */
static void BikeCsc_UpdateWheel(BIKE_CSC_STATE *pState,
                                const BIKE_CSC_MEASUREMENT *pMeasurement,
                                uint16_t usWheelCircumferenceMm)
{
    uint32_t ulRevolutionDelta;
    uint16_t usTimeDelta;
    uint64_t udSpeedCentiKph;
    uint64_t udNumerator;

    pState->bWheelSpeedValid = false;
    if (pState->bHasWheelSample)
    {
        ulRevolutionDelta = pMeasurement->ulCumulativeWheelRevolutions -
                            pState->ulPreviousWheelRevolutions;
        usTimeDelta = (uint16_t)(pMeasurement->usLastWheelEventTime -
                                pState->usPreviousWheelEventTime);
        if ((0U < ulRevolutionDelta) && (0U < usTimeDelta))
        {
            udNumerator = (uint64_t)ulRevolutionDelta;
            if ((UINT64_MAX / (uint64_t)usWheelCircumferenceMm >=
                 udNumerator) &&
                (UINT64_MAX / BIKE_CSC_EVENT_TIME_HZ >=
                 (udNumerator * (uint64_t)usWheelCircumferenceMm)))
            {
                udNumerator *= (uint64_t)usWheelCircumferenceMm;
                udNumerator *= BIKE_CSC_EVENT_TIME_HZ;
                if (UINT64_MAX / BIKE_CSC_SPEED_SCALE_NUMERATOR >=
                    udNumerator)
                {
                    udNumerator *= BIKE_CSC_SPEED_SCALE_NUMERATOR;
                    udSpeedCentiKph = udNumerator /
                        (BIKE_CSC_MM_PER_KM * (uint64_t)usTimeDelta);
                    if (BIKE_CSC_MAX_SPEED_CENTI_KPH >= udSpeedCentiKph)
                    {
                        pState->usWheelSpeedCentiKph =
                            (uint16_t)udSpeedCentiKph;
                        pState->bWheelSpeedValid = true;
                    }
                }
            }
        }
    }

    pState->ulPreviousWheelRevolutions = pMeasurement->ulCumulativeWheelRevolutions;
    pState->usPreviousWheelEventTime = pMeasurement->usLastWheelEventTime;
    pState->bHasWheelSample = true;

    return;
}

/* BikeCsc_UpdateCrank: 根据累计曲柄转数和 1/1024 秒事件时间计算踏频。
 * 参数：
 *   - pState: CSC 状态
 *   - pMeasurement: 当前测量
 * 返回值：无
 */
static void BikeCsc_UpdateCrank(BIKE_CSC_STATE *pState,
                                const BIKE_CSC_MEASUREMENT *pMeasurement)
{
    uint16_t usRevolutionDelta;
    uint16_t usTimeDelta;
    uint64_t udCadenceRpm;

    pState->bCadenceValid = false;
    if (pState->bHasCrankSample)
    {
        usRevolutionDelta = (uint16_t)(pMeasurement->usCumulativeCrankRevolutions -
                                      pState->usPreviousCrankRevolutions);
        usTimeDelta = (uint16_t)(pMeasurement->usLastCrankEventTime -
                                pState->usPreviousCrankEventTime);
        if ((0U < usRevolutionDelta) && (0U < usTimeDelta))
        {
            udCadenceRpm = (uint64_t)usRevolutionDelta * 60ULL *
                           BIKE_CSC_EVENT_TIME_HZ;
            udCadenceRpm /= (uint64_t)usTimeDelta;
            if (BIKE_CSC_MAX_CADENCE_RPM >= udCadenceRpm)
            {
                pState->usCadenceRpm = (uint16_t)udCadenceRpm;
                pState->bCadenceValid = true;
            }
        }
    }

    pState->usPreviousCrankRevolutions =
        pMeasurement->usCumulativeCrankRevolutions;
    pState->usPreviousCrankEventTime = pMeasurement->usLastCrankEventTime;
    pState->bHasCrankSample = true;

    return;
}

/* BIKE_CSC_Update: 消费一帧 CSC 累计值并处理 16/32 位自然回绕。
 * 参数：
 *   - pState: CSC 状态
 *   - pMeasurement: 当前测量
 *   - usWheelCircumferenceMm: 轮周，单位毫米
 * 返回值：无
 */
void BIKE_CSC_Update(BIKE_CSC_STATE *pState,
                     const BIKE_CSC_MEASUREMENT *pMeasurement,
                     uint16_t usWheelCircumferenceMm)
{
    if ((NULL == pState) || (NULL == pMeasurement) ||
        (0U == usWheelCircumferenceMm))
    {
        return;
    }

    if (0U != (pMeasurement->ucFlags & BIKE_CSC_WHEEL_DATA_PRESENT))
    {
        BikeCsc_UpdateWheel(pState, pMeasurement, usWheelCircumferenceMm);
    }
    else
    {
        pState->bWheelSpeedValid = false;
        pState->bHasWheelSample = false;
    }
    if (0U != (pMeasurement->ucFlags & BIKE_CSC_CRANK_DATA_PRESENT))
    {
        BikeCsc_UpdateCrank(pState, pMeasurement);
    }
    else
    {
        pState->bCadenceValid = false;
        pState->bHasCrankSample = false;
    }

    return;
}
