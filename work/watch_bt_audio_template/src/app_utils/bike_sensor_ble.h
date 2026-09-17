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
 *   - bHeartRateBatteryValid/bCscBatteryValid/bPowerBatteryValid: BAS 电量有效性
 *   - bPowerValid: 功率在超时范围内有效
 *   - bPowerOn/bScanning/bConnecting: BLE 中央设备连接管理状态
 *   - bHeartRateConnected/bCscConnected/bPowerConnected: 传感器链路状态
 *   - usHeartRateBpm: 心率，单位 bpm
 *   - usWheelSpeedCentiKph: CSC 轮速，单位 0.01 km/h
 *   - usCadenceRpm: 踏频，单位 rpm
 *   - sPowerWatts: 瞬时功率，单位 W
 *   - ucHeartRateBatteryPercent/ucCscBatteryPercent/ucPowerBatteryPercent: 电量百分比
 *   - ucHeartRateConnIndex/ucCscConnIndex/ucPowerConnIndex: SDK 连接索引
 *   - cLastRssi: 最近发现目标传感器的 RSSI
 *   - ulHeartRateUpdateMs/ulCscUpdateMs/ulPowerUpdateMs: 最近通知时间戳
 *   - ulConnectionAttemptCount: 启动以来连接尝试次数
 *   - ulDroppedEventCount: 互斥锁忙或事件长度错误时丢弃的事件数
 */
typedef struct _BIKE_SENSOR_BLE_SNAPSHOT
{
    bool bHeartRateValid;
    bool bWheelSpeedValid;
    bool bCadenceValid;
    bool bHeartRateBatteryValid;
    bool bCscBatteryValid;
    bool bPowerValid;
    bool bPowerBatteryValid;
    bool bPowerOn;
    bool bScanning;
    bool bConnecting;
    bool bHeartRateConnected;
    bool bCscConnected;
    bool bPowerConnected;
    uint16_t usHeartRateBpm;
    uint16_t usWheelSpeedCentiKph;
    uint16_t usCadenceRpm;
    int16_t sPowerWatts;
    uint8_t ucHeartRateBatteryPercent;
    uint8_t ucCscBatteryPercent;
    uint8_t ucPowerBatteryPercent;
    uint8_t ucHeartRateConnIndex;
    uint8_t ucCscConnIndex;
    uint8_t ucPowerConnIndex;
    int8_t cLastRssi;
    uint32_t ulHeartRateUpdateMs;
    uint32_t ulCscUpdateMs;
    uint32_t ulPowerUpdateMs;
    uint32_t ulConnectionAttemptCount;
    uint32_t ulDroppedEventCount;
} BIKE_SENSOR_BLE_SNAPSHOT;

bool BIKE_SENSOR_BLE_Init(void);
bool BIKE_SENSOR_BLE_GetSnapshot(BIKE_SENSOR_BLE_SNAPSHOT *pSnapshot);
bool BIKE_SENSOR_BLE_RequestScan(void);
bool BIKE_SENSOR_BLE_ClearPeers(void);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_SENSOR_BLE_H */
