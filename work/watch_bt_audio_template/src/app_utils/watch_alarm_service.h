#ifndef WATCH_ALARM_SERVICE_H
#define WATCH_ALARM_SERVICE_H

#include <stdint.h>
#include <rtdef.h>

typedef struct
{
    uint8_t alarm_enabled;
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    uint8_t alarm_ringing;
    uint8_t timer_ringing;
    uint8_t timer_running;
    uint32_t timer_remaining_seconds;
} watch_alarm_snapshot_t;

rt_err_t watch_alarm_service_init(void);
rt_err_t watch_alarm_set(uint8_t enabled, uint8_t hour, uint8_t minute);
rt_err_t watch_alarm_dismiss(void);
rt_err_t watch_alarm_get_snapshot(watch_alarm_snapshot_t *snapshot);
rt_err_t watch_timer_start(uint32_t seconds);
rt_err_t watch_timer_pause(void);
rt_err_t watch_timer_reset(void);

#endif /* WATCH_ALARM_SERVICE_H */
