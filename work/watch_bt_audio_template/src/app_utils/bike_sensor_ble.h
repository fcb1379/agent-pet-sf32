#ifndef BIKE_SENSOR_BLE_H
#define BIKE_SENSOR_BLE_H

#include <stdbool.h>
#include <stdint.h>

#include "bike_csc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BIKE_SENSOR_BLE_SNAPSHOT: BLE 骑行传感器的一致性快照。
 * 成员说明：
 *   - bHeartRateValid: 心率在超时范围内有效
 *   - bWheelSpeedValid: CSC 轮速在超时范围内有效
 *   - bCadenceValid: CSC 踏频在超时范围内有效
 *   - usHeartRateBpm: 心率，单位 bpm
 *   - usWheelSpeedCentiKph: CSC 轮速，单位 0.01 km/h
 *   - usCadenceRpm: 踏频，单位 rpm
 *   - ulHeartRateUpdateMs/ulCscUpdateMs: 最近通知的单调时间戳
 *   - ulDroppedEventCount: 互斥锁忙或事件长度错误时丢弃的事件数
 */
typedef struct _BIKE_SENSOR_BLE_SNAPSHOT
{
    bool bHeartRateValid;
    bool bWheelSpeedValid;
    bool bCadenceValid;
    uint16_t usHeartRateBpm;
    uint16_t usWheelSpeedCentiKph;
    uint16_t usCadenceRpm;
    uint32_t ulHeartRateUpdateMs;
    uint32_t ulCscUpdateMs;
    uint32_t ulDroppedEventCount;
} BIKE_SENSOR_BLE_SNAPSHOT;

bool BIKE_SENSOR_BLE_Init(void);
bool BIKE_SENSOR_BLE_GetSnapshot(BIKE_SENSOR_BLE_SNAPSHOT *pSnapshot);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_SENSOR_BLE_H */
