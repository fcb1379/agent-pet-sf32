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
#include "audio_diagnostics.h"
#include "bt_audio_sink.h"
#include "local_music_player.h"
#include "watch_settings.h"

#define DBG_TAG "local.music"
#define DBG_LVL DBG_INFO
#include "log.h"

#define LOCAL_MUSIC_DEFAULT_PATH "/16k.wav"
#define LOCAL_MUSIC_QUEUE_DEPTH 8
#define LOCAL_MUSIC_THREAD_STACK 3072
#define LOCAL_MUSIC_EFFECT_BUFFER_SIZE (2048U)
#define LOCAL_MUSIC_EFFECT_CACHE_SIZE (8192U)
#define LOCAL_MUSIC_EFFECT_WRITE_TIMEOUT_MS (1000U)
#define LOCAL_MUSIC_EFFECT_WRITE_RETRY_MS (5U)
#define LOCAL_MUSIC_EFFECT_MIN_DRAIN_MS (50U)
#define LOCAL_MUSIC_EFFECT_MAX_DRAIN_MS (300U)
#define LOCAL_MUSIC_WAV_HEADER_SIZE (44U)

typedef enum
{
    LOCAL_MUSIC_CMD_PLAY = 0,
    LOCAL_MUSIC_CMD_PLAY_EFFECT,
    LOCAL_MUSIC_CMD_STOP,
    LOCAL_MUSIC_CMD_PAUSE,
    LOCAL_MUSIC_CMD_RESUME,
} local_music_cmd_t;

typedef struct
{
    local_music_cmd_t cmd;
    uint32_t loop;
    char path[LOCAL_MUSIC_PATH_LENGTH];
} local_music_msg_t;

static mp3ctrl_handle g_music_handle;
static rt_mq_t g_music_mq;
static rt_thread_t g_music_thread;
static LOCAL_MUSIC_STATE g_music_state = LOCAL_MUSIC_STATE_IDLE;
static char g_music_path[LOCAL_MUSIC_PATH_LENGTH] = LOCAL_MUSIC_DEFAULT_PATH;
static uint32_t g_music_last_callback;
static uint8_t g_aEffectBuffer[LOCAL_MUSIC_EFFECT_BUFFER_SIZE]; /* Fixed PCM transfer buffer, range 2048 bytes; avoids heap allocation and decoder-thread churn for short effects. */
static rt_bool_t g_bEffectPending; /* Short-effect queue state, RT_TRUE while queued or playing; coalesces rapid wooden-fish hits. */

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

static void local_music_close_current(void)
{
    if (g_music_handle)
    {
        mp3ctrl_close(g_music_handle);
        g_music_handle = NULL;
    }
}

/***************************
 * LOCALMUSIC_ReadLe16: Read an unsigned 16-bit little-endian value.
 * Parameters:
 *   - pData: Pointer to at least two bytes of input data.
 * Return value: Decoded unsigned value.
 ***************************/
static uint16_t LOCALMUSIC_ReadLe16(const uint8_t *pData)
{
    uint16_t usValue;

    RT_ASSERT(NULL != pData);
    usValue = (uint16_t)pData[0] | ((uint16_t)pData[1] << 8U);

    return usValue;
}

/***************************
 * LOCALMUSIC_ReadLe32: Read an unsigned 32-bit little-endian value.
 * Parameters:
 *   - pData: Pointer to at least four bytes of input data.
 * Return value: Decoded unsigned value.
 ***************************/
static uint32_t LOCALMUSIC_ReadLe32(const uint8_t *pData)
{
    uint32_t ulValue;

    RT_ASSERT(NULL != pData);
    ulValue = (uint32_t)pData[0] |
              ((uint32_t)pData[1] << 8U) |
              ((uint32_t)pData[2] << 16U) |
              ((uint32_t)pData[3] << 24U);

    return ulValue;
}

/***************************
 * LOCALMUSIC_PlayPcmEffect: Stream a canonical PCM WAV file to the speaker.
 * Parameters:
 *   - pPath: Absolute file-system path of the short PCM WAV effect.
 * Return value: RT_EOK on success, otherwise an RT-Thread error code.
 ***************************/
