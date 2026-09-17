#ifndef BIKE_BLE_MEASUREMENT_H
#define BIKE_BLE_MEASUREMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "bike_csc.h"

#ifdef __cplusplus
extern "C" {
#endif

bool BIKE_BLE_MEAS_ParseHeartRate(const uint8_t *pData, uint16_t usLength,
                                  uint16_t *pHeartRateBpm);
bool BIKE_BLE_MEAS_ParseCsc(const uint8_t *pData, uint16_t usLength,
                           BIKE_CSC_MEASUREMENT *pMeasurement);
bool BIKE_BLE_MEAS_ParseBatteryLevel(const uint8_t *pData,
                                     uint16_t usLength,
                                     uint8_t *pBatteryPercent);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_BLE_MEASUREMENT_H */
