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
} watch_settings_snapshot_t;

rt_err_t watch_settings_get_snapshot(watch_settings_snapshot_t *snapshot);
uint8_t watch_settings_get_local_volume(void);
uint8_t watch_settings_get_bt_volume(void);
watch_audio_route_t watch_settings_get_route(void);
rt_err_t watch_settings_set_local_volume(uint8_t volume);
rt_err_t watch_settings_set_bt_volume(uint8_t volume);
rt_err_t watch_settings_set_route(watch_audio_route_t route);
rt_err_t watch_settings_apply_audio(void);

#endif /* WATCH_SETTINGS_H */
