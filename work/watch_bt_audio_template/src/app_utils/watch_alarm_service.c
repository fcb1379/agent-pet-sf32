#include <rtthread.h>
#include <drivers/alarm.h>
#include <time.h>

#include "ble_watch_link.h"
#include "local_music_player.h"
#include "watch_alarm_service.h"
#include "watch_settings.h"

#define LOG_TAG "watch.alarm"
#include "log.h"

#define WATCH_ALARM_EVENT_DAILY (1U << 0)
#define WATCH_ALARM_EVENT_TIMER (1U << 1)
#define WATCH_ALARM_EVENT_MASK (WATCH_ALARM_EVENT_DAILY | WATCH_ALARM_EVENT_TIMER)
#define WATCH_ALARM_WEEK_DAYS (7U)
#define WATCH_ALARM_REPEAT_ALL (0x7FU)

typedef struct
{
    uint8_t initialized;
    uint8_t alarm_ringing;
    uint8_t timer_ringing;
    uint8_t timer_running;
    uint8_t alarm_snoozed;
    uint32_t alarm_snooze_deadline;
    uint32_t timer_deadline;
    uint32_t timer_remaining_seconds;
    rt_alarm_t aDailyAlarm[WATCH_ALARM_WEEK_DAYS];
    rt_timer_t timer_tick;
    rt_event_t event;
    rt_thread_t thread;
    struct rt_mutex lock;
} watch_alarm_env_t;

static watch_alarm_env_t g_watch_alarm;

static void watch_alarm_notify(const char *text)
{
    ble_link_notify_event(text);
    if (local_music_play_file(NULL, 3) != RT_EOK)
    {
        LOG_W("audio alert unavailable");
    }
}

static void watch_alarm_daily_callback(rt_alarm_t alarm, time_t timestamp)
{
    (void)alarm;
    (void)timestamp;
    rt_event_send(g_watch_alarm.event, WATCH_ALARM_EVENT_DAILY);
}

static void watch_alarm_timer_callback(void *parameter)
{
    (void)parameter;
    rt_event_send(g_watch_alarm.event, WATCH_ALARM_EVENT_TIMER);
}

static void watch_alarm_rearm_locked(const watch_settings_snapshot_t *settings)
{
    struct rt_alarm_setup setup;
    time_t now;
    time_t target;
    uint8_t ucDay;

    for (ucDay = 0U; ucDay < WATCH_ALARM_WEEK_DAYS; ucDay++)
    {
        if (NULL != g_watch_alarm.aDailyAlarm[ucDay])
        {
            rt_alarm_stop(g_watch_alarm.aDailyAlarm[ucDay]);
            rt_alarm_delete(g_watch_alarm.aDailyAlarm[ucDay]);
            g_watch_alarm.aDailyAlarm[ucDay] = NULL;
        }
    }

    g_watch_alarm.alarm_snoozed = 0U;
    g_watch_alarm.alarm_snooze_deadline = 0U;
    if ((!settings->alarm_present) || (!settings->alarm_enabled))
    {
        return;
    }

    rt_memset(&setup, 0, sizeof(setup));
    now = time(NULL);
    gmtime_r(&now, &setup.wktime);
    setup.wktime.tm_hour = settings->alarm_hour;
    setup.wktime.tm_min = settings->alarm_minute;
    setup.wktime.tm_sec = 0;

    if (0U == settings->alarm_repeat_mask)
    {
        target = mktime(&setup.wktime);
        if (target <= now)
        {
            target += 24 * 60 * 60;
            gmtime_r(&target, &setup.wktime);
        }
        setup.flag = RT_ALARM_ONESHOT;
        g_watch_alarm.aDailyAlarm[0] = rt_alarm_create(
            watch_alarm_daily_callback, &setup);
        if ((NULL == g_watch_alarm.aDailyAlarm[0]) ||
            (RT_EOK != rt_alarm_start(g_watch_alarm.aDailyAlarm[0])))
        {
            LOG_E("failed to arm one-shot %02d:%02d", settings->alarm_hour,
                  settings->alarm_minute);
        }
        return;
    }

    setup.flag = RT_ALARM_WEEKLY;
    for (ucDay = 0U; ucDay < WATCH_ALARM_WEEK_DAYS; ucDay++)
    {
        if (0U == (settings->alarm_repeat_mask & (1U << ucDay)))
        {
            continue;
        }
        setup.wktime.tm_wday = ucDay;
        g_watch_alarm.aDailyAlarm[ucDay] = rt_alarm_create(
            watch_alarm_daily_callback, &setup);
        if ((NULL == g_watch_alarm.aDailyAlarm[ucDay]) ||
            (RT_EOK != rt_alarm_start(g_watch_alarm.aDailyAlarm[ucDay])))
        {
            LOG_E("failed to arm weekday=%u %02d:%02d", ucDay,
                  settings->alarm_hour, settings->alarm_minute);
        }
    }
}

