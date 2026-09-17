#ifndef BIKE_BLE_GATT_CLIENT_H
#define BIKE_BLE_GATT_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "bike_ble_advertising.h"
#include "bike_csc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*BIKE_BLE_GATT_READY_CALLBACK)(uint8_t ucConnIndex,
                                             uint8_t ucServiceMask,
                                             bool bReady);
typedef void (*BIKE_BLE_GATT_HEART_RATE_CALLBACK)(uint8_t ucConnIndex,
                                                  uint16_t usHeartRateBpm);
typedef void (*BIKE_BLE_GATT_CSC_CALLBACK)(
    uint8_t ucConnIndex, const BIKE_CSC_MEASUREMENT *pMeasurement);

bool BIKE_BLE_GATT_Init(BIKE_BLE_GATT_READY_CALLBACK pReadyCallback,
                        BIKE_BLE_GATT_HEART_RATE_CALLBACK pHeartRateCallback,
                        BIKE_BLE_GATT_CSC_CALLBACK pCscCallback);
bool BIKE_BLE_GATT_Attach(uint8_t ucConnIndex, uint8_t ucServiceMask);
void BIKE_BLE_GATT_Detach(uint8_t ucConnIndex);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_BLE_GATT_CLIENT_H */
