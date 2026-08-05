/*
 * Local music playback bridge for the Huangshan watch template.
 *
 * The file system image contains /16k.wav. This module exposes a small
 * command/API layer around the SDK mp3ctrl local-music player without playing
 * anything automatically at boot.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <stdlib.h>

#ifdef RT_USING_DFS
    #include "dfs_posix.h"
#endif

#include "audio_server.h"
#include "audio_mp3ctrl.h"
#include "bt_audio_sink.h"
#include "local_music_player.h"
#include "watch_settings.h"

#define DBG_TAG "local.music"
#define DBG_LVL DBG_INFO
#include "log.h"

#define LOCAL_MUSIC_DEFAULT_PATH "/16k.wav"
#define LOCAL_MUSIC_QUEUE_DEPTH 8
#define LOCAL_MUSIC_THREAD_STACK 3072

typedef enum
{
    LOCAL_MUSIC_CMD_PLAY = 0,
    LOCAL_MUSIC_CMD_PLAY_EFFECT,
    LOCAL_MUSIC_CMD_STOP_EFFECT,
    LOCAL_MUSIC_CMD_STOP,
    LOCAL_MUSIC_CMD_PAUSE,
    LOCAL_MUSIC_CMD_RESUME,
} local_music_cmd_t;

typedef struct
{
    local_music_cmd_t cmd;
    uint32_t loop;
    uint32_t ulGeneration;
    char path[LOCAL_MUSIC_PATH_LENGTH];
} local_music_msg_t;

static mp3ctrl_handle g_music_handle;
static mp3ctrl_handle g_pEffectHandle; /* Short-effect decoder handle; NULL when idle; used only by the audio worker; never access it from an ISR. */
static rt_mq_t g_music_mq;
static rt_thread_t g_music_thread;
static LOCAL_MUSIC_STATE g_music_state = LOCAL_MUSIC_STATE_IDLE;
static char g_music_path[LOCAL_MUSIC_PATH_LENGTH] = LOCAL_MUSIC_DEFAULT_PATH;
static uint32_t g_music_last_callback;
static uint32_t g_ulEffectNextGeneration; /* Monotonic effect request ID, range 1~UINT32_MAX; prevents stale completion callbacks from closing a newer hit. */
static uint32_t g_ulEffectActiveGeneration; /* ID of the effect handle currently open, range 0~UINT32_MAX; compared only in the audio worker. */