static void watch_alarm_thread_entry(void *parameter)
{
    rt_uint32_t events;

    (void)parameter;
    while (1)
    {
        if (rt_event_recv(g_watch_alarm.event, WATCH_ALARM_EVENT_MASK,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &events) != RT_EOK)
        {
            continue;
        }

        if (events & WATCH_ALARM_EVENT_DAILY)
        {
            rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
            g_watch_alarm.alarm_ringing = 1;
            g_watch_alarm.alarm_snoozed = 0U;
            rt_mutex_release(&g_watch_alarm.lock);
            watch_alarm_notify("HWS1|0|ALARM|RING");
        }

        if (events & WATCH_ALARM_EVENT_TIMER)
        {
            time_t now = time(NULL);
            uint8_t expired = 0;
            uint8_t alarm_expired = 0;

            rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
            if (g_watch_alarm.alarm_snoozed &&
                ((uint32_t)now >= g_watch_alarm.alarm_snooze_deadline))
            {
                g_watch_alarm.alarm_snoozed = 0U;
                g_watch_alarm.alarm_ringing = 1U;
                alarm_expired = 1U;
            }
            if (g_watch_alarm.timer_running && (uint32_t)now >= g_watch_alarm.timer_deadline)
            {
                g_watch_alarm.timer_running = 0;
                g_watch_alarm.timer_ringing = 1;
                g_watch_alarm.timer_remaining_seconds = 0;
                expired = 1;
            }
            else if (g_watch_alarm.timer_running)
            {
                g_watch_alarm.timer_remaining_seconds =
                    g_watch_alarm.timer_deadline - (uint32_t)now;
            }
            rt_mutex_release(&g_watch_alarm.lock);
            if (expired)
            {
                watch_alarm_notify("HWS1|0|TIMER|DONE");
            }
            if (alarm_expired)
            {
                watch_alarm_notify("HWS1|0|ALARM|SNOOZE");
            }
        }
    }
}

