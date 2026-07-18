#include <rtthread.h>
#include <drivers/rtc.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "badge_transfer.h"
#include "watch_protocol.h"
#include "watch_settings.h"

#define LOG_TAG "watch.proto"
#include "log.h"

#define WATCH_PROTOCOL_PREFIX "HWS1"
#define WATCH_PROTOCOL_MAX_REQUEST 64
#define WATCH_PROTOCOL_TIME_MIN 1577836800L
#define WATCH_PROTOCOL_TIME_MAX 2145916800L
#define WATCH_PROTOCOL_TZ_MIN (-840)
#define WATCH_PROTOCOL_TZ_MAX 840

enum watch_protocol_error
{
    WATCH_PROTOCOL_ERR_MALFORMED = 1,
    WATCH_PROTOCOL_ERR_UNSUPPORTED,
    WATCH_PROTOCOL_ERR_INVALID_VALUE,
    WATCH_PROTOCOL_ERR_RUNTIME,
};

static void watch_protocol_error(char *response, size_t response_size,
                                 const char *request_id, int error)
{
    rt_snprintf(response, response_size, "%s|%s|ERR|%d",
                WATCH_PROTOCOL_PREFIX, request_id ? request_id : "0", error);
}

static void watch_protocol_ok(char *response, size_t response_size,
                              const char *request_id, const char *payload)
{
    if (payload && payload[0])
    {
        rt_snprintf(response, response_size, "%s|%s|OK|%s",
                    WATCH_PROTOCOL_PREFIX, request_id, payload);
    }
    else
    {
        rt_snprintf(response, response_size, "%s|%s|OK",
                    WATCH_PROTOCOL_PREFIX, request_id);
    }
}

static int watch_protocol_parse_long(const char *text, long minimum, long maximum, long *value)
{
    char *end;
    long parsed;

    if (!text || !text[0] || !value)
    {
        return -RT_EINVAL;
    }

    parsed = strtol(text, &end, 10);
    if (*end != '\0' || parsed < minimum || parsed > maximum)
    {
        return -RT_EINVAL;
    }

    *value = parsed;
    return RT_EOK;
}