static rt_err_t LOCALMUSIC_PlayPcmEffect(const char *pPath)
{
#ifdef RT_USING_DFS
    audio_parameter_t tParameters;
    audio_client_t pClient;
    uint8_t aHeader[LOCAL_MUSIC_WAV_HEADER_SIZE];
    uint32_t ulSampleRate;
    uint32_t ulByteRate;
    uint32_t ulDataRemaining;
    uint32_t ulBytesPlayed;
    uint32_t ulDrainMs;
    uint32_t ulLastProgressMs;
    uint32_t ulOffset;
    uint16_t usChannels;
    uint16_t usBitsPerSample;
    int lFile;
    int lReadSize;
    int lWriteSize;
    rt_err_t tResult;

    if (NULL == pPath)
    {
        return -RT_EINVAL;
    }

    lFile = open(pPath, O_RDONLY | O_BINARY);
    if (0 > lFile)
    {
        LOG_E("effect file not found: %s", pPath);
        AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_EFFECT_ERROR);
        return -RT_ERROR;
    }
    AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_FILE_OPENED);

    tResult = -RT_ERROR;
    pClient = NULL;
    lReadSize = read(lFile, aHeader, sizeof(aHeader));
    if ((int)sizeof(aHeader) != lReadSize)
    {
        LOG_E("effect header read failed: %s", pPath);
        goto cleanup;
    }
    if ((0 != memcmp(&aHeader[0], "RIFF", 4U)) ||
        (0 != memcmp(&aHeader[8], "WAVE", 4U)) ||
        (0 != memcmp(&aHeader[12], "fmt ", 4U)) ||
        (1U != LOCALMUSIC_ReadLe16(&aHeader[20])) ||
        (0 != memcmp(&aHeader[36], "data", 4U)))
    {
        LOG_E("effect WAV format unsupported: %s", pPath);
        goto cleanup;
    }

    usChannels = LOCALMUSIC_ReadLe16(&aHeader[22]);
    ulSampleRate = LOCALMUSIC_ReadLe32(&aHeader[24]);
    ulByteRate = LOCALMUSIC_ReadLe32(&aHeader[28]);
    usBitsPerSample = LOCALMUSIC_ReadLe16(&aHeader[34]);
    ulDataRemaining = LOCALMUSIC_ReadLe32(&aHeader[40]);
    if (((1U != usChannels) && (2U != usChannels)) ||
        (16U != usBitsPerSample) ||
        (8000U > ulSampleRate) ||
        (48000U < ulSampleRate) ||
        (0U == ulByteRate) ||
        (0U == ulDataRemaining))
    {
        LOG_E("effect WAV parameters unsupported: rate=%lu channels=%u bits=%u",
              ulSampleRate,
              usChannels,
              usBitsPerSample);
        goto cleanup;
    }
    AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_HEADER_VALID);

    rt_memset(&tParameters, 0, sizeof(tParameters));
    tParameters.write_samplerate = ulSampleRate;
    tParameters.write_cache_size = LOCAL_MUSIC_EFFECT_CACHE_SIZE;
    tParameters.write_channnel_num = (uint8_t)usChannels;
    tParameters.write_bits_per_sample = (uint8_t)usBitsPerSample;
    (void)audio_server_set_private_volume(
        AUDIO_TYPE_LOCAL_MUSIC,
        (uint8_t)watch_settings_get_local_volume());
    AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_AUDIO_OPEN_BEGIN);
    pClient = audio_open(
        AUDIO_TYPE_LOCAL_MUSIC,
        AUDIO_TX,
        &tParameters,
        NULL,
        NULL);
    if (NULL == pClient)
    {
        LOG_E("effect audio open failed: %s", pPath);
        goto cleanup;
    }
    AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_AUDIO_OPEN_DONE);

    ulBytesPlayed = 0U;
    while (0U < ulDataRemaining)
    {
        uint32_t ulReadRequest;

        ulReadRequest = ulDataRemaining;
        if ((uint32_t)sizeof(g_aEffectBuffer) < ulReadRequest)
        {
            ulReadRequest = (uint32_t)sizeof(g_aEffectBuffer);
        }
        lReadSize = read(lFile, g_aEffectBuffer, ulReadRequest);
        if (0 >= lReadSize)
        {
            LOG_E("effect data read failed: %s", pPath);
            goto cleanup;
        }

        ulOffset = 0U;
        ulLastProgressMs = rt_tick_get_millisecond();
        while (ulOffset < (uint32_t)lReadSize)
        {
            lWriteSize = audio_write(
                pClient,
                &g_aEffectBuffer[ulOffset],
                (uint32_t)lReadSize - ulOffset);
            if (0 < lWriteSize)
            {
                ulOffset += (uint32_t)lWriteSize;
                ulLastProgressMs = rt_tick_get_millisecond();
                AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_WRITE_PROGRESS);
            }
            else if ((0 == lWriteSize) || (-1 == lWriteSize))
            {
                if (LOCAL_MUSIC_EFFECT_WRITE_TIMEOUT_MS <
                    (rt_tick_get_millisecond() - ulLastProgressMs))
                {
                    LOG_E("effect audio write timeout: %s", pPath);
                    goto cleanup;
                }
                rt_thread_mdelay(LOCAL_MUSIC_EFFECT_WRITE_RETRY_MS);
            }
            else
            {
                LOG_E("effect audio write failed: %d", lWriteSize);
                goto cleanup;
            }
        }

        ulBytesPlayed += (uint32_t)lReadSize;
        ulDataRemaining -= (uint32_t)lReadSize;
    }

    ulDrainMs = ((ulBytesPlayed * 1000U) / ulByteRate) + 20U;
    if (LOCAL_MUSIC_EFFECT_MIN_DRAIN_MS > ulDrainMs)
    {
        ulDrainMs = LOCAL_MUSIC_EFFECT_MIN_DRAIN_MS;
    }
    if (LOCAL_MUSIC_EFFECT_MAX_DRAIN_MS < ulDrainMs)
    {
        ulDrainMs = LOCAL_MUSIC_EFFECT_MAX_DRAIN_MS;
    }
    AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_DRAIN_BEGIN);
    rt_thread_mdelay(ulDrainMs);
    tResult = RT_EOK;
    LOG_I("effect played: %s bytes=%lu", pPath, ulBytesPlayed);

