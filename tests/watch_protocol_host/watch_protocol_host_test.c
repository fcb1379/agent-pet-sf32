#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "badge_transfer.h"
#include "watch_alarm_service.h"
#include "watch_protocol.h"
#include "watch_settings.h"

static badge_transfer_snapshot_t g_badge;
static watch_settings_snapshot_t g_settings;
static watch_alarm_snapshot_t g_alarm;
static rt_err_t g_date_result;
static rt_err_t g_time_result;
static rt_err_t g_settings_result;
static int g_settings_calls;
static int g_clear_calls;
static int g_cancel_calls;

rt_err_t watch_alarm_set(uint8_t enabled, uint8_t hour, uint8_t minute)
{
    g_alarm.alarm_enabled = enabled;
    g_alarm.alarm_hour = hour;
    g_alarm.alarm_minute = minute;
    return RT_EOK;
}

rt_err_t watch_alarm_dismiss(void)
{
    g_alarm.alarm_ringing = 0;
    return RT_EOK;
}

rt_err_t watch_alarm_get_snapshot(watch_alarm_snapshot_t *snapshot)
{
    *snapshot = g_alarm;
    return RT_EOK;
}

rt_err_t set_date(rt_uint32_t year, rt_uint32_t month, rt_uint32_t day)
{
    assert(year == 2023 && month == 11 && day == 15);
    return g_date_result;
}

rt_err_t set_time(rt_uint32_t hour, rt_uint32_t minute, rt_uint32_t second)
{
    assert(hour == 6 && minute == 13 && second == 20);
    return g_time_result;
}

void badge_transfer_get_snapshot(badge_transfer_snapshot_t *snapshot)
{
    *snapshot = g_badge;
}

int badge_transfer_clear(void)
{
    g_clear_calls++;
    return RT_EOK;
}

int badge_transfer_cancel(void)
{
    g_cancel_calls++;
    return RT_EOK;
}

rt_err_t watch_settings_get_snapshot(watch_settings_snapshot_t *snapshot)
{
    *snapshot = g_settings;
    return RT_EOK;
}

rt_err_t watch_settings_set_time_sync(int16_t timezone_offset_minutes, uint32_t utc_epoch)
{
    g_settings_calls++;
    assert(timezone_offset_minutes == 480);
    assert(utc_epoch == 1700000000U);
    g_settings.timezone_offset_minutes = timezone_offset_minutes;
    g_settings.last_time_sync_epoch = utc_epoch;
    return g_settings_result;
}

static void expect_response(const char *request, const char *expected)
{
    char response[64] = {0};

    assert(watch_protocol_handle_request(request, response, sizeof(response)) == 1);
    assert(strcmp(response, expected) == 0);
    assert(strlen(response) < sizeof(response));
}

int main(void)
{
    char response[64] = {0};

    assert(watch_protocol_handle_request("badge", response, sizeof(response)) == 0);
    expect_response("HWS1|1|HELLO", "HWS1|1|OK|model=HS52;cap=TIME,BADGE,STATE,ALARM,TIME_REQ");
    expect_response("HWS1|2|TIME|1700000000,480", "HWS1|2|OK|time=20231115T061320;tz=480;p=1");
    assert(g_settings_calls == 1);

    g_settings_result = -RT_ERROR;
    expect_response("HWS1|3|TIME|1700000000,480", "HWS1|3|OK|time=20231115T061320;tz=480;p=0");
    assert(g_settings_calls == 2);

    g_date_result = -RT_ERROR;
    expect_response("HWS1|4|TIME|1700000000,480", "HWS1|4|ERR|4");
    assert(g_settings_calls == 2);
    g_date_result = RT_EOK;
    g_settings_result = RT_EOK;

    expect_response("HWS1|5|TIME|1,480", "HWS1|5|ERR|3");
    expect_response("HWS1|6|MEDIA", "HWS1|6|ERR|2");
    expect_response("HWS1|7|BADGE|UNKNOWN", "HWS1|7|ERR|3");

    g_badge.image_available = 1;
    g_badge.state = BADGE_TRANSFER_RECEIVING;
    g_badge.received = 2097152;
    g_badge.total = 2097152;
    g_badge.last_error = -32768;
    expect_response("HWS1|8|BADGE", "HWS1|8|OK|i=1;s=1;r=2097152;t=2097152;e=-32768");
    expect_response("HWS1|9|BADGE|CLEAR", "HWS1|9|OK|action=CLEAR");
    expect_response("HWS1|10|BADGE|CANCEL", "HWS1|10|OK|action=CANCEL");
    assert(g_clear_calls == 1 && g_cancel_calls == 1);

    expect_response("HWS1|11|BADGE|STATUS|extra", "HWS1|11|ERR|1");
    expect_response("HWS1|12|ALARM|ON,7,30", "HWS1|12|OK|enabled=1;time=0730;ring=0");
    expect_response("HWS1|13|ALARM|OFF", "HWS1|13|OK|enabled=0;time=0730;ring=0");
    g_alarm.alarm_ringing = 1;
    expect_response("HWS1|14|ALARM|DISMISS", "HWS1|14|OK|enabled=0;time=0730;ring=0");
    expect_response("HWS1|999999|HELLO", "HWS1|0|ERR|1");
    puts("watch protocol host tests passed");
    return 0;
}