static int watch_protocol_set_time(const char *payload, char *result, size_t result_size)
{
    char value[40];
    char *separator;
    long utc_seconds;
    long timezone_minutes;
    time_t local_seconds;
    struct tm local_time;
    struct tm *time_ptr;
    rt_err_t date_ret;
    rt_err_t time_ret;
    rt_err_t settings_ret;

    if (!payload || rt_strlen(payload) >= sizeof(value))
    {
        return -RT_EINVAL;
    }

    rt_strncpy(value, payload, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    separator = strchr(value, ',');
    if (!separator)
    {
        return -RT_EINVAL;
    }
    *separator++ = '\0';

    if (watch_protocol_parse_long(value, WATCH_PROTOCOL_TIME_MIN, WATCH_PROTOCOL_TIME_MAX,
                                  &utc_seconds) != RT_EOK ||
            watch_protocol_parse_long(separator, WATCH_PROTOCOL_TZ_MIN, WATCH_PROTOCOL_TZ_MAX,
                                      &timezone_minutes) != RT_EOK)
    {
        return -RT_EINVAL;
    }

    local_seconds = (time_t)(utc_seconds + timezone_minutes * 60L);
    time_ptr = gmtime(&local_seconds);
    if (!time_ptr || time_ptr->tm_year + 1900 < 2020 || time_ptr->tm_year + 1900 > 2038)
    {
        return -RT_EINVAL;
    }
    local_time = *time_ptr;

    date_ret = set_date((rt_uint32_t)local_time.tm_year + 1900,
                        (rt_uint32_t)local_time.tm_mon + 1,
                        (rt_uint32_t)local_time.tm_mday);
    time_ret = set_time((rt_uint32_t)local_time.tm_hour,
                        (rt_uint32_t)local_time.tm_min,
                        (rt_uint32_t)local_time.tm_sec);
    if (date_ret != RT_EOK || time_ret != RT_EOK)
    {
        LOG_E("RTC update failed date=%d time=%d", date_ret, time_ret);
        return -RT_ERROR;
    }
    settings_ret = watch_settings_set_time_sync((int16_t)timezone_minutes,
                                                (uint32_t)utc_seconds);

    rt_snprintf(result, result_size, "time=%04d%02d%02dT%02d%02d%02d;tz=%ld;p=%d",
                local_time.tm_year + 1900,
                local_time.tm_mon + 1,
                local_time.tm_mday,
                local_time.tm_hour,
                local_time.tm_min,
                local_time.tm_sec,
                timezone_minutes,
                settings_ret == RT_EOK);
    return RT_EOK;
}

static int watch_protocol_state(char *result, size_t result_size)
{
    watch_settings_snapshot_t settings;
    badge_transfer_snapshot_t badge;
    time_t current_time;
    struct tm time_value;
    struct tm *time_ptr;

    if (watch_settings_get_snapshot(&settings) != RT_EOK)
    {
        return -RT_ERROR;
    }
    badge_transfer_get_snapshot(&badge);
    current_time = time(NULL);
    time_ptr = gmtime(&current_time);
    if (!time_ptr)
    {
        return -RT_ERROR;
    }
    time_value = *time_ptr;

    rt_snprintf(result, result_size, "time=%04d%02d%02dT%02d%02d%02d;tz=%d;img=%d",
                time_value.tm_year + 1900,
                time_value.tm_mon + 1,
                time_value.tm_mday,
                time_value.tm_hour,
                time_value.tm_min,
                time_value.tm_sec,
                settings.timezone_offset_minutes,
                badge.image_available);
    return RT_EOK;
}

static int watch_protocol_badge(const char *payload, char *result, size_t result_size)
{
    badge_transfer_snapshot_t badge;
    int ret;

    if (!payload || strcmp(payload, "STATUS") == 0)
    {
        badge_transfer_get_snapshot(&badge);
        rt_snprintf(result, result_size, "i=%d;s=%d;r=%lu;t=%lu;e=%d",
                    badge.image_available,
                    badge.state,
                    (unsigned long)badge.received,
                    (unsigned long)badge.total,
                    badge.last_error);
        return RT_EOK;
    }
    if (strcmp(payload, "CLEAR") == 0)
    {
        ret = badge_transfer_clear();
    }
    else if (strcmp(payload, "CANCEL") == 0)
    {
        ret = badge_transfer_cancel();
    }
    else
    {
        return -RT_EINVAL;
    }

    if (ret != RT_EOK)
    {
        return -RT_ERROR;
    }
    rt_snprintf(result, result_size, "action=%s", payload);
    return RT_EOK;
}

int watch_protocol_handle_request(const char *request, char *response, size_t response_size)
{
    char frame[WATCH_PROTOCOL_MAX_REQUEST];
    char *parts[4] = {0};
    char *saveptr = NULL;
    char *token;
    int count = 0;
    char result[48];
    long request_id;
    const char *error_request_id = "0";
    int ret;

    if (!request || !response || response_size == 0 || strncmp(request, WATCH_PROTOCOL_PREFIX "|", 5) != 0)
    {
        return 0;
    }

    if (rt_strlen(request) >= sizeof(frame))
    {
        watch_protocol_error(response, response_size, "0", WATCH_PROTOCOL_ERR_MALFORMED);
        return 1;
    }
    rt_strncpy(frame, request, sizeof(frame) - 1);
    frame[sizeof(frame) - 1] = '\0';

    token = strtok_r(frame, "|", &saveptr);
    while (token && count < (int)(sizeof(parts) / sizeof(parts[0])))
    {
        parts[count++] = token;
        token = strtok_r(NULL, "|", &saveptr);
    }
    if (count >= 2 && watch_protocol_parse_long(parts[1], 1, 65535, &request_id) == RT_EOK)
    {
        error_request_id = parts[1];
    }
    if (token || count < 3 || strcmp(parts[0], WATCH_PROTOCOL_PREFIX) != 0 ||
            error_request_id[0] == '0')
    {
        watch_protocol_error(response, response_size, error_request_id,
                             WATCH_PROTOCOL_ERR_MALFORMED);
        return 1;
    }

    if (strcmp(parts[2], "HELLO") == 0 && count == 3)
    {
        watch_protocol_ok(response, response_size, parts[1],
                          "model=HS52;cap=TIME,BADGE,STATE");
    }
    else if (strcmp(parts[2], "TIME") == 0 && count == 4)
    {
        ret = watch_protocol_set_time(parts[3], result, sizeof(result));
        if (ret == RT_EOK)
        {
            watch_protocol_ok(response, response_size, parts[1], result);
        }
        else
        {
            watch_protocol_error(response, response_size, parts[1],
                                 ret == -RT_EINVAL ? WATCH_PROTOCOL_ERR_INVALID_VALUE : WATCH_PROTOCOL_ERR_RUNTIME);
        }
    }
    else if (strcmp(parts[2], "STATE") == 0 && count == 3)
    {
        if (watch_protocol_state(result, sizeof(result)) == RT_EOK)
        {
            watch_protocol_ok(response, response_size, parts[1], result);
        }
        else
        {
            watch_protocol_error(response, response_size, parts[1], WATCH_PROTOCOL_ERR_RUNTIME);
        }
    }
    else if (strcmp(parts[2], "BADGE") == 0 && (count == 3 || count == 4))
    {
        ret = watch_protocol_badge(count == 4 ? parts[3] : "STATUS", result, sizeof(result));
        if (ret == RT_EOK)
        {
            watch_protocol_ok(response, response_size, parts[1], result);
        }
        else
        {
            watch_protocol_error(response, response_size, parts[1],
                                 ret == -RT_EINVAL ? WATCH_PROTOCOL_ERR_INVALID_VALUE : WATCH_PROTOCOL_ERR_RUNTIME);
        }
    }
    else if ((strcmp(parts[2], "MEDIA") == 0 || strcmp(parts[2], "NOTIFY") == 0 ||
              strcmp(parts[2], "FIND") == 0) && count == 3)
    {
        watch_protocol_error(response, response_size, parts[1], WATCH_PROTOCOL_ERR_UNSUPPORTED);
    }
    else
    {
        watch_protocol_error(response, response_size, parts[1], WATCH_PROTOCOL_ERR_MALFORMED);
    }

    return 1;
}