rt_err_t watch_alarm_service_init(void)
{
    watch_settings_snapshot_t settings;

    if (g_watch_alarm.initialized)
    {
        return RT_EOK;
    }

    rt_memset(&g_watch_alarm, 0, sizeof(g_watch_alarm));
    rt_mutex_init(&g_watch_alarm.lock, "walarm", RT_IPC_FLAG_FIFO);
    g_watch_alarm.event = rt_event_create("walarm", RT_IPC_FLAG_FIFO);
    g_watch_alarm.timer_tick = rt_timer_create("wtimer", watch_alarm_timer_callback, NULL,
                                                rt_tick_from_millisecond(1000),
                                                RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
    g_watch_alarm.thread = rt_thread_create("walarm", watch_alarm_thread_entry, NULL, 1536,
                                             RT_THREAD_PRIORITY_MIDDLE, RT_THREAD_TICK_DEFAULT);
    if (!g_watch_alarm.event || !g_watch_alarm.timer_tick || !g_watch_alarm.thread)
    {
        return -RT_ENOMEM;
    }

    g_watch_alarm.initialized = 1;
    rt_timer_start(g_watch_alarm.timer_tick);
    rt_thread_startup(g_watch_alarm.thread);
    watch_settings_get_snapshot(&settings);
    rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
    watch_alarm_rearm_locked(&settings);
    rt_mutex_release(&g_watch_alarm.lock);
    LOG_I("ready enabled=%d time=%02d:%02d", settings.alarm_enabled,
          settings.alarm_hour, settings.alarm_minute);
    return RT_EOK;
}

rt_err_t watch_alarm_set(uint8_t enabled, uint8_t hour, uint8_t minute)
{
    watch_settings_snapshot_t settings;
    rt_err_t ret;

    if (!g_watch_alarm.initialized)
    {
        ret = watch_alarm_service_init();
        if (ret != RT_EOK)
        {
            return ret;
        }
    }
    ret = watch_settings_set_alarm(enabled, hour, minute);

    if (ret != RT_EOK)
    {
        return ret;
    }
    watch_settings_get_snapshot(&settings);
    rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
    g_watch_alarm.alarm_ringing = 0;
    watch_alarm_rearm_locked(&settings);
    rt_mutex_release(&g_watch_alarm.lock);
    return RT_EOK;
}

rt_err_t watch_alarm_set_repeat(uint8_t repeat_mask)
{
    watch_settings_snapshot_t tSettings;
    rt_err_t tRet;

    tRet = watch_settings_set_alarm_repeat(repeat_mask & WATCH_ALARM_REPEAT_ALL);
    if (RT_EOK != tRet)
    {
        return tRet;
    }
    watch_settings_get_snapshot(&tSettings);
    rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
    watch_alarm_rearm_locked(&tSettings);
    rt_mutex_release(&g_watch_alarm.lock);
    return RT_EOK;
}

rt_err_t watch_alarm_set_present(uint8_t present)
{
    watch_settings_snapshot_t tSettings;
    rt_err_t tRet;

    tRet = watch_settings_set_alarm_present(present);
    if (RT_EOK != tRet)
    {
        return tRet;
    }
    watch_settings_get_snapshot(&tSettings);
    rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
    watch_alarm_rearm_locked(&tSettings);
    rt_mutex_release(&g_watch_alarm.lock);
    return RT_EOK;
}

rt_err_t watch_alarm_dismiss(void)
{
    uint8_t was_ringing;

    if (!g_watch_alarm.initialized)
    {
        return -RT_ERROR;
    }
    rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
    was_ringing = g_watch_alarm.alarm_ringing || g_watch_alarm.timer_ringing;
    g_watch_alarm.alarm_ringing = 0;
    g_watch_alarm.alarm_snoozed = 0U;
    g_watch_alarm.alarm_snooze_deadline = 0U;
    g_watch_alarm.timer_ringing = 0;
    rt_mutex_release(&g_watch_alarm.lock);
    return was_ringing ? local_music_stop() : RT_EOK;
}

rt_err_t watch_alarm_snooze(uint32_t seconds)
{
    uint8_t ucWasRinging;

    if ((!g_watch_alarm.initialized) || (0U == seconds))
    {
        return -RT_EINVAL;
    }
    rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
    ucWasRinging = g_watch_alarm.alarm_ringing;
    if (0U != ucWasRinging)
    {
        g_watch_alarm.alarm_ringing = 0U;
        g_watch_alarm.alarm_snoozed = 1U;
        g_watch_alarm.alarm_snooze_deadline = (uint32_t)time(NULL) + seconds;
    }
    rt_mutex_release(&g_watch_alarm.lock);
    if (0U == ucWasRinging)
    {
        return -RT_ERROR;
    }
    return local_music_stop();
}

rt_err_t watch_alarm_get_snapshot(watch_alarm_snapshot_t *snapshot)
{
    watch_settings_snapshot_t settings;
    time_t now = time(NULL);

    if (!snapshot)
    {
        return -RT_EINVAL;
    }
    if (!g_watch_alarm.initialized)
    {
        return -RT_ERROR;
    }
    watch_settings_get_snapshot(&settings);
    rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
    snapshot->alarm_enabled = settings.alarm_enabled;
    snapshot->alarm_hour = settings.alarm_hour;
    snapshot->alarm_minute = settings.alarm_minute;
    snapshot->alarm_repeat_mask = settings.alarm_repeat_mask;
    snapshot->alarm_present = settings.alarm_present;
    snapshot->alarm_ringing = g_watch_alarm.alarm_ringing;
    snapshot->timer_ringing = g_watch_alarm.timer_ringing;
    snapshot->timer_running = g_watch_alarm.timer_running;
    snapshot->timer_remaining_seconds = g_watch_alarm.timer_running &&
                                        (uint32_t)now < g_watch_alarm.timer_deadline ?
                                        g_watch_alarm.timer_deadline - (uint32_t)now :
                                        g_watch_alarm.timer_remaining_seconds;
    rt_mutex_release(&g_watch_alarm.lock);
    return RT_EOK;
}

rt_err_t watch_timer_start(uint32_t seconds)
{
    if (!g_watch_alarm.initialized)
    {
        return -RT_ERROR;
    }
    if (seconds == 0 || seconds > 24U * 60U * 60U)
    {
        return -RT_EINVAL;
    }
    rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
    g_watch_alarm.timer_deadline = (uint32_t)time(NULL) + seconds;
    g_watch_alarm.timer_remaining_seconds = seconds;
    g_watch_alarm.timer_running = 1;
    g_watch_alarm.timer_ringing = 0;
    rt_mutex_release(&g_watch_alarm.lock);
    return RT_EOK;
}

rt_err_t watch_timer_pause(void)
{
    time_t now = time(NULL);

    if (!g_watch_alarm.initialized)
    {
        return -RT_ERROR;
    }
    rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
    if (g_watch_alarm.timer_running)
    {
        g_watch_alarm.timer_remaining_seconds = g_watch_alarm.timer_deadline > (uint32_t)now ?
                                               g_watch_alarm.timer_deadline - (uint32_t)now : 0;
        g_watch_alarm.timer_running = 0;
    }
    rt_mutex_release(&g_watch_alarm.lock);
    return RT_EOK;
}

rt_err_t watch_timer_reset(void)
{
    if (!g_watch_alarm.initialized)
    {
        return -RT_ERROR;
    }
    rt_mutex_take(&g_watch_alarm.lock, RT_WAITING_FOREVER);
    g_watch_alarm.timer_deadline = 0;
    g_watch_alarm.timer_remaining_seconds = 0;
    g_watch_alarm.timer_running = 0;
    g_watch_alarm.timer_ringing = 0;
    rt_mutex_release(&g_watch_alarm.lock);
    return local_music_stop();
}