static const char *local_music_state_name(LOCAL_MUSIC_STATE state)
{
    switch (state)
    {
    case LOCAL_MUSIC_STATE_IDLE:
        return "idle";
    case LOCAL_MUSIC_STATE_PLAYING:
        return "playing";
    case LOCAL_MUSIC_STATE_PAUSED:
        return "paused";
    case LOCAL_MUSIC_STATE_SUSPENDED:
        return "suspended";
    case LOCAL_MUSIC_STATE_ENDED:
        return "ended";
    case LOCAL_MUSIC_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static rt_bool_t local_music_file_exists(const char *path)
{
#ifdef RT_USING_DFS
    int fd;

    fd = open(path, O_RDONLY);
    if (fd >= 0)
    {
        close(fd);
        return RT_TRUE;
    }
#endif

    return RT_FALSE;
}

static int local_music_callback(audio_server_callback_cmt_t cmd,
                                void *callback_userdata,
                                uint32_t reserved)
{
    (void)callback_userdata;
    (void)reserved;

    g_music_last_callback = (uint32_t)cmd;

    switch (cmd)
    {
    case as_callback_cmd_play_to_end:
        g_music_state = LOCAL_MUSIC_STATE_ENDED;
        if (g_music_mq)
        {
            local_music_msg_t msg;
            rt_memset(&msg, 0, sizeof(msg));
            msg.cmd = LOCAL_MUSIC_CMD_STOP;
            rt_mq_send(g_music_mq, &msg, sizeof(msg));
        }
        break;
    case as_callback_cmd_suspended:
        g_music_state = LOCAL_MUSIC_STATE_SUSPENDED;
        break;
    case as_callback_cmd_resumed:
        g_music_state = LOCAL_MUSIC_STATE_PLAYING;
        break;
    default:
        break;
    }

    LOG_I("callback cmd=%d state=%s", cmd, local_music_state_name(g_music_state));
    return 0;
}

/***************************
 * LOCALMUSIC_EffectCallback: Handle completion of a short sound effect.
 * Parameters:
 *   - eCmd: Audio server callback command.
 *   - pCallbackData: Playback generation encoded as callback data.
 *   - ulReserved: Reserved by the audio server.
 * Return value: Always returns 0.
 ***************************/
static int LOCALMUSIC_EffectCallback(audio_server_callback_cmt_t eCmd,
                                     void *pCallbackData,
                                     uint32_t ulReserved)
{
    local_music_msg_t tMessage;
    rt_err_t tResult;

    (void)ulReserved;
    if ((as_callback_cmd_play_to_end == eCmd) && (NULL != g_music_mq))
    {
        rt_memset(&tMessage, 0, sizeof(tMessage));
        tMessage.cmd = LOCAL_MUSIC_CMD_STOP_EFFECT;
        tMessage.ulGeneration = (uint32_t)(uintptr_t)pCallbackData;
        tResult = rt_mq_send(g_music_mq, &tMessage, sizeof(tMessage));
        if (RT_EOK != tResult)
        {
            LOG_W("effect completion queue failed: %d", tResult);
        }
    }

    return 0;
}

static void local_music_close_current(void)
{
    if (g_music_handle)
    {
        mp3ctrl_close(g_music_handle);
        g_music_handle = NULL;
    }
}

/***************************
 * LOCALMUSIC_CloseEffect: Close the current short sound-effect decoder.
 * Parameters: None.
 * Return value: None.
 ***************************/
static void LOCALMUSIC_CloseEffect(void)
{
    int lRetVal;

    if (NULL != g_pEffectHandle)
    {
        lRetVal = mp3ctrl_close(g_pEffectHandle);
        g_pEffectHandle = NULL;
        if (0 != lRetVal)
        {
            LOG_W("effect close failed: %d", lRetVal);
        }
    }

    return;
}

static void local_music_thread_entry(void *parameter)
{
    local_music_msg_t msg;

    (void)parameter;

    while (1)
    {
        if (rt_mq_recv(g_music_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        switch (msg.cmd)
        {
        case LOCAL_MUSIC_CMD_PLAY:
            local_music_close_current();

            if (!local_music_file_exists(msg.path))
            {
                g_music_state = LOCAL_MUSIC_STATE_ERROR;
                LOG_E("file not found: %s", msg.path);
                break;
            }

            rt_strncpy(g_music_path, msg.path, sizeof(g_music_path) - 1);
            g_music_path[sizeof(g_music_path) - 1] = '\0';

            g_music_handle = mp3ctrl_open(AUDIO_TYPE_LOCAL_MUSIC,
                                          g_music_path,
                                          local_music_callback,
                                          NULL);
            if (!g_music_handle)
            {
                g_music_state = LOCAL_MUSIC_STATE_ERROR;
                LOG_E("open failed: %s", g_music_path);
                break;
            }

            mp3ctrl_ioctl(g_music_handle, MP3CTRL_IOCTRL_LOOP_TIMES, msg.loop);
            if (mp3ctrl_play(g_music_handle) == 0)
            {
                g_music_state = LOCAL_MUSIC_STATE_PLAYING;
                LOG_I("playing %s loop=%lu", g_music_path, msg.loop);
            }
            else
            {
                g_music_state = LOCAL_MUSIC_STATE_ERROR;
                local_music_close_current();
                LOG_E("play failed: %s", g_music_path);
            }
            break;

        case LOCAL_MUSIC_CMD_PLAY_EFFECT:
            LOCALMUSIC_CloseEffect();

            if (RT_FALSE == local_music_file_exists(msg.path))
            {
                LOG_E("effect file not found: %s", msg.path);
                break;
            }

            g_ulEffectActiveGeneration = msg.ulGeneration;
            g_pEffectHandle = mp3ctrl_open(
                AUDIO_TYPE_LOCAL_RING,
                msg.path,
                LOCALMUSIC_EffectCallback,
                (void *)(uintptr_t)msg.ulGeneration);
            if (NULL == g_pEffectHandle)
            {
                LOG_E("effect open failed: %s", msg.path);
                break;
            }

            if (0 != mp3ctrl_play(g_pEffectHandle))
            {
                LOCALMUSIC_CloseEffect();
                LOG_E("effect play failed: %s", msg.path);
            }
            break;

        case LOCAL_MUSIC_CMD_STOP_EFFECT:
            if (msg.ulGeneration == g_ulEffectActiveGeneration)
            {
                LOCALMUSIC_CloseEffect();
            }
            break;

        case LOCAL_MUSIC_CMD_STOP:
            local_music_close_current();
            if (g_music_state != LOCAL_MUSIC_STATE_ENDED)
            {
                g_music_state = LOCAL_MUSIC_STATE_IDLE;
            }
            LOG_I("stopped");
            break;

        case LOCAL_MUSIC_CMD_PAUSE:
            if (g_music_handle && mp3ctrl_pause(g_music_handle) == 0)
            {
                g_music_state = LOCAL_MUSIC_STATE_PAUSED;
                LOG_I("paused");
            }
            break;

        case LOCAL_MUSIC_CMD_RESUME:
            if (g_music_handle && mp3ctrl_resume(g_music_handle) == 0)
            {
                g_music_state = LOCAL_MUSIC_STATE_PLAYING;
                LOG_I("resumed");
            }
            break;

        default:
            break;
        }
    }
}

static rt_err_t local_music_send(local_music_cmd_t cmd, const char *path, uint32_t loop)
{
    local_music_msg_t msg;

    if (!g_music_mq)
    {
        return -RT_ERROR;
    }

    rt_memset(&msg, 0, sizeof(msg));
    msg.cmd = cmd;
    msg.loop = loop;
    rt_strncpy(msg.path, path ? path : LOCAL_MUSIC_DEFAULT_PATH, sizeof(msg.path) - 1);

    return rt_mq_send(g_music_mq, &msg, sizeof(msg));
}

int local_music_get_snapshot(LOCAL_MUSIC_SNAPSHOT *pSnapshot)
{
    rt_base_t tLevel;

    if (NULL == pSnapshot)
    {
        return -RT_EINVAL;
    }

    tLevel = rt_hw_interrupt_disable();
    pSnapshot->eState = g_music_state;
    rt_strncpy(pSnapshot->aPath, g_music_path, sizeof(pSnapshot->aPath) - 1U);
    pSnapshot->ulLastCallback = g_music_last_callback;
    rt_hw_interrupt_enable(tLevel);
    pSnapshot->aPath[sizeof(pSnapshot->aPath) - 1U] = '\0';

    return RT_EOK;
}
int local_music_play_file(const char *path, uint32_t loop)
{
    if (bt_audio_sink_is_streaming())
    {
        g_music_state = LOCAL_MUSIC_STATE_ERROR;
        LOG_W("play denied while A2DP sink is streaming");
        return -RT_EBUSY;
    }

    return local_music_send(LOCAL_MUSIC_CMD_PLAY, path, loop);
}

/***************************
 * LOCALMUSIC_PlayEffect: Queue a short sound effect on the local-ring route.
 * Parameters:
 *   - pPath: Absolute path of the WAV file in the device file system.
 * Return value: RT_EOK on success, otherwise an RT-Thread error code.
 ***************************/
int LOCALMUSIC_PlayEffect(const char *pPath)
{
    local_music_msg_t tMessage;
    rt_base_t tLevel;

    if (NULL == pPath)
    {
        return -RT_EINVAL;
    }
    if (NULL == g_music_mq)
    {
        return -RT_ERROR;
    }

    rt_memset(&tMessage, 0, sizeof(tMessage));
    tMessage.cmd = LOCAL_MUSIC_CMD_PLAY_EFFECT;
    tLevel = rt_hw_interrupt_disable();
    g_ulEffectNextGeneration++;
    if (0U == g_ulEffectNextGeneration)
    {
        g_ulEffectNextGeneration = 1U;
    }
    tMessage.ulGeneration = g_ulEffectNextGeneration;
    rt_hw_interrupt_enable(tLevel);
    rt_strncpy(tMessage.path, pPath, sizeof(tMessage.path) - 1U);

    return rt_mq_send(g_music_mq, &tMessage, sizeof(tMessage));
}

int local_music_stop(void)
{
    return local_music_send(LOCAL_MUSIC_CMD_STOP, NULL, 0);
}

int local_music_pause(void)
{
    return local_music_send(LOCAL_MUSIC_CMD_PAUSE, NULL, 0);
}

int local_music_resume(void)
{
    return local_music_send(LOCAL_MUSIC_CMD_RESUME, NULL, 0);
}

static int local_music_player_init(void)
{
    if (g_music_mq)
    {
        return RT_EOK;
    }

    g_music_mq = rt_mq_create("locmusq",
                              sizeof(local_music_msg_t),
                              LOCAL_MUSIC_QUEUE_DEPTH,
                              RT_IPC_FLAG_FIFO);
    RT_ASSERT(g_music_mq);

    g_music_thread = rt_thread_create("locmusic",
                                      local_music_thread_entry,
                                      NULL,
                                      LOCAL_MUSIC_THREAD_STACK,
                                      RT_THREAD_PRIORITY_MIDDLE,
                                      RT_THREAD_TICK_DEFAULT);
    RT_ASSERT(g_music_thread);
    rt_thread_startup(g_music_thread);

    watch_settings_apply_audio();
    LOG_I("ready, default=%s", LOCAL_MUSIC_DEFAULT_PATH);
    return RT_EOK;
}
INIT_APP_EXPORT(local_music_player_init);

static void localmusic(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "status") == 0)
    {
        rt_kprintf("localmusic state=%s path=%s cb=%lu exists=%d\n",
                   local_music_state_name(g_music_state),
                   g_music_path,
                   g_music_last_callback,
                   local_music_file_exists(g_music_path));
        rt_kprintf("localmusic volume=%d/%d\n",
                   watch_settings_get_local_volume(),
                   audio_server_get_max_volume());
        rt_kprintf("usage: localmusic play [path] [loop] | stop | pause | resume | vol <0-15>\n");
        return;
    }

    if (strcmp(argv[1], "play") == 0)
    {
        const char *path = (argc >= 3) ? argv[2] : LOCAL_MUSIC_DEFAULT_PATH;
        uint32_t loop = (argc >= 4) ? (uint32_t)atoi(argv[3]) : 0;
        rt_kprintf("localmusic play %s loop=%lu ret=%d\n",
                   path,
                   loop,
                   local_music_play_file(path, loop));
    }
    else if (strcmp(argv[1], "stop") == 0)
    {
        rt_kprintf("localmusic stop ret=%d\n", local_music_stop());
    }
    else if (strcmp(argv[1], "pause") == 0)
    {
        rt_kprintf("localmusic pause ret=%d\n", local_music_pause());
    }
    else if (strcmp(argv[1], "resume") == 0)
    {
        rt_kprintf("localmusic resume ret=%d\n", local_music_resume());
    }
    else if (strcmp(argv[1], "vol") == 0 && argc >= 3)
    {
        watch_settings_set_local_volume((uint8_t)atoi(argv[2]));
        rt_kprintf("localmusic volume=%d/%d\n",
                   watch_settings_get_local_volume(),
                   audio_server_get_max_volume());
    }
    else
    {
        rt_kprintf("unknown localmusic command\n");
    }
}
MSH_CMD_EXPORT(localmusic, local music playback command);