cleanup:
    if (NULL != pClient)
    {
        AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_AUDIO_CLOSE_BEGIN);
        (void)audio_close(pClient);
    }
    close(lFile);

    if (RT_EOK == tResult)
    {
        AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_EFFECT_DONE);
    }
    else
    {
        AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_EFFECT_ERROR);
    }

    return tResult;
#else
    (void)pPath;
    return -RT_ENOSYS;
#endif
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
            {
                rt_base_t tLevel;

                AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_EFFECT_START);
                (void)LOCALMUSIC_PlayPcmEffect(msg.path);

                tLevel = rt_hw_interrupt_disable();
                g_bEffectPending = RT_FALSE;
                rt_hw_interrupt_enable(tLevel);
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
 * LOCALMUSIC_PlayEffect: Queue a short PCM effect on the proven A2DP speaker route.
 * Parameters:
 *   - pPath: Absolute path of the WAV file in the device file system.
 * Return value: RT_EOK on success, otherwise an RT-Thread error code.
 ***************************/
int LOCALMUSIC_PlayEffect(const char *pPath)
{
    local_music_msg_t tMessage;
    rt_base_t tLevel;
    rt_err_t tResult;

    if (NULL == pPath)
    {
        return -RT_EINVAL;
    }
    if (NULL == g_music_mq)
    {
        return -RT_ERROR;
    }
    if (RT_TRUE == bt_audio_sink_is_streaming())
    {
        return -RT_EBUSY;
    }

    tLevel = rt_hw_interrupt_disable();
    if (RT_TRUE == g_bEffectPending)
    {
        rt_hw_interrupt_enable(tLevel);
        return RT_EOK;
    }
    g_bEffectPending = RT_TRUE;
    rt_hw_interrupt_enable(tLevel);
    AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_EFFECT_QUEUED);

    rt_memset(&tMessage, 0, sizeof(tMessage));
    tMessage.cmd = LOCAL_MUSIC_CMD_PLAY_EFFECT;
    rt_strncpy(tMessage.path, pPath, sizeof(tMessage.path) - 1U);
    tResult = rt_mq_send(g_music_mq, &tMessage, sizeof(tMessage));
    if (RT_EOK != tResult)
    {
        tLevel = rt_hw_interrupt_disable();
        g_bEffectPending = RT_FALSE;
        rt_hw_interrupt_enable(tLevel);
        AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_EFFECT_ERROR);
    }

    return tResult;
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
        rt_kprintf("usage: localmusic play [path] [loop] | effect [path] | stop | pause | resume | vol <0-15>\n");
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
    else if (strcmp(argv[1], "effect") == 0)
    {
        const char *pPath;

        pPath = (3 <= argc) ? argv[2] : "/940muyu3.wav";
        rt_kprintf("localmusic effect %s ret=%d\n",
                   pPath,
                   LOCALMUSIC_PlayEffect(pPath));
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
