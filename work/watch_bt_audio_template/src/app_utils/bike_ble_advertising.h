#ifndef BIKE_BLE_ADVERTISING_H
#define BIKE_BLE_ADVERTISING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_BLE_SERVICE_HEART_RATE (0x01U)
#define BIKE_BLE_SERVICE_CSC (0x02U)
#define BIKE_BLE_SERVICE_POWER (0x04U)

uint8_t BIKE_BLE_ADV_GetServiceMask(const uint8_t *pData, uint16_t usLength);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_BLE_ADVERTISING_H */
