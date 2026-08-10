#ifndef WATCH_SETTINGS_H
#define WATCH_SETTINGS_H

#include <stdint.h>
#include <rtdef.h>

#define WATCH_SETTINGS_DEFAULT_LOCAL_VOLUME 8
#define WATCH_SETTINGS_DEFAULT_BT_VOLUME 8

typedef enum
{
    WATCH_AUDIO_ROUTE_SPEAKER_ONLY = 0,
} watch_audio_route_t;

typedef struct
{
    uint8_t local_volume;
    uint8_t bt_volume;
    watch_audio_route_t route;
    int16_t timezone_offset_minutes;
    uint32_t last_time_sync_epoch;
    uint8_t alarm_enabled;
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    uint8_t alarm_repeat_mask;
    uint8_t alarm_present;
} watch_settings_snapshot_t;

rt_err_t watch_settings_get_snapshot(watch_settings_snapshot_t *snapshot);
uint8_t watch_settings_get_local_volume(void);
uint8_t watch_settings_get_bt_volume(void);
watch_audio_route_t watch_settings_get_route(void);
rt_err_t watch_settings_set_local_volume(uint8_t volume);
rt_err_t watch_settings_set_bt_volume(uint8_t volume);
rt_err_t watch_settings_set_route(watch_audio_route_t route);
rt_err_t watch_settings_set_time_sync(int16_t timezone_offset_minutes, uint32_t utc_epoch);
rt_err_t watch_settings_set_alarm(uint8_t enabled, uint8_t hour, uint8_t minute);
rt_err_t watch_settings_set_alarm_repeat(uint8_t repeat_mask);
rt_err_t watch_settings_set_alarm_present(uint8_t present);
rt_err_t watch_settings_apply_audio(void);
rt_err_t watch_settings_reset(void);

#endif /* WATCH_SETTINGS_H */
