#ifndef RECORDER_SERVICE_H
#define RECORDER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#define RECORDER_PATH_LENGTH        (160U)
#define RECORDER_FILE_NAME_LENGTH   (96U)
#define RECORDER_FILE_MAX           (32U)
#define RECORDER_DIRECTORY_PATH     "/sdcard/recordings"
#define RECORDER_OPUS_SAMPLE_RATE_HZ       (16000U)
#define RECORDER_OPUS_CHANNEL_COUNT        (1U)
#define RECORDER_OPUS_BITRATE_BPS          (24000U)
#define RECORDER_OPUS_FRAME_SAMPLES        (320U)
#define RECORDER_OPUS_PRE_SKIP             (312U)
#define RECORDER_OPUS_MAX_PACKET_BYTES     (1275U)

typedef enum _RECORDER_FORMAT
{
    RECORDER_FORMAT_MP3 = 0,
    RECORDER_FORMAT_AAC,
    RECORDER_FORMAT_OPUS,
    RECORDER_FORMAT_COUNT,
} RECORDER_FORMAT;

typedef enum _RECORDER_RECORD_STATE
{
    RECORDER_RECORD_STATE_IDLE = 0,
    RECORDER_RECORD_STATE_STARTING,
    RECORDER_RECORD_STATE_RECORDING,
    RECORDER_RECORD_STATE_PAUSED,
    RECORDER_RECORD_STATE_STOPPING,
    RECORDER_RECORD_STATE_STOPPED,
    RECORDER_RECORD_STATE_ERROR,
} RECORDER_RECORD_STATE;

typedef enum _RECORDER_PLAYBACK_STATE
{
    RECORDER_PLAYBACK_STATE_IDLE = 0,
    RECORDER_PLAYBACK_STATE_STARTING,
    RECORDER_PLAYBACK_STATE_PLAYING,
    RECORDER_PLAYBACK_STATE_PAUSED,
    RECORDER_PLAYBACK_STATE_ENDED,
    RECORDER_PLAYBACK_STATE_ERROR,
} RECORDER_PLAYBACK_STATE;

typedef struct _RECORDER_FILE_INFO
{
    char aName[RECORDER_FILE_NAME_LENGTH];
    char aPath[RECORDER_PATH_LENGTH];
    RECORDER_FORMAT eFormat;
    uint32_t ulSizeBytes;
} RECORDER_FILE_INFO;

typedef struct _RECORDER_SNAPSHOT
{
    RECORDER_RECORD_STATE eRecordState;
    RECORDER_PLAYBACK_STATE ePlaybackState;
    RECORDER_FORMAT eRecordFormat;
    char aRecordPath[RECORDER_PATH_LENGTH];
    char aPlaybackPath[RECORDER_PATH_LENGTH];
    uint32_t ulRecordSeconds;
    uint32_t ulPlaybackSeconds;
    uint32_t ulPlaybackDurationSeconds;
    uint32_t ulFileSizeBytes;
    uint32_t ulDroppedPcmBytes;
    uint32_t ulUploadDroppedPackets;
    int32_t lLastError;
} RECORDER_SNAPSHOT;

typedef enum _RECORDER_OPUS_UPLOAD_EVENT
{
    RECORDER_OPUS_UPLOAD_STARTED = 0,
    RECORDER_OPUS_UPLOAD_PACKET,
    RECORDER_OPUS_UPLOAD_STOPPED,
    RECORDER_OPUS_UPLOAD_ERROR,
} RECORDER_OPUS_UPLOAD_EVENT;

typedef int (*RECORDER_OPUS_UPLOAD_CALLBACK)(RECORDER_OPUS_UPLOAD_EVENT eEvent,
                                              const uint8_t *pPacket,
                                              uint16_t usPacketLength,
                                              uint32_t ulSequence,
                                              uint32_t ulTimestampMs,
                                              void *pContext);

rt_err_t RECORDER_StartOpusStream(void);
rt_err_t RECORDER_StopOpusStream(void);
rt_err_t RECORDER_Start(RECORDER_FORMAT eFormat);
rt_err_t RECORDER_Pause(void);
rt_err_t RECORDER_Resume(void);
rt_err_t RECORDER_Stop(void);
rt_err_t RECORDER_GetSnapshot(RECORDER_SNAPSHOT *pSnapshot);
rt_err_t RECORDER_RefreshFiles(void);
uint16_t RECORDER_GetFileCount(void);
rt_err_t RECORDER_GetFile(uint16_t usIndex, RECORDER_FILE_INFO *pFileInfo);

rt_err_t RECORDER_Play(const char *pPath);
rt_err_t RECORDER_PlaybackPause(void);
rt_err_t RECORDER_PlaybackResume(void);
rt_err_t RECORDER_PlaybackSeekRelative(int32_t lDeltaSeconds);
rt_err_t RECORDER_PlaybackRestart(void);
rt_err_t RECORDER_PlaybackStop(void);

rt_err_t RECORDER_RegisterOpusUploader(RECORDER_OPUS_UPLOAD_CALLBACK pCallback,
                                       void *pContext);

#endif /* RECORDER_SERVICE_H */
