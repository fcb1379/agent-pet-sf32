#ifndef BIKE_SPEED_SOURCE_H
#define BIKE_SPEED_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BIKE_SPEED_SOURCE: 骑行统计当前采用的速度来源。 */
typedef enum _BIKE_SPEED_SOURCE
{
    BIKE_SPEED_SOURCE_NONE = 0,
    BIKE_SPEED_SOURCE_GNSS,
    BIKE_SPEED_SOURCE_CSC
} BIKE_SPEED_SOURCE;

/* BIKE_SPEED_INPUT: GNSS 和 CSC 候选速度。
 * 成员说明：
 *   - bGnssValid: GNSS 定位和速度是否在有效期内
 *   - bCscValid: CSC 轮速是否在有效期内
 *   - ulGnssSpeedCmPerSec: GNSS 地速，单位 cm/s
 *   - usCscSpeedCentiKph: CSC 轮速，单位 0.01 km/h
 */
typedef struct _BIKE_SPEED_INPUT
{
    bool bGnssValid;
    bool bCscValid;
    uint32_t ulGnssSpeedCmPerSec;
    uint16_t usCscSpeedCentiKph;
} BIKE_SPEED_INPUT;

/* BIKE_SPEED_SELECTION: 仲裁后的统一速度样本。
 * 成员说明：
 *   - eSource: 速度来源
 *   - bValid: 样本是否有效
 *   - usSpeedCentiKph: 速度，单位 0.01 km/h
 */
typedef struct _BIKE_SPEED_SELECTION
{
    BIKE_SPEED_SOURCE eSource;
    bool bValid;
    uint16_t usSpeedCentiKph;
} BIKE_SPEED_SELECTION;

void BIKE_SPEED_Select(const BIKE_SPEED_INPUT *pInput,
                       BIKE_SPEED_SELECTION *pSelection);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_SPEED_SOURCE_H */
