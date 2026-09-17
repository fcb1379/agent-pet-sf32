#ifndef BIKE_CSC_H
#define BIKE_CSC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_CSC_WHEEL_DATA_PRESENT (0x01U)
#define BIKE_CSC_CRANK_DATA_PRESENT (0x02U)

/* BIKE_CSC_MEASUREMENT: 一帧标准 BLE CSC Measurement 数据。 */
typedef struct _BIKE_CSC_MEASUREMENT
{
    uint8_t ucFlags;
    uint16_t usCumulativeCrankRevolutions;
    uint16_t usLastCrankEventTime;
    uint16_t usLastWheelEventTime;
    uint32_t ulCumulativeWheelRevolutions;
} BIKE_CSC_MEASUREMENT;

/* BIKE_CSC_STATE: CSC 回绕计算状态和最近结果。
 * 成员说明：
 *   - bHasWheelSample/bHasCrankSample: 是否已有前一有效样本
 *   - bWheelSpeedValid/bCadenceValid: 当前派生值是否有效
 *   - usWheelSpeedCentiKph: 轮速，单位 0.01 km/h
 *   - usCadenceRpm: 踏频，单位 rpm
 *   - usPreviousCrankRevolutions/usPreviousCrankEventTime: 前一曲柄样本
 *   - usPreviousWheelEventTime/ulPreviousWheelRevolutions: 前一车轮样本
 */
typedef struct _BIKE_CSC_STATE
{
    bool bHasWheelSample;
    bool bHasCrankSample;
    bool bWheelSpeedValid;
    bool bCadenceValid;
    uint16_t usWheelSpeedCentiKph;
    uint16_t usCadenceRpm;
    uint16_t usPreviousCrankRevolutions;
    uint16_t usPreviousCrankEventTime;
    uint16_t usPreviousWheelEventTime;
    uint32_t ulPreviousWheelRevolutions;
} BIKE_CSC_STATE;

void BIKE_CSC_Init(BIKE_CSC_STATE *pState);
void BIKE_CSC_Update(BIKE_CSC_STATE *pState,
                     const BIKE_CSC_MEASUREMENT *pMeasurement,
                     uint16_t usWheelCircumferenceMm);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_CSC_H */
