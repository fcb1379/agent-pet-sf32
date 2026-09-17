#ifndef BIKE_SETTINGS_H
#define BIKE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include <rtdef.h>

#include "bike_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_SETTINGS_DEFAULT_WHEEL_MM (2105U)
#define BIKE_SETTINGS_DEFAULT_RIDER_WEIGHT_KG (65U)
#define BIKE_SETTINGS_DEFAULT_AUTO_PAUSE_CENTI_KPH (300U)
#define BIKE_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT (80U)
#define BIKE_SETTINGS_DEFAULT_SCREEN_TIMEOUT_SECONDS (30U)
#define BIKE_SETTINGS_DEFAULT_MAP_USE_WGS84 (false)
#define BIKE_SETTINGS_DEFAULT_MAP_DIRECTORY "/MAP"

/* BIKE_SETTINGS_SNAPSHOT: 码表运行参数的一致性快照。
 * 成员说明：
 *   - sTimeZoneMinutes: UTC 时区偏移，范围 -720~840 分钟
 *   - usWheelCircumferenceMm: 车轮周长，范围 500~4000 毫米
 *   - ucRiderWeightKg: 卡路里估算使用的骑手体重，范围 30~250 kg
 *   - usAutoPauseCentiKph: 自动暂停阈值，范围 50~2000，单位 0.01 km/h
 *   - usScreenTimeoutSeconds: 自动熄屏时间，范围 0~3600 秒，0 表示关闭
 *   - ucBrightnessPercent: 屏幕亮度，范围 1~100%
 *   - bAutoPauseEnabled: 是否启用自动暂停
 *   - bMapUseWgs84: true 使用 WGS-84 瓦片，false 使用 GCJ-02 瓦片
 *   - aMapDirectory: 当前介质内的地图逻辑绝对路径
 *   - bStorageReady: 参数持久化存储是否可用
 */
typedef struct _BIKE_SETTINGS_SNAPSHOT
{
    int16_t sTimeZoneMinutes;
    uint16_t usWheelCircumferenceMm;
    uint16_t usAutoPauseCentiKph;
    uint16_t usScreenTimeoutSeconds;
    uint8_t ucBrightnessPercent;
    uint8_t ucRiderWeightKg;
    bool bAutoPauseEnabled;
    bool bMapUseWgs84;
    bool bStorageReady;
    char aMapDirectory[BIKE_STORAGE_MAP_DIRECTORY_MAX];
} BIKE_SETTINGS_SNAPSHOT;

rt_err_t BIKE_SETTINGS_Init(void);
rt_err_t BIKE_SETTINGS_GetSnapshot(BIKE_SETTINGS_SNAPSHOT *pSnapshot);
rt_err_t BIKE_SETTINGS_SetTimeZoneMinutes(int16_t sMinutes);
rt_err_t BIKE_SETTINGS_SetWheelCircumference(uint16_t usMillimeters);
rt_err_t BIKE_SETTINGS_SetRiderWeight(uint8_t ucWeightKg);
rt_err_t BIKE_SETTINGS_SetAutoPause(bool bEnabled, uint16_t usThresholdCentiKph);
rt_err_t BIKE_SETTINGS_SetDisplay(uint8_t ucBrightnessPercent,
                                  uint16_t usTimeoutSeconds);
rt_err_t BIKE_SETTINGS_SetMapUseWgs84(bool bUseWgs84);
rt_err_t BIKE_SETTINGS_SetMapDirectory(const char *pDirectory);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_SETTINGS_H */
