#include "recorder_service.h"

#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>

#include "audio_mp3ctrl.h"
#include "audio_server.h"
#include "bt_audio_sink.h"
#include "dfs_posix.h"
#include "local_music_player.h"
#include "media_dec.h"
#include "opus.h"
#include "opus_multistream.h"
#include "shine_mp3.h"
#include "tf_card_service.h"

#define RECORDER_SAMPLE_RATE_HZ             RECORDER_OPUS_SAMPLE_RATE_HZ
#define RECORDER_CHANNEL_COUNT              RECORDER_OPUS_CHANNEL_COUNT
#define RECORDER_BITS_PER_SAMPLE            (16U)
#define RECORDER_AUDIO_CACHE_BYTES          (4096U)
#define RECORDER_PCM_RING_BYTES             (32760U)
#define RECORDER_RINGBUFFER_MAX_BYTES       (32767U)
#define RECORDER_WORKER_STACK_BYTES         (220000U)
#define RECORDER_WORKER_PRIORITY            (18U)
#define RECORDER_WORKER_TICK                 (10U)
#define RECORDER_PCM_FRAMES_PER_SLICE        (2U)
#define RECORDER_AUDIO_STOP_SETTLE_MS        (20U)
#define RECORDER_PLAYBACK_STOP_TIMEOUT_MS    (3000U)
#define RECORDER_PLAYBACK_STOP_POLL_MS       (10U)
#define RECORDER_EVENT_START                 (1UL << 0)
#define RECORDER_EVENT_PCM                   (1UL << 1)
#define RECORDER_EVENT_STOP                  (1UL << 2)
#define RECORDER_EVENT_ALL                   (RECORDER_EVENT_START | \
                                              RECORDER_EVENT_PCM | \
                                              RECORDER_EVENT_STOP)
#define RECORDER_MP3_BITRATE_KBPS            (64U)
#define RECORDER_MP3_ENCODER_BYTES           (131072U)
#define RECORDER_AAC_BITRATE_BPS             (32000U)
#define RECORDER_OPUS_GRANULE_SAMPLES         \
    ((RECORDER_OPUS_FRAME_SAMPLES * 48000U) / RECORDER_SAMPLE_RATE_HZ)
#define RECORDER_OPUS_ENCODER_BYTES           (65536U)
#define RECORDER_OPUS_SCRATCH_BYTES           (120000U)
#define RECORDER_OPUS_DIAGNOSTIC_PACKET_INTERVAL (100U)
#define RECORDER_AUDIO_RING_BYTES             (32064U)
#define RECORDER_PCM_FRAME_MAX_SAMPLES        (2304U)
#define RECORDER_AAC_FRAME_SAMPLES            (1024U)
#define RECORDER_ADTS_HEADER_BYTES            (7U)
#define RECORDER_OGG_HEADER_BYTES             (27U)
#define RECORDER_OGG_MAX_SEGMENTS             (6U)
#define RECORDER_SEEK_STEP_SECONDS             (10U)

#if ((0U == RECORDER_PCM_RING_BYTES) || \
     (RECORDER_RINGBUFFER_MAX_BYTES < RECORDER_PCM_RING_BYTES))
    #error "RECORDER_PCM_RING_BYTES must fit the rt_int16_t ringbuffer size"
#endif /* RECORDER_PCM_RING_BYTES range */

typedef struct _RECORDER_CONTEXT
{
    volatile RECORDER_RECORD_STATE eRecordState;
    volatile RECORDER_PLAYBACK_STATE ePlaybackState;
    RECORDER_FORMAT eRecordFormat;
    bool bStreamOnly;
    int lFileDescriptor;
    audio_client_t pAudioClient;
    shine_t pMp3Encoder;
    AVCodecContext *pAacEncoder;
    OpusEncoder *pOpusEncoder;
    ffmpeg_handle pPlaybackHandle;
    char aRecordPath[RECORDER_PATH_LENGTH];
    char aPlaybackPath[RECORDER_PATH_LENGTH];
    uint32_t ulMp3FrameBytes;
    uint32_t ulRecordSamples;
    uint32_t ulPlaybackSeconds;
    uint32_t ulPlaybackDurationSeconds;
    uint32_t ulFileSizeBytes;
    uint32_t ulDroppedPcmBytes;
    uint32_t ulUploadDroppedPackets;
    uint32_t ulOpusPageSequence;
    uint32_t ulOpusPacketSequence;
    uint64_t udOpusGranulePosition;
    int32_t lLastError;
} RECORDER_CONTEXT;

extern AVCodec ff_aac_encoder;
extern AVCodec ff_libopus_decoder;
/* ff_ogg_demuxer: FFmpeg Ogg 输入格式实例，仅用于识别本应用生成的 Ogg Opus 录音。 */
extern AVInputFormat ff_ogg_demuxer;
extern void *ffmpeg_alloc(size_t ulBytes);
extern void ffmpeg_free(void *pMemory);
extern uint8_t __recorder_psram_bss_start__;
extern uint8_t __recorder_psram_bss_end__;

/* l_adOpusScratchMemory: libopus 非线程安全伪栈的固定 PSRAM 工作区，容量 120000 字节；
 * 录音与本地播放由录音服务状态机保证互斥，禁止在两个线程中并发调用 libopus。 */
static uint64_t l_adOpusScratchMemory[
    RECORDER_OPUS_SCRATCH_BYTES / sizeof(uint64_t)];
/* l_adAudioRingMemory: 录音输入与本地播放输出互斥复用的 Audio Manager PCM 环形缓存，
 * 容量 32064 字节并保持 8 字节对齐；生命周期覆盖 audio_open 到 audio_close。 */
static uint64_t l_adAudioRingMemory[
    RECORDER_AUDIO_RING_BYTES / sizeof(uint64_t)];

/***************************
 * opus_multistream_decode_float: 为定点裁剪的 libopus 提供 FFmpeg 链接兼容入口。
 * 功能：当前播放器固定请求 S16 输出，浮点入口不应被调用；若外部调用方误请求
 *       浮点输出，则显式返回 OPUS_UNIMPLEMENTED，避免静默产生错误音频。
 * 参数：
 *   - pDecoder: Opus 多流解码器。
 *   - pData: 输入 Opus 包。
 *   - lLength: 输入包长度。
 *   - pPcm: 浮点 PCM 输出缓冲区。
 *   - lFrameSize: 单通道最大输出采样数。
 *   - lDecodeFec: 前向纠错开关。
 * 返回值：固定返回 OPUS_UNIMPLEMENTED。
 ***************************/
int opus_multistream_decode_float(OpusMSDecoder *pDecoder,
                                  const unsigned char *pData,
                                  opus_int32 lLength,
                                  float *pPcm,
                                  int lFrameSize,
                                  int lDecodeFec)
{
    (void)pDecoder;
    (void)pData;
    (void)lLength;
    (void)pPcm;
    (void)lFrameSize;
    (void)lDecodeFec;

    return OPUS_UNIMPLEMENTED;
}

/***************************
 * opus_heap_malloc: 覆盖 libopus 默认系统堆分配入口，将动态解码器工作区放入 FFmpeg 媒体内存池。
 * 参数：
 *   - ulBytes: 申请字节数，范围 1~UINT32_MAX。
 * 返回值：成功返回媒体内存地址，失败返回 NULL。
 ***************************/
void *opus_heap_malloc(uint32_t ulBytes)
{
    if (0U == ulBytes)
    {
        return NULL;
    }

    if (RECORDER_OPUS_SCRATCH_BYTES == ulBytes)
    {
        rt_kprintf("[REC] opus scratch static bytes=%lu address=%p\n",
                   (unsigned long)ulBytes,
                   l_adOpusScratchMemory);
        return l_adOpusScratchMemory;
    }

    return ffmpeg_alloc((size_t)ulBytes);
}

/***************************
 * opus_heap_free: 释放由 opus_heap_malloc 取得的 libopus 动态解码器工作区。
 * 参数：
 *   - pMemory: 待释放地址，允许为 NULL。
 * 返回值：无。
 ***************************/
void opus_heap_free(void *pMemory)
{
    if ((NULL != pMemory) &&
            ((void *)l_adOpusScratchMemory != pMemory))
    {
        ffmpeg_free(pMemory);
    }

    return;
}

/* l_tRecorderMutex: 录音服务公共状态互斥锁，只用于线程上下文，保护路径、文件列表和播放快照。 */
static struct rt_mutex l_tRecorderMutex;
/* l_tRecorderEvent: 录音工作线程事件对象，仅传递启动、PCM 到达和停止事件，不携带音频数据。 */
static struct rt_event l_tRecorderEvent;
/* l_tRecorderThread: 录音编码及 SD 卡写入工作线程控制块，避免在 Audio Manager 回调中执行阻塞操作。 */
static struct rt_thread l_tRecorderThread;
/* l_aRecorderThreadStack: 录音编码线程静态栈，固定 220000 字节；匹配 SDK 定点 Opus 示例的 alloca 峰值，并兼顾 AAC/MP3 编码调用。 */
static uint8_t l_aRecorderThreadStack[RECORDER_WORKER_STACK_BYTES];
/* l_tPcmRingBuffer: 麦克风 PCM 静态环形缓冲区描述符，由音频回调写入、编码线程读出。 */
static struct rt_ringbuffer l_tPcmRingBuffer;
/* l_aPcmRingMemory: PCM 环形缓冲区存储空间，固定 32760 字节，约可缓存 1 秒 16 kHz 单声道音频；长度需适配 rt_int16_t 接口。 */
static uint8_t l_aPcmRingMemory[RECORDER_PCM_RING_BYTES];
/* l_aPcmFrame: 各编码器共用的 16 位 PCM 帧缓冲区，容量覆盖当前最大单帧采样数。 */
static int16_t l_aPcmFrame[RECORDER_PCM_FRAME_MAX_SAMPLES];
/* l_adMp3EncoderMemory: Shine MP3 编码器固定工作区，容量 131072 字节，位于录音 PSRAM BSS；避免录音重启时依赖碎片化系统堆。 */
static uint64_t l_adMp3EncoderMemory[
    RECORDER_MP3_ENCODER_BYTES / sizeof(uint64_t)];
/* l_afAacFrame: AAC 编码器使用的单声道平面浮点帧，范围 -1.0~1.0。 */
static float l_afAacFrame[RECORDER_AAC_FRAME_SAMPLES];
/* l_adOpusEncoderMemory: Opus 编码器静态对齐存储，容量 65536 字节，启动时仍校验实际需求，避免 SDK 结构增长越界。 */
static uint64_t l_adOpusEncoderMemory[RECORDER_OPUS_ENCODER_BYTES / sizeof(uint64_t)];
/* l_aOpusPacket: Opus 单包输出缓冲区，最大 1275 字节，供 Ogg 落盘和未来实时上传复用。 */
static uint8_t l_aOpusPacket[RECORDER_OPUS_MAX_PACKET_BYTES];
/* l_tRecorderContext: 录音与播放服务唯一运行上下文，所有状态变化均通过公开接口或工作线程完成。 */
static RECORDER_CONTEXT l_tRecorderContext;
/* l_aRecorderFiles: SD 卡录音文件的有界缓存，最多保存 32 条，按文件名从新到旧排序。 */
static RECORDER_FILE_INFO l_aRecorderFiles[RECORDER_FILE_MAX];
/* l_usRecorderFileCount: 当前文件缓存有效条目数，范围 0~RECORDER_FILE_MAX。 */
static uint16_t l_usRecorderFileCount;
/* l_pOpusUploadCallback: Opus 实时上传扩展回调；NULL 表示仅写入本地 Ogg Opus 文件。 */
static RECORDER_OPUS_UPLOAD_CALLBACK l_pOpusUploadCallback;
/* l_pOpusUploadContext: Opus 实时上传扩展回调上下文，仅由注册接口写入、编码线程读取。 */
static void *l_pOpusUploadContext;
/* l_bRecorderServiceReady: 服务初始化完成标志，false/true；未就绪时公开接口拒绝请求。 */
static bool l_bRecorderServiceReady;
/* l_bFfmpegComponentsRegistered: 应用补充的 Ogg 格式与音频编解码器注册标志，避免重复注册。 */
static bool l_bFfmpegComponentsRegistered;

/***************************
 * Recorder_NotifyOpusUploader: 将 Opus 会话事件非阻塞地发布给已注册上传器。
 * 参数：
 *   - eEvent: 会话开始、编码包、正常结束或异常结束。
 *   - pPacket: 编码包地址；非数据事件允许为 NULL。
 *   - usPacketLength: 编码包长度；非数据事件为 0。
 *   - ulSequence: 数据包序号，结束事件中表示会话总包数。
 *   - ulTimestampMs: 数据包或会话结束时间戳，单位毫秒。
 * 返回值：未注册上传器或上传器接受事件返回 0，否则返回上传器错误码。
 ***************************/
static int Recorder_NotifyOpusUploader(
    RECORDER_OPUS_UPLOAD_EVENT eEvent,
    const uint8_t *pPacket,
    uint16_t usPacketLength,
    uint32_t ulSequence,
    uint32_t ulTimestampMs)
{
    RECORDER_OPUS_UPLOAD_CALLBACK pUploadCallback;
    void *pUploadContext;
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    pUploadCallback = l_pOpusUploadCallback;
    pUploadContext = l_pOpusUploadContext;
    rt_hw_interrupt_enable(tLevel);
    if (!l_tRecorderContext.bStreamOnly || (NULL == pUploadCallback))
    {
        return 0;
    }

    return pUploadCallback(eEvent,
                           pPacket,
                           usPacketLength,
                           ulSequence,
                           ulTimestampMs,
                           pUploadContext);
}

/***************************
 * Recorder_GetErrnoResult: 将 RT-Thread DFS 的 errno 统一为服务使用的负错误码。
 * 参数：无。
 * 返回值：errno 非零时返回对应负错误码，否则返回 -RT_ERROR。
 ***************************/
static rt_err_t Recorder_GetErrnoResult(void)
{
    int lError;

    lError = rt_get_errno();
    if (0 < lError)
    {
        return (rt_err_t)(-lError);
    }
    if (0 > lError)
    {
        return (rt_err_t)lError;
    }

    return -RT_ERROR;
}

/***************************
 * Recorder_WriteAll: 将完整缓冲区可靠写入当前录音文件。
 * 参数：
 *   - pData: 输入数据指针，不得为 NULL。
 *   - ulLength: 输入字节数。
 * 返回值：成功返回 RT_EOK，写入失败返回负错误码。
 ***************************/
static rt_err_t Recorder_WriteAll(const uint8_t *pData, uint32_t ulLength)
{
    uint32_t ulOffset;

    if ((NULL == pData) || (0U == ulLength) ||
            (0 > l_tRecorderContext.lFileDescriptor))
    {
        return -RT_EINVAL;
    }

    ulOffset = 0U;
    while (ulOffset < ulLength)
    {
        int lWritten;

        lWritten = write(l_tRecorderContext.lFileDescriptor,
                         &pData[ulOffset],
                         ulLength - ulOffset);
        if (0 >= lWritten)
        {
            return Recorder_GetErrnoResult();
        }
        ulOffset += (uint32_t)lWritten;
    }
    l_tRecorderContext.ulFileSizeBytes += ulLength;

    return RT_EOK;
}

/***************************
 * Recorder_WriteLe32: 将 32 位整数按小端序写入字节数组。
 * 参数：
 *   - pOutput: 至少 4 字节的输出缓冲区。
 *   - ulValue: 待写入数值。
 * 返回值：无。
 ***************************/
static void Recorder_WriteLe32(uint8_t *pOutput, uint32_t ulValue)
{
    if (NULL != pOutput)
    {
        pOutput[0] = (uint8_t)(ulValue & 0xFFU);
        pOutput[1] = (uint8_t)((ulValue >> 8) & 0xFFU);
        pOutput[2] = (uint8_t)((ulValue >> 16) & 0xFFU);
        pOutput[3] = (uint8_t)((ulValue >> 24) & 0xFFU);
    }

    return;
}

/***************************
 * Recorder_WriteLe64: 将 64 位整数按小端序写入字节数组。
 * 参数：
 *   - pOutput: 至少 8 字节的输出缓冲区。
 *   - udValue: 待写入数值。
 * 返回值：无。
 ***************************/
static void Recorder_WriteLe64(uint8_t *pOutput, uint64_t udValue)
{
    uint8_t ucIndex;

    if (NULL == pOutput)
    {
        return;
    }
    for (ucIndex = 0U; ucIndex < 8U; ucIndex++)
    {
        pOutput[ucIndex] = (uint8_t)((udValue >> (ucIndex * 8U)) & 0xFFU);
    }

    return;
}

/***************************
 * Recorder_OggCrcUpdate: 按 Ogg 规范更新无反射 CRC-32。
 * 参数：
 *   - ulCrc: 前一段数据的 CRC。
 *   - pData: 输入数据。
 *   - ulLength: 输入字节数。
 * 返回值：更新后的 CRC。
 ***************************/
static uint32_t Recorder_OggCrcUpdate(uint32_t ulCrc,
                                      const uint8_t *pData,
                                      uint32_t ulLength)
{
    uint32_t ulIndex;

    if (NULL == pData)
    {
        return ulCrc;
    }
    for (ulIndex = 0U; ulIndex < ulLength; ulIndex++)
    {
        uint8_t ucBit;

        ulCrc ^= ((uint32_t)pData[ulIndex] << 24);
        for (ucBit = 0U; ucBit < 8U; ucBit++)
        {
            ulCrc = (0U != (ulCrc & 0x80000000U)) ?
                    ((ulCrc << 1) ^ 0x04C11DB7U) : (ulCrc << 1);
        }
    }

    return ulCrc;
}

/***************************
 * Recorder_WriteOggPage: 将一个完整 Opus 包封装为单独 Ogg 页。
 * 参数：
 *   - pPacket: 包数据；空 EOS 页时可为 NULL。
 *   - usPacketLength: 包长度，范围 0~1275。
 *   - ucHeaderType: Ogg 页头类型标志。
 *   - udGranulePosition: 48 kHz 时间基的累计 granule 位置。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_WriteOggPage(const uint8_t *pPacket,
                                      uint16_t usPacketLength,
                                      uint8_t ucHeaderType,
                                      uint64_t udGranulePosition)
{
    uint8_t aHeader[RECORDER_OGG_HEADER_BYTES];
    uint8_t aSegments[RECORDER_OGG_MAX_SEGMENTS];
    uint8_t ucSegmentCount;
    uint16_t usRemaining;
    uint32_t ulCrc;
    rt_err_t tResult;

    if ((RECORDER_OPUS_MAX_PACKET_BYTES < usPacketLength) ||
            ((0U < usPacketLength) && (NULL == pPacket)))
    {
        return -RT_EINVAL;
    }

    rt_memset(aHeader, 0, sizeof(aHeader));
    rt_memcpy(aHeader, "OggS", 4U);
    aHeader[4] = 0U;
    aHeader[5] = ucHeaderType;
    Recorder_WriteLe64(&aHeader[6], udGranulePosition);
    Recorder_WriteLe32(&aHeader[14], 0x41504554U);
    Recorder_WriteLe32(&aHeader[18], l_tRecorderContext.ulOpusPageSequence);

    ucSegmentCount = 0U;
    usRemaining = usPacketLength;
    while (255U <= usRemaining)
    {
        if (RECORDER_OGG_MAX_SEGMENTS <= ucSegmentCount)
        {
            return -RT_EFULL;
        }
        aSegments[ucSegmentCount] = 255U;
        ucSegmentCount++;
        usRemaining -= 255U;
    }
    if ((0U < usPacketLength) || (0U < usRemaining))
    {
        if (RECORDER_OGG_MAX_SEGMENTS <= ucSegmentCount)
        {
            return -RT_EFULL;
        }
        aSegments[ucSegmentCount] = (uint8_t)usRemaining;
        ucSegmentCount++;
    }
    aHeader[26] = ucSegmentCount;

    ulCrc = Recorder_OggCrcUpdate(0U, aHeader, sizeof(aHeader));
    ulCrc = Recorder_OggCrcUpdate(ulCrc, aSegments, ucSegmentCount);
    ulCrc = Recorder_OggCrcUpdate(ulCrc, pPacket, usPacketLength);
    Recorder_WriteLe32(&aHeader[22], ulCrc);

    tResult = Recorder_WriteAll(aHeader, sizeof(aHeader));
    if ((RT_EOK == tResult) && (0U < ucSegmentCount))
    {
        tResult = Recorder_WriteAll(aSegments, ucSegmentCount);
    }
    if ((RT_EOK == tResult) && (0U < usPacketLength))
    {
        tResult = Recorder_WriteAll(pPacket, usPacketLength);
    }
    if (RT_EOK == tResult)
    {
        l_tRecorderContext.ulOpusPageSequence++;
    }

    return tResult;
}

/***************************
 * Recorder_WriteOpusHeaders: 写入标准 Ogg OpusHead 与 OpusTags 两个首页。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_WriteOpusHeaders(void)
{
    static const uint8_t aVendor[] = "AgentPet";
    uint8_t aOpusHead[19];
    uint8_t aOpusTags[24];
    rt_err_t tResult;

    rt_memset(aOpusHead, 0, sizeof(aOpusHead));
    rt_memcpy(aOpusHead, "OpusHead", 8U);
    aOpusHead[8] = 1U;
    aOpusHead[9] = RECORDER_CHANNEL_COUNT;
    aOpusHead[10] = (uint8_t)(RECORDER_OPUS_PRE_SKIP & 0xFFU);
    aOpusHead[11] = (uint8_t)((RECORDER_OPUS_PRE_SKIP >> 8) & 0xFFU);
    Recorder_WriteLe32(&aOpusHead[12], RECORDER_SAMPLE_RATE_HZ);
    aOpusHead[18] = 0U;

    rt_memset(aOpusTags, 0, sizeof(aOpusTags));
    rt_memcpy(aOpusTags, "OpusTags", 8U);
    Recorder_WriteLe32(&aOpusTags[8], sizeof(aVendor) - 1U);
    rt_memcpy(&aOpusTags[12], aVendor, sizeof(aVendor) - 1U);
    Recorder_WriteLe32(&aOpusTags[20], 0U);

    l_tRecorderContext.ulOpusPageSequence = 0U;
    tResult = Recorder_WriteOggPage(aOpusHead,
                                    sizeof(aOpusHead),
                                    0x02U,
                                    0U);
    if (RT_EOK == tResult)
    {
        tResult = Recorder_WriteOggPage(aOpusTags,
                                        sizeof(aOpusTags),
                                        0U,
                                        0U);
    }

    return tResult;
}

/***************************
 * Recorder_InitMp3Encoder: 初始化 16 kHz 单声道 Shine MP3 编码器。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_InitMp3Encoder(void)
{
    shine_config_t tConfiguration;
    size_t ulRequiredBytes;
    int lSamplesPerPass;

    rt_memset(&tConfiguration, 0, sizeof(tConfiguration));
    shine_set_config_mpeg_defaults(&tConfiguration.mpeg);
    tConfiguration.wave.channels = RECORDER_CHANNEL_COUNT;
    tConfiguration.wave.samplerate = RECORDER_SAMPLE_RATE_HZ;
    tConfiguration.mpeg.mode = MONO;
    tConfiguration.mpeg.bitr = RECORDER_MP3_BITRATE_KBPS;
    if (0 > shine_check_config(tConfiguration.wave.samplerate,
                               tConfiguration.mpeg.bitr))
    {
        return -RT_EINVAL;
    }

    ulRequiredBytes = shine_get_work_buffer_size();
    if (sizeof(l_adMp3EncoderMemory) < ulRequiredBytes)
    {
        rt_kprintf("[REC] MP3 workspace too small required=%lu available=%lu\n",
                   (unsigned long)ulRequiredBytes,
                   (unsigned long)sizeof(l_adMp3EncoderMemory));
        return -RT_ENOMEM;
    }
    l_tRecorderContext.pMp3Encoder = shine_initialise_with_buffer(
                                         &tConfiguration,
                                         l_adMp3EncoderMemory,
                                         sizeof(l_adMp3EncoderMemory));
    if (NULL == l_tRecorderContext.pMp3Encoder)
    {
        return -RT_ENOMEM;
    }
    lSamplesPerPass = shine_samples_per_pass(l_tRecorderContext.pMp3Encoder);
    if ((0 >= lSamplesPerPass) ||
            (RECORDER_PCM_FRAME_MAX_SAMPLES < (uint32_t)lSamplesPerPass))
    {
        shine_close(l_tRecorderContext.pMp3Encoder);
        l_tRecorderContext.pMp3Encoder = NULL;
        return -RT_EINVAL;
    }
    l_tRecorderContext.ulMp3FrameBytes =
        (uint32_t)lSamplesPerPass * sizeof(int16_t);

    return RT_EOK;
}

/***************************
 * Recorder_InitAacEncoder: 初始化 FFmpeg AAC-LC 编码器，输出由服务封装为 ADTS。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_InitAacEncoder(void)
{
    int lResult;

    l_tRecorderContext.pAacEncoder = avcodec_alloc_context3(&ff_aac_encoder);
    if (NULL == l_tRecorderContext.pAacEncoder)
    {
        return -RT_ENOMEM;
    }

    l_tRecorderContext.pAacEncoder->bit_rate = RECORDER_AAC_BITRATE_BPS;
    l_tRecorderContext.pAacEncoder->sample_rate = RECORDER_SAMPLE_RATE_HZ;
    l_tRecorderContext.pAacEncoder->channels = RECORDER_CHANNEL_COUNT;
    l_tRecorderContext.pAacEncoder->channel_layout = AV_CH_LAYOUT_MONO;
    l_tRecorderContext.pAacEncoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
    l_tRecorderContext.pAacEncoder->profile = FF_PROFILE_AAC_LOW;
    l_tRecorderContext.pAacEncoder->time_base.num = 1;
    l_tRecorderContext.pAacEncoder->time_base.den = RECORDER_SAMPLE_RATE_HZ;
    lResult = avcodec_open2(l_tRecorderContext.pAacEncoder,
                            &ff_aac_encoder,
                            NULL);
    if (0 > lResult)
    {
        avcodec_free_context(&l_tRecorderContext.pAacEncoder);
        return -RT_ERROR;
    }

    return RT_EOK;
}

/***************************
 * Recorder_InitOpusEncoder: 在静态内存中初始化 16 kHz 单声道 Opus VOIP 编码器。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_InitOpusEncoder(void)
{
    int lEncoderBytes;
    int lResult;

    lEncoderBytes = opus_encoder_get_size(RECORDER_CHANNEL_COUNT);
    if ((0 >= lEncoderBytes) ||
            (RECORDER_OPUS_ENCODER_BYTES < (uint32_t)lEncoderBytes))
    {
        return -RT_ENOMEM;
    }

    rt_memset(l_adOpusEncoderMemory, 0, sizeof(l_adOpusEncoderMemory));
    l_tRecorderContext.pOpusEncoder =
        (OpusEncoder *)(void *)l_adOpusEncoderMemory;
    lResult = opus_encoder_init(l_tRecorderContext.pOpusEncoder,
                                RECORDER_SAMPLE_RATE_HZ,
                                RECORDER_CHANNEL_COUNT,
                                OPUS_APPLICATION_VOIP);
    if (OPUS_OK != lResult)
    {
        l_tRecorderContext.pOpusEncoder = NULL;
        return -RT_ERROR;
    }
    lResult = opus_encoder_ctl(l_tRecorderContext.pOpusEncoder,
                               OPUS_SET_BITRATE(RECORDER_OPUS_BITRATE_BPS));
    if (OPUS_OK == lResult)
    {
        lResult = opus_encoder_ctl(l_tRecorderContext.pOpusEncoder,
                                   OPUS_SET_COMPLEXITY(3));
    }
    if (OPUS_OK == lResult)
    {
        lResult = opus_encoder_ctl(l_tRecorderContext.pOpusEncoder,
                                   OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    }
    if (OPUS_OK != lResult)
    {
        l_tRecorderContext.pOpusEncoder = NULL;
        return -RT_ERROR;
    }
    l_tRecorderContext.ulOpusPacketSequence = 0U;
    l_tRecorderContext.udOpusGranulePosition = RECORDER_OPUS_PRE_SKIP;

    return l_tRecorderContext.bStreamOnly ?
        RT_EOK : Recorder_WriteOpusHeaders();
}

/***************************
 * Recorder_WriteAdtsPacket: 为一帧原始 AAC-LC 数据添加 16 kHz 单声道 ADTS 页头。
 * 参数：
 *   - pPacket: FFmpeg 输出的原始 AAC 包。
 *   - ulPacketLength: 原始 AAC 包长度。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_WriteAdtsPacket(const uint8_t *pPacket,
                                         uint32_t ulPacketLength)
{
    uint8_t aHeader[RECORDER_ADTS_HEADER_BYTES];
    uint32_t ulFrameLength;
    rt_err_t tResult;

    if ((NULL == pPacket) || ((0x1FFFU - RECORDER_ADTS_HEADER_BYTES) < ulPacketLength))
    {
        return -RT_EINVAL;
    }
    ulFrameLength = ulPacketLength + RECORDER_ADTS_HEADER_BYTES;
    aHeader[0] = 0xFFU;
    aHeader[1] = 0xF1U;
    aHeader[2] = 0x60U;
    aHeader[3] = (uint8_t)(0x40U | ((ulFrameLength >> 11) & 0x03U));
    aHeader[4] = (uint8_t)((ulFrameLength >> 3) & 0xFFU);
    aHeader[5] = (uint8_t)(((ulFrameLength & 0x07U) << 5) | 0x1FU);
    aHeader[6] = 0xFCU;

    tResult = Recorder_WriteAll(aHeader, sizeof(aHeader));
    if (RT_EOK == tResult)
    {
        tResult = Recorder_WriteAll(pPacket, ulPacketLength);
    }

    return tResult;
}

/***************************
 * Recorder_EncodeMp3Frame: 编码并写入一帧完整 MP3 PCM 数据。
 * 参数：无，PCM 输入来自 l_aPcmFrame。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_EncodeMp3Frame(void)
{
    uint8_t *pEncodedData;
    int lEncodedLength;

    pEncodedData = shine_encode_buffer_interleaved(
        l_tRecorderContext.pMp3Encoder,
        l_aPcmFrame,
        &lEncodedLength);
    if ((0 > lEncodedLength) ||
            ((0 < lEncodedLength) && (NULL == pEncodedData)))
    {
        return -RT_ERROR;
    }
    if (0 == lEncodedLength)
    {
        return RT_EOK;
    }

    return Recorder_WriteAll(pEncodedData, (uint32_t)lEncodedLength);
}

/***************************
 * Recorder_EncodeAacFrame: 将一帧 S16 PCM 转换为平面浮点并编码为 AAC ADTS。
 * 参数：无，PCM 输入来自 l_aPcmFrame。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_EncodeAacFrame(void)
{
    AVFrame tFrame;
    AVPacket tPacket;
    uint32_t ulIndex;
    int lGotPacket;
    int lResult;
    rt_err_t tResult;

    for (ulIndex = 0U; ulIndex < RECORDER_AAC_FRAME_SAMPLES; ulIndex++)
    {
        l_afAacFrame[ulIndex] = (float)l_aPcmFrame[ulIndex] / 32768.0f;
    }

    rt_memset(&tFrame, 0, sizeof(tFrame));
    tFrame.nb_samples = RECORDER_AAC_FRAME_SAMPLES;
    tFrame.format = AV_SAMPLE_FMT_FLTP;
    tFrame.channel_layout = AV_CH_LAYOUT_MONO;
    tFrame.sample_rate = RECORDER_SAMPLE_RATE_HZ;
    tFrame.data[0] = (uint8_t *)(void *)l_afAacFrame;
    tFrame.extended_data = tFrame.data;

    av_init_packet(&tPacket);
    tPacket.data = NULL;
    tPacket.size = 0;
    lGotPacket = 0;
    lResult = avcodec_encode_audio2(l_tRecorderContext.pAacEncoder,
                                    &tPacket,
                                    &tFrame,
                                    &lGotPacket);
    if (0 > lResult)
    {
        av_free_packet(&tPacket);
        return -RT_ERROR;
    }
    tResult = RT_EOK;
    if ((0 != lGotPacket) && (0 < tPacket.size))
    {
        tResult = Recorder_WriteAdtsPacket(tPacket.data,
                                           (uint32_t)tPacket.size);
    }
    av_free_packet(&tPacket);

    return tResult;
}

/***************************
 * Recorder_EncodeOpusFrame: 编码一帧 Opus、触发可选实时上传回调并写入 Ogg 页。
 * 参数：无，PCM 输入来自 l_aPcmFrame。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_EncodeOpusFrame(void)
{
    uint32_t ulTimestampMs;
    bool bFirstPacket;
    rt_err_t tWriteResult;
    int lEncodedLength;
    int lUploadResult;

    bFirstPacket = (0U == l_tRecorderContext.ulOpusPacketSequence);
    if (bFirstPacket)
    {
        rt_kprintf("[REC] opus first frame encode begin\n");
    }
    lEncodedLength = opus_encode(l_tRecorderContext.pOpusEncoder,
                                 l_aPcmFrame,
                                 RECORDER_OPUS_FRAME_SAMPLES,
                                 l_aOpusPacket,
                                 sizeof(l_aOpusPacket));
    if (0 >= lEncodedLength)
    {
        return -RT_ERROR;
    }
    if (bFirstPacket)
    {
        rt_kprintf("[REC] opus first frame encoded bytes=%d\n",
                   lEncodedLength);
    }

    ulTimestampMs = (l_tRecorderContext.ulOpusPacketSequence *
                     RECORDER_OPUS_FRAME_SAMPLES * 1000U) /
                    RECORDER_SAMPLE_RATE_HZ;
    lUploadResult = Recorder_NotifyOpusUploader(
        RECORDER_OPUS_UPLOAD_PACKET,
        l_aOpusPacket,
        (uint16_t)lEncodedLength,
        l_tRecorderContext.ulOpusPacketSequence,
        ulTimestampMs);
    if (0 != lUploadResult)
    {
        l_tRecorderContext.ulUploadDroppedPackets++;
    }
    if (bFirstPacket)
    {
        rt_kprintf("[REC] opus first frame upload result=%d\n",
                   lUploadResult);
    }
    l_tRecorderContext.ulOpusPacketSequence++;
    l_tRecorderContext.udOpusGranulePosition +=
        RECORDER_OPUS_GRANULE_SAMPLES;

    if (l_tRecorderContext.bStreamOnly)
    {
        tWriteResult = RT_EOK;
    }
    else
    {
        tWriteResult = Recorder_WriteOggPage(
            l_aOpusPacket,
            (uint16_t)lEncodedLength,
            0U,
            l_tRecorderContext.udOpusGranulePosition);
    }
    if (bFirstPacket)
    {
        rt_kprintf("[REC] opus first frame stored result=%d\n",
                   (int)tWriteResult);
    }
    else if ((RT_EOK == tWriteResult) &&
             (0U == (l_tRecorderContext.ulOpusPacketSequence %
                     RECORDER_OPUS_DIAGNOSTIC_PACKET_INTERVAL)))
    {
        rt_kprintf("[REC] opus heartbeat packets=%lu upload_drop=%lu pcm_drop=%lu\n",
                   (unsigned long)l_tRecorderContext.ulOpusPacketSequence,
                   (unsigned long)l_tRecorderContext.ulUploadDroppedPackets,
                   (unsigned long)l_tRecorderContext.ulDroppedPcmBytes);
    }

    return tWriteResult;
}

/***************************
 * Recorder_ProcessPcm: 按当前格式消耗环形缓冲区中的完整 PCM 帧。
 * 参数：
 *   - bFlushPartial: true 时用静音补齐最后一个不完整帧。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_ProcessPcm(bool bFlushPartial)
{
    uint32_t ulFrameBytes;
    uint32_t ulAvailableBytes;
    uint32_t ulReadBytes;
    uint32_t ulProcessedFrames;
    rt_err_t tResult;

    if (RECORDER_FORMAT_MP3 == l_tRecorderContext.eRecordFormat)
    {
        ulFrameBytes = l_tRecorderContext.ulMp3FrameBytes;
    }
    else if (RECORDER_FORMAT_AAC == l_tRecorderContext.eRecordFormat)
    {
        ulFrameBytes = RECORDER_AAC_FRAME_SAMPLES * sizeof(int16_t);
    }
    else
    {
        ulFrameBytes = RECORDER_OPUS_FRAME_SAMPLES * sizeof(int16_t);
    }
    if ((0U == ulFrameBytes) || (sizeof(l_aPcmFrame) < ulFrameBytes))
    {
        return -RT_EINVAL;
    }

    tResult = RT_EOK;
    ulProcessedFrames = 0U;
    ulAvailableBytes = rt_ringbuffer_data_len(&l_tPcmRingBuffer);
    while ((ulFrameBytes <= ulAvailableBytes) ||
            ((true == bFlushPartial) && (0U < ulAvailableBytes)))
    {
        rt_memset(l_aPcmFrame, 0, ulFrameBytes);
        ulReadBytes = (ulFrameBytes < ulAvailableBytes) ?
                      ulFrameBytes : ulAvailableBytes;
        ulReadBytes = rt_ringbuffer_get(&l_tPcmRingBuffer,
                                        (uint8_t *)(void *)l_aPcmFrame,
                                        ulReadBytes);
        if (0U == ulReadBytes)
        {
            break;
        }

        if (RECORDER_FORMAT_MP3 == l_tRecorderContext.eRecordFormat)
        {
            tResult = Recorder_EncodeMp3Frame();
        }
        else if (RECORDER_FORMAT_AAC == l_tRecorderContext.eRecordFormat)
        {
            tResult = Recorder_EncodeAacFrame();
        }
        else
        {
            tResult = Recorder_EncodeOpusFrame();
        }
        if (RT_EOK != tResult)
        {
            break;
        }
        ulProcessedFrames++;
        ulAvailableBytes = rt_ringbuffer_data_len(&l_tPcmRingBuffer);
        if ((false == bFlushPartial) &&
                (RECORDER_PCM_FRAMES_PER_SLICE <= ulProcessedFrames))
        {
            if (ulFrameBytes <= ulAvailableBytes)
            {
                (void)rt_event_send(&l_tRecorderEvent,
                                    RECORDER_EVENT_PCM);
            }
            break;
        }
    }

    return tResult;
}

/***************************
 * Recorder_FlushEncoder: 刷新编码器尾帧并完成容器结尾。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_FlushEncoder(void)
{
    rt_err_t tResult;

    tResult = Recorder_ProcessPcm(true);
    if (RT_EOK != tResult)
    {
        return tResult;
    }

    if (RECORDER_FORMAT_MP3 == l_tRecorderContext.eRecordFormat)
    {
        uint8_t *pEncodedData;
        int lEncodedLength;

        pEncodedData = shine_flush(l_tRecorderContext.pMp3Encoder,
                                   &lEncodedLength);
        if ((0 > lEncodedLength) ||
                ((0 < lEncodedLength) && (NULL == pEncodedData)))
        {
            return -RT_ERROR;
        }
        if (0 < lEncodedLength)
        {
            tResult = Recorder_WriteAll(pEncodedData,
                                        (uint32_t)lEncodedLength);
        }
    }
    else if (RECORDER_FORMAT_AAC == l_tRecorderContext.eRecordFormat)
    {
        bool bContinue;

        bContinue = true;
        while (true == bContinue)
        {
            AVPacket tPacket;
            int lGotPacket;
            int lResult;

            av_init_packet(&tPacket);
            tPacket.data = NULL;
            tPacket.size = 0;
            lGotPacket = 0;
            lResult = avcodec_encode_audio2(l_tRecorderContext.pAacEncoder,
                                            &tPacket,
                                            NULL,
                                            &lGotPacket);
            if (0 > lResult)
            {
                av_free_packet(&tPacket);
                return -RT_ERROR;
            }
            if ((0 != lGotPacket) && (0 < tPacket.size))
            {
                tResult = Recorder_WriteAdtsPacket(tPacket.data,
                                                   (uint32_t)tPacket.size);
            }
            else
            {
                bContinue = false;
            }
            av_free_packet(&tPacket);
            if (RT_EOK != tResult)
            {
                break;
            }
        }
    }
    else
    {
        if (l_tRecorderContext.bStreamOnly)
        {
            return RT_EOK;
        }
        tResult = Recorder_WriteOggPage(NULL,
                                        0U,
                                        0x04U,
                                        l_tRecorderContext.udOpusGranulePosition);
    }

    return tResult;
}

/***************************
 * Recorder_CloseEncoders: 释放编码器内部资源并清空句柄。
 * 参数：无。
 * 返回值：无。
 ***************************/
static void Recorder_CloseEncoders(void)
{
    if (NULL != l_tRecorderContext.pMp3Encoder)
    {
        shine_close(l_tRecorderContext.pMp3Encoder);
        l_tRecorderContext.pMp3Encoder = NULL;
    }
    if (NULL != l_tRecorderContext.pAacEncoder)
    {
        avcodec_free_context(&l_tRecorderContext.pAacEncoder);
    }
    l_tRecorderContext.pOpusEncoder = NULL;
    l_tRecorderContext.ulMp3FrameBytes = 0U;

    return;
}

/***************************
 * Recorder_AudioCallback: 接收 Audio Manager 麦克风 PCM 并快速写入静态环形缓冲区。
 * 参数：
 *   - eCommand: Audio Manager 回调事件。
 *   - pUserData: 未使用的用户数据。
 *   - ulReserved: 数据事件中为 audio_server_coming_data_t 指针。
 * 返回值：始终返回 0。
 ***************************/
static int Recorder_AudioCallback(audio_server_callback_cmt_t eCommand,
                                  void *pUserData,
                                  uint32_t ulReserved)
{
    audio_server_coming_data_t *pComingData;
    uint32_t ulWrittenBytes;

    (void)pUserData;
    if ((as_callback_cmd_data_coming != eCommand) ||
            (RECORDER_RECORD_STATE_RECORDING !=
             l_tRecorderContext.eRecordState) ||
            (0U == ulReserved))
    {
        return 0;
    }

    pComingData = (audio_server_coming_data_t *)(uintptr_t)ulReserved;
    if ((NULL == pComingData->data) || (0U == pComingData->data_len))
    {
        return 0;
    }
    if (pComingData->data_len >
            rt_ringbuffer_space_len(&l_tPcmRingBuffer))
    {
        l_tRecorderContext.ulDroppedPcmBytes += pComingData->data_len;
        return 0;
    }

    ulWrittenBytes = rt_ringbuffer_put(&l_tPcmRingBuffer,
                                       pComingData->data,
                                       pComingData->data_len);
    if (pComingData->data_len != ulWrittenBytes)
    {
        l_tRecorderContext.ulDroppedPcmBytes +=
            pComingData->data_len - ulWrittenBytes;
    }
    l_tRecorderContext.ulRecordSamples +=
        ulWrittenBytes / sizeof(int16_t);
    (void)rt_event_send(&l_tRecorderEvent, RECORDER_EVENT_PCM);

    return 0;
}

/***************************
 * Recorder_OpenAudio: 打开 16 kHz、16 位、单声道本地录音链路。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回 -RT_ERROR。
 ***************************/
static rt_err_t Recorder_OpenAudio(void)
{
    audio_parameter_t tParameters;

    if (NULL != l_tRecorderContext.pAudioClient)
    {
        rt_kprintf("[REC] microphone open rejected: previous client active\n");
        return -RT_EBUSY;
    }
    rt_memset(&tParameters, 0, sizeof(tParameters));
    tParameters.read_bits_per_sample = RECORDER_BITS_PER_SAMPLE;
    tParameters.read_channnel_num = RECORDER_CHANNEL_COUNT;
    tParameters.read_samplerate = RECORDER_SAMPLE_RATE_HZ;
    tParameters.read_cache_size = RECORDER_AUDIO_CACHE_BYTES;
    tParameters.write_bits_per_sample = RECORDER_BITS_PER_SAMPLE;
    tParameters.write_channnel_num = RECORDER_CHANNEL_COUNT;
    tParameters.write_samplerate = RECORDER_SAMPLE_RATE_HZ;
    tParameters.write_cache_size = RECORDER_AUDIO_CACHE_BYTES;

    l_tRecorderContext.pAudioClient = AUDIO_OpenWithCache(
        AUDIO_TYPE_LOCAL_RECORD,
        AUDIO_RX,
        &tParameters,
        Recorder_AudioCallback,
        NULL,
        (uint8_t *)(void *)l_adAudioRingMemory,
        (uint32_t)sizeof(l_adAudioRingMemory));
    if (NULL == l_tRecorderContext.pAudioClient)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

/***************************
 * Recorder_OpenSession: 创建文件、初始化选定编码器并打开麦克风。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回负错误码并删除无效文件。
 ***************************/
static rt_err_t Recorder_OpenSession(void)
{
    rt_err_t tResult;

    l_tRecorderContext.lFileDescriptor = -1;
    if (!l_tRecorderContext.bStreamOnly)
    {
        l_tRecorderContext.lFileDescriptor = open(
            l_tRecorderContext.aRecordPath,
            O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
            0666);
        if (0 > l_tRecorderContext.lFileDescriptor)
        {
            rt_kprintf("[REC] open file failed path=%s errno=%d\n",
                       l_tRecorderContext.aRecordPath,
                       rt_get_errno());
            return Recorder_GetErrnoResult();
        }
    }

    if (RECORDER_FORMAT_MP3 == l_tRecorderContext.eRecordFormat)
    {
        tResult = Recorder_InitMp3Encoder();
    }
    else if (RECORDER_FORMAT_AAC == l_tRecorderContext.eRecordFormat)
    {
        tResult = Recorder_InitAacEncoder();
    }
    else
    {
        tResult = Recorder_InitOpusEncoder();
    }
    if (RT_EOK != tResult)
    {
        rt_kprintf("[REC] encoder init failed format=%u result=%d\n",
                   (unsigned int)l_tRecorderContext.eRecordFormat,
                   (int)tResult);
    }
    if (RT_EOK == tResult)
    {
        tResult = Recorder_OpenAudio();
        if (RT_EOK != tResult)
        {
            rt_kprintf("[REC] microphone open failed result=%d\n",
                       (int)tResult);
        }
    }
    if (RT_EOK != tResult)
    {
        Recorder_CloseEncoders();
        if (0 <= l_tRecorderContext.lFileDescriptor)
        {
            (void)close(l_tRecorderContext.lFileDescriptor);
            l_tRecorderContext.lFileDescriptor = -1;
            (void)unlink(l_tRecorderContext.aRecordPath);
        }
    }
    else
    {
        if ((RECORDER_FORMAT_OPUS == l_tRecorderContext.eRecordFormat) &&
            (0 != Recorder_NotifyOpusUploader(
                RECORDER_OPUS_UPLOAD_STARTED,
                NULL,
                0U,
                0U,
                0U)))
        {
            l_tRecorderContext.ulUploadDroppedPackets++;
        }
        rt_kprintf("[REC] recording active format=%u path=%s\n",
                   (unsigned int)l_tRecorderContext.eRecordFormat,
                   l_tRecorderContext.bStreamOnly ?
                       "<stream>" : l_tRecorderContext.aRecordPath);
    }

    return tResult;
}

/***************************
 * Recorder_CloseSession: 关闭麦克风、处理剩余 PCM、刷新编码器并关闭文件。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回最先出现的负错误码。
 ***************************/
static rt_err_t Recorder_CloseSession(void)
{
    rt_err_t tResult;
    rt_err_t tCloseResult;

    tResult = RT_EOK;
    rt_kprintf("[REC] stop finalize begin pcm=%u file=%ld\n",
               (unsigned int)rt_ringbuffer_data_len(&l_tPcmRingBuffer),
               (long)l_tRecorderContext.lFileDescriptor);
    if (NULL != l_tRecorderContext.pAudioClient)
    {
        /* RECORDER_Stop has already blocked new PCM.  Let a callback that
         * entered just before the state transition leave the DMA path before
         * Audio Manager tears the codec down. */
        rt_thread_mdelay(RECORDER_AUDIO_STOP_SETTLE_MS);
        rt_kprintf("[REC] microphone close begin\n");
        tCloseResult = (rt_err_t)audio_close(
            l_tRecorderContext.pAudioClient);
        if (RT_EOK == tCloseResult)
        {
            l_tRecorderContext.pAudioClient = NULL;
        }
        rt_kprintf("[REC] microphone close done result=%d\n",
                   (int)tCloseResult);
        if (RT_EOK != tCloseResult)
        {
            tResult = tCloseResult;
        }
    }
    tCloseResult = RT_EOK;
    if ((0 <= l_tRecorderContext.lFileDescriptor) ||
        l_tRecorderContext.bStreamOnly)
    {
        rt_kprintf("[REC] encoder flush begin pcm=%u\n",
                   (unsigned int)rt_ringbuffer_data_len(
                       &l_tPcmRingBuffer));
        tCloseResult = Recorder_FlushEncoder();
        rt_kprintf("[REC] encoder flush done result=%d size=%lu\n",
                   (int)tCloseResult,
                   (unsigned long)l_tRecorderContext.ulFileSizeBytes);
    }
    if ((RT_EOK == tResult) && (RT_EOK != tCloseResult))
    {
        tResult = tCloseResult;
    }
    Recorder_CloseEncoders();
    if (0 <= l_tRecorderContext.lFileDescriptor)
    {
        if ((0 != close(l_tRecorderContext.lFileDescriptor)) &&
                (RT_EOK == tResult))
        {
            tResult = -RT_ERROR;
        }
        l_tRecorderContext.lFileDescriptor = -1;
        rt_kprintf("[REC] file close done result=%d\n", (int)tResult);
    }

    rt_kprintf("[REC] stop finalize done result=%d\n", (int)tResult);

    return tResult;
}

/***************************
 * Recorder_WorkerEntry: 串行执行编码与 SD 卡写入，确保音频回调保持短小。
 * 参数：
 *   - pParameter: 未使用的线程参数。
 * 返回值：无。
 ***************************/
static void Recorder_WorkerEntry(void *pParameter)
{
    rt_uint32_t ulEvents;
    rt_err_t tResult;

    (void)pParameter;
    while (true)
    {
        ulEvents = 0U;
        tResult = rt_event_recv(&l_tRecorderEvent,
                                RECORDER_EVENT_ALL,
                                RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                                RT_WAITING_FOREVER,
                                &ulEvents);
        if (RT_EOK != tResult)
        {
            continue;
        }

        if (0U != (ulEvents & RECORDER_EVENT_START))
        {
            rt_kprintf("[REC] worker received start\n");
            tResult = Recorder_OpenSession();
            if (RT_EOK == tResult)
            {
                l_tRecorderContext.eRecordState =
                    RECORDER_RECORD_STATE_RECORDING;
            }
            else
            {
                l_tRecorderContext.lLastError = tResult;
                l_tRecorderContext.eRecordState = RECORDER_RECORD_STATE_ERROR;
                rt_kprintf("[REC] start failed result=%d\n", (int)tResult);
                if (l_tRecorderContext.bStreamOnly)
                {
                    (void)Recorder_NotifyOpusUploader(
                        RECORDER_OPUS_UPLOAD_ERROR,
                        NULL,
                        0U,
                        0U,
                        0U);
                }
            }
        }
        if ((0U != (ulEvents & RECORDER_EVENT_PCM)) &&
                ((RECORDER_RECORD_STATE_RECORDING ==
                  l_tRecorderContext.eRecordState) ||
                 (RECORDER_RECORD_STATE_PAUSED ==
                  l_tRecorderContext.eRecordState)))
        {
            tResult = Recorder_ProcessPcm(false);
            if (RT_EOK != tResult)
            {
                l_tRecorderContext.lLastError = tResult;
                l_tRecorderContext.eRecordState =
                    RECORDER_RECORD_STATE_STOPPING;
                (void)rt_event_send(&l_tRecorderEvent,
                                    RECORDER_EVENT_STOP);
            }
        }
        if (0U != (ulEvents & RECORDER_EVENT_STOP))
        {
            rt_err_t tRefreshResult;
            rt_err_t tSessionError;

            rt_kprintf("[REC] worker received stop\n");
            tSessionError = (rt_err_t)l_tRecorderContext.lLastError;
            tResult = Recorder_CloseSession();
            if ((RT_EOK == tResult) && (RT_EOK != tSessionError))
            {
                tResult = tSessionError;
            }
            if (RECORDER_FORMAT_OPUS == l_tRecorderContext.eRecordFormat)
            {
                uint32_t ulDurationMs;

                ulDurationMs = (l_tRecorderContext.ulOpusPacketSequence *
                                RECORDER_OPUS_FRAME_SAMPLES * 1000U) /
                               RECORDER_SAMPLE_RATE_HZ;
                if (0 != Recorder_NotifyOpusUploader(
                    (RT_EOK == tResult) ? RECORDER_OPUS_UPLOAD_STOPPED :
                                         RECORDER_OPUS_UPLOAD_ERROR,
                    NULL,
                    0U,
                    l_tRecorderContext.ulOpusPacketSequence,
                    ulDurationMs))
                {
                    l_tRecorderContext.ulUploadDroppedPackets++;
                }
            }
            l_tRecorderContext.lLastError = tResult;
            l_tRecorderContext.eRecordState = (RT_EOK == tResult) ?
                RECORDER_RECORD_STATE_STOPPED :
                RECORDER_RECORD_STATE_ERROR;
            tRefreshResult = l_tRecorderContext.bStreamOnly ?
                RT_EOK : RECORDER_RefreshFiles();
            if ((RT_EOK == tResult) && (RT_EOK != tRefreshResult))
            {
                l_tRecorderContext.lLastError = tRefreshResult;
                l_tRecorderContext.eRecordState =
                    RECORDER_RECORD_STATE_ERROR;
            }
            rt_kprintf("[REC] stop complete close=%d refresh=%d files=%u\n",
                       (int)tResult,
                       (int)tRefreshResult,
                       (unsigned int)RECORDER_GetFileCount());
        }
    }
}

/***************************
 * Recorder_HasExtension: 不区分大小写判断文件扩展名。
 * 参数：
 *   - pName: 文件名。
 *   - pExtension: 含点号的扩展名。
 * 返回值：匹配返回 true，否则返回 false。
 ***************************/
static bool Recorder_HasExtension(const char *pName, const char *pExtension)
{
    size_t ulNameLength;
    size_t ulExtensionLength;
    size_t ulIndex;

    if ((NULL == pName) || (NULL == pExtension))
    {
        return false;
    }
    ulNameLength = rt_strlen(pName);
    ulExtensionLength = rt_strlen(pExtension);
    if (ulNameLength < ulExtensionLength)
    {
        return false;
    }
    for (ulIndex = 0U; ulIndex < ulExtensionLength; ulIndex++)
    {
        char cName;
        char cExtension;

        cName = pName[ulNameLength - ulExtensionLength + ulIndex];
        cExtension = pExtension[ulIndex];
        if (('A' <= cName) && ('Z' >= cName))
        {
            cName = (char)(cName + ('a' - 'A'));
        }
        if (('A' <= cExtension) && ('Z' >= cExtension))
        {
            cExtension = (char)(cExtension + ('a' - 'A'));
        }
        if (cName != cExtension)
        {
            return false;
        }
    }

    return true;
}

/***************************
 * Recorder_FormatFromName: 根据录音文件扩展名返回编码格式。
 * 参数：
 *   - pName: 文件名。
 *   - pFormat: 输出格式指针。
 * 返回值：支持的扩展名返回 true，否则返回 false。
 ***************************/
static bool Recorder_FormatFromName(const char *pName,
                                    RECORDER_FORMAT *pFormat)
{
    if ((NULL == pName) || (NULL == pFormat))
    {
        return false;
    }
    if (true == Recorder_HasExtension(pName, ".mp3"))
    {
        *pFormat = RECORDER_FORMAT_MP3;
        return true;
    }
    if (true == Recorder_HasExtension(pName, ".aac"))
    {
        *pFormat = RECORDER_FORMAT_AAC;
        return true;
    }
    if (true == Recorder_HasExtension(pName, ".opus"))
    {
        *pFormat = RECORDER_FORMAT_OPUS;
        return true;
    }

    return false;
}

/***************************
 * Recorder_CalculateAacDuration: 扫描 ADTS 页头并计算 AAC 时长。
 * 参数：
 *   - pPath: AAC 文件绝对路径。
 * 返回值：向下取整的秒数，解析失败返回 0。
 ***************************/
static uint32_t Recorder_CalculateAacDuration(const char *pPath)
{
    uint8_t aHeader[RECORDER_ADTS_HEADER_BYTES];
    uint32_t ulFrameCount;
    int lFileDescriptor;

    lFileDescriptor = open(pPath, O_RDONLY | O_BINARY, 0);
    if (0 > lFileDescriptor)
    {
        return 0U;
    }
    ulFrameCount = 0U;
    while (sizeof(aHeader) ==
            (size_t)read(lFileDescriptor, aHeader, sizeof(aHeader)))
    {
        uint32_t ulFrameLength;

        if ((0xFFU != aHeader[0]) || (0xF0U != (aHeader[1] & 0xF0U)))
        {
            break;
        }
        ulFrameLength = ((uint32_t)(aHeader[3] & 0x03U) << 11) |
                        ((uint32_t)aHeader[4] << 3) |
                        ((uint32_t)aHeader[5] >> 5);
        if ((RECORDER_ADTS_HEADER_BYTES >= ulFrameLength) ||
                (0 > lseek(lFileDescriptor,
                           (off_t)(ulFrameLength - RECORDER_ADTS_HEADER_BYTES),
                           SEEK_CUR)))
        {
            break;
        }
        ulFrameCount++;
    }
    (void)close(lFileDescriptor);

    return (ulFrameCount * RECORDER_AAC_FRAME_SAMPLES) /
           RECORDER_SAMPLE_RATE_HZ;
}

/***************************
 * Recorder_CalculateOpusDuration: 扫描 Ogg 页并读取最后一个 granule 位置。
 * 参数：
 *   - pPath: Ogg Opus 文件绝对路径。
 * 返回值：向下取整的秒数，解析失败返回 0。
 ***************************/
static uint32_t Recorder_CalculateOpusDuration(const char *pPath)
{
    uint8_t aHeader[RECORDER_OGG_HEADER_BYTES];
    uint8_t aSegments[255];
    uint64_t udLastGranule;
    int lFileDescriptor;

    lFileDescriptor = open(pPath, O_RDONLY | O_BINARY, 0);
    if (0 > lFileDescriptor)
    {
        return 0U;
    }
    udLastGranule = 0U;
    while (sizeof(aHeader) ==
            (size_t)read(lFileDescriptor, aHeader, sizeof(aHeader)))
    {
        uint32_t ulPageBytes;
        uint16_t usIndex;
        uint8_t ucSegmentCount;

        if (0 != rt_memcmp(aHeader, "OggS", 4U))
        {
            break;
        }
        udLastGranule = 0U;
        for (usIndex = 0U; usIndex < 8U; usIndex++)
        {
            udLastGranule |= ((uint64_t)aHeader[6U + usIndex] <<
                              (usIndex * 8U));
        }
        ucSegmentCount = aHeader[26];
        if ((int)ucSegmentCount !=
                read(lFileDescriptor,
                     aSegments,
                     ucSegmentCount))
        {
            break;
        }
        ulPageBytes = 0U;
        for (usIndex = 0U; usIndex < ucSegmentCount; usIndex++)
        {
            ulPageBytes += aSegments[usIndex];
        }
        if (0 > lseek(lFileDescriptor, (off_t)ulPageBytes, SEEK_CUR))
        {
            break;
        }
    }
    (void)close(lFileDescriptor);

    return (uint32_t)(udLastGranule / 48000U);
}

/***************************
 * Recorder_CalculateDuration: 按文件扩展名计算播放总时长。
 * 参数：
 *   - pPath: 录音文件绝对路径。
 * 返回值：向下取整的秒数，未知或解析失败返回 0。
 ***************************/
static uint32_t Recorder_CalculateDuration(const char *pPath)
{
    if (true == Recorder_HasExtension(pPath, ".mp3"))
    {
        mp3_info_t tInformation;

        rt_memset(&tInformation, 0, sizeof(tInformation));
        if (0 == mp3ctrl_getinfo(pPath, &tInformation))
        {
            return tInformation.total_time_in_seconds;
        }
    }
    else if (true == Recorder_HasExtension(pPath, ".aac"))
    {
        return Recorder_CalculateAacDuration(pPath);
    }
    else if (true == Recorder_HasExtension(pPath, ".opus"))
    {
        return Recorder_CalculateOpusDuration(pPath);
    }

    return 0U;
}

/***************************
 * Recorder_PlaybackNotify: 接收 FFmpeg 播放进度与生命周期通知。
 * 参数：
 *   - ulUserData: 未使用的用户值。
 *   - eCommand: FFmpeg 播放事件。
 *   - ulValue: 进度事件中的秒数。
 * 返回值：始终返回 0。
 ***************************/
static int Recorder_PlaybackNotify(uint32_t ulUserData,
                                   ffmpeg_cmd_e eCommand,
                                   uint32_t ulValue)
{
    (void)ulUserData;
    if (e_ffmpeg_progress == eCommand)
    {
        l_tRecorderContext.ulPlaybackSeconds = ulValue;
    }
    else if (e_ffmpeg_play_to_end == eCommand)
    {
        l_tRecorderContext.ulPlaybackSeconds =
            l_tRecorderContext.ulPlaybackDurationSeconds;
        l_tRecorderContext.ePlaybackState =
            RECORDER_PLAYBACK_STATE_ENDED;
    }
    else if (e_ffmpeg_suspended == eCommand)
    {
        l_tRecorderContext.ePlaybackState =
            RECORDER_PLAYBACK_STATE_PAUSED;
    }
    else if (e_ffmpeg_resumed == eCommand)
    {
        l_tRecorderContext.ePlaybackState =
            RECORDER_PLAYBACK_STATE_PLAYING;
    }
    else if (e_ffmpeg_play_to_error == eCommand)
    {
        l_tRecorderContext.lLastError = -RT_ERROR;
        l_tRecorderContext.ePlaybackState =
            RECORDER_PLAYBACK_STATE_ERROR;
    }
    else
    {
        /* 其他通知不改变应用状态。 */
    }

    return 0;
}

/***************************
 * Recorder_BuildRecordPath: 按 RTC 时间和系统 tick 生成唯一录音文件名。
 * 参数：
 *   - eFormat: 目标编码格式。
 *   - pOutput: 输出路径缓冲区。
 *   - ulOutputSize: 输出缓冲区容量。
 * 返回值：成功返回 RT_EOK，路径过长或格式无效返回 -RT_EINVAL。
 ***************************/
static rt_err_t Recorder_BuildRecordPath(RECORDER_FORMAT eFormat,
                                         char *pOutput,
                                         size_t ulOutputSize)
{
    static const char *aExtensions[RECORDER_FORMAT_COUNT] =
    {
        ".mp3", ".aac", ".opus"
    };
    struct tm *pLocalTime;
    time_t tNow;
    int lResult;

    if ((RECORDER_FORMAT_COUNT <= eFormat) ||
            (NULL == pOutput) || (0U == ulOutputSize))
    {
        return -RT_EINVAL;
    }
    tNow = time(NULL);
    pLocalTime = localtime(&tNow);
    if ((NULL != pLocalTime) && (120 <= pLocalTime->tm_year))
    {
        lResult = rt_snprintf(
            pOutput,
            ulOutputSize,
            RECORDER_DIRECTORY_PATH "/REC_%04d%02d%02d_%02d%02d%02d_%08lx%s",
            pLocalTime->tm_year + 1900,
            pLocalTime->tm_mon + 1,
            pLocalTime->tm_mday,
            pLocalTime->tm_hour,
            pLocalTime->tm_min,
            pLocalTime->tm_sec,
            (unsigned long)rt_tick_get(),
            aExtensions[eFormat]);
    }
    else
    {
        lResult = rt_snprintf(
            pOutput,
            ulOutputSize,
            RECORDER_DIRECTORY_PATH "/REC_%010lu%s",
            (unsigned long)rt_tick_get(),
            aExtensions[eFormat]);
    }
    if ((0 > lResult) || ((size_t)lResult >= ulOutputSize))
    {
        pOutput[0] = '\0';
        return -RT_EINVAL;
    }

    return RT_EOK;
}

/***************************
 * RECORDER_Start: 在 SD 卡 recordings 目录开始指定格式的新录音。
 * 参数：
 *   - eFormat: MP3、AAC 或 Opus。
 * 返回值：请求成功返回 RT_EOK，否则返回负错误码。
 ***************************/
static rt_err_t Recorder_StartInternal(RECORDER_FORMAT eFormat,
                                       bool bStreamOnly)
{
    rt_err_t tResult;

    rt_kprintf("[REC] start request format=%u stream=%u ready=%u rec=%u play=%u\n",
               (unsigned int)eFormat,
               (unsigned int)bStreamOnly,
               (unsigned int)l_bRecorderServiceReady,
               (unsigned int)l_tRecorderContext.eRecordState,
               (unsigned int)l_tRecorderContext.ePlaybackState);
    if ((false == l_bRecorderServiceReady) ||
        (RECORDER_FORMAT_COUNT <= eFormat) ||
        (bStreamOnly && (RECORDER_FORMAT_OPUS != eFormat)))
    {
        rt_kprintf("[REC] start rejected invalid state\n");
        return -RT_EINVAL;
    }
    if ((RECORDER_PLAYBACK_STATE_IDLE !=
         l_tRecorderContext.ePlaybackState) &&
        (RECORDER_PLAYBACK_STATE_ENDED !=
         l_tRecorderContext.ePlaybackState) &&
        (RECORDER_PLAYBACK_STATE_ERROR !=
         l_tRecorderContext.ePlaybackState))
    {
        return -RT_EBUSY;
    }
    if ((RECORDER_RECORD_STATE_IDLE != l_tRecorderContext.eRecordState) &&
        (RECORDER_RECORD_STATE_STOPPED !=
         l_tRecorderContext.eRecordState) &&
        (RECORDER_RECORD_STATE_ERROR !=
         l_tRecorderContext.eRecordState))
    {
        return -RT_EBUSY;
    }

    tResult = RECORDER_PlaybackStop();
    if (RT_EOK != tResult)
    {
        l_tRecorderContext.lLastError = tResult;
        rt_kprintf("[REC] previous playback still closing result=%d\n",
                   (int)tResult);
        return tResult;
    }

    if (!bStreamOnly)
    {
        tResult = TF_CARD_EnsureMounted();
        if (RT_EOK != tResult)
        {
            l_tRecorderContext.lLastError = tResult;
            rt_kprintf("[REC] TF mount failed result=%d\n", (int)tResult);
            return tResult;
        }
        if (0 != mkdir(RECORDER_DIRECTORY_PATH, 0777))
        {
            tResult = Recorder_GetErrnoResult();
            if (-EEXIST != tResult)
            {
                l_tRecorderContext.lLastError = tResult;
                return tResult;
            }
        }
        tResult = Recorder_BuildRecordPath(
            eFormat,
            l_tRecorderContext.aRecordPath,
            sizeof(l_tRecorderContext.aRecordPath));
        if (RT_EOK != tResult)
        {
            return tResult;
        }
    }
    else
    {
        l_tRecorderContext.aRecordPath[0] = '\0';
    }

    rt_ringbuffer_reset(&l_tPcmRingBuffer);
    l_tRecorderContext.eRecordFormat = eFormat;
    l_tRecorderContext.bStreamOnly = bStreamOnly;
    l_tRecorderContext.ulRecordSamples = 0U;
    l_tRecorderContext.ulFileSizeBytes = 0U;
    l_tRecorderContext.ulDroppedPcmBytes = 0U;
    l_tRecorderContext.ulUploadDroppedPackets = 0U;
    l_tRecorderContext.lLastError = 0;
    l_tRecorderContext.eRecordState = RECORDER_RECORD_STATE_STARTING;
    tResult = rt_event_send(&l_tRecorderEvent, RECORDER_EVENT_START);
    if (RT_EOK != tResult)
    {
        l_tRecorderContext.lLastError = tResult;
        l_tRecorderContext.eRecordState = RECORDER_RECORD_STATE_ERROR;
    }
    else
    {
        rt_kprintf("[REC] start queued path=%s\n",
                   bStreamOnly ? "<stream>" :
                       l_tRecorderContext.aRecordPath);
    }

    return tResult;
}

/***************************
 * RECORDER_Start: start one device-local recording file.
 * Parameters: eFormat selects MP3, AAC, or Opus storage.
 * Return: RT_EOK when queued, otherwise a negative error code.
 ***************************/
rt_err_t RECORDER_Start(RECORDER_FORMAT eFormat)
{
    return Recorder_StartInternal(eFormat, false);
}

/***************************
 * RECORDER_StartOpusStream: start an uploader-only Opus capture.
 * Parameters: none.
 * Return: RT_EOK when queued, -RT_EBUSY when another recorder owns the mic.
 ***************************/
rt_err_t RECORDER_StartOpusStream(void)
{
    return Recorder_StartInternal(RECORDER_FORMAT_OPUS, true);
}

/***************************
 * RECORDER_Pause: 暂停接收新的麦克风 PCM，保留当前文件和编码器上下文。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，状态不允许返回 -RT_EINVAL。
 ***************************/
rt_err_t RECORDER_Pause(void)
{
    if (RECORDER_RECORD_STATE_RECORDING !=
            l_tRecorderContext.eRecordState)
    {
        return -RT_EINVAL;
    }
    l_tRecorderContext.eRecordState = RECORDER_RECORD_STATE_PAUSED;

    return RT_EOK;
}

/***************************
 * RECORDER_Resume: 从暂停状态恢复接收麦克风 PCM。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，状态不允许返回 -RT_EINVAL。
 ***************************/
rt_err_t RECORDER_Resume(void)
{
    if (RECORDER_RECORD_STATE_PAUSED !=
            l_tRecorderContext.eRecordState)
    {
        return -RT_EINVAL;
    }
    l_tRecorderContext.eRecordState = RECORDER_RECORD_STATE_RECORDING;

    return RT_EOK;
}

/***************************
 * RECORDER_Stop: 请求结束录音并完整刷新编码器与文件。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，状态不允许返回 -RT_EINVAL。
 ***************************/
static rt_err_t Recorder_StopInternal(bool bStreamOnly)
{
    if (bStreamOnly != l_tRecorderContext.bStreamOnly)
    {
        return -RT_EBUSY;
    }
    if ((RECORDER_RECORD_STATE_RECORDING !=
         l_tRecorderContext.eRecordState) &&
        (RECORDER_RECORD_STATE_PAUSED !=
         l_tRecorderContext.eRecordState) &&
        (RECORDER_RECORD_STATE_STARTING !=
         l_tRecorderContext.eRecordState))
    {
        return -RT_EINVAL;
    }
    l_tRecorderContext.eRecordState = RECORDER_RECORD_STATE_STOPPING;

    return rt_event_send(&l_tRecorderEvent, RECORDER_EVENT_STOP);
}

/***************************
 * RECORDER_Stop: stop only a device-local recording.
 * Parameters: none.
 * Return: RT_EOK when queued, -RT_EBUSY when streaming owns the mic.
 ***************************/
rt_err_t RECORDER_Stop(void)
{
    return Recorder_StopInternal(false);
}

/***************************
 * RECORDER_StopOpusStream: stop only uploader-owned Opus capture.
 * Parameters: none.
 * Return: RT_EOK when queued, -RT_EBUSY when local recording owns the mic.
 ***************************/
rt_err_t RECORDER_StopOpusStream(void)
{
    return Recorder_StopInternal(true);
}

/***************************
 * RECORDER_GetSnapshot: 复制当前录音和播放状态快照。
 * 参数：
 *   - pSnapshot: 输出快照，不得为 NULL。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
rt_err_t RECORDER_GetSnapshot(RECORDER_SNAPSHOT *pSnapshot)
{
    rt_err_t tResult;

    if (NULL == pSnapshot)
    {
        return -RT_EINVAL;
    }
    if (false == l_bRecorderServiceReady)
    {
        return -RT_ERROR;
    }

    tResult = rt_mutex_take(&l_tRecorderMutex, RT_WAITING_FOREVER);
    if (RT_EOK != tResult)
    {
        return tResult;
    }
    rt_memset(pSnapshot, 0, sizeof(*pSnapshot));
    pSnapshot->eRecordState = l_tRecorderContext.eRecordState;
    pSnapshot->ePlaybackState = l_tRecorderContext.ePlaybackState;
    pSnapshot->eRecordFormat = l_tRecorderContext.eRecordFormat;
    rt_strncpy(pSnapshot->aRecordPath,
               l_tRecorderContext.aRecordPath,
               sizeof(pSnapshot->aRecordPath) - 1U);
    rt_strncpy(pSnapshot->aPlaybackPath,
               l_tRecorderContext.aPlaybackPath,
               sizeof(pSnapshot->aPlaybackPath) - 1U);
    pSnapshot->ulRecordSeconds =
        l_tRecorderContext.ulRecordSamples / RECORDER_SAMPLE_RATE_HZ;
    pSnapshot->ulPlaybackSeconds =
        l_tRecorderContext.ulPlaybackSeconds;
    pSnapshot->ulPlaybackDurationSeconds =
        l_tRecorderContext.ulPlaybackDurationSeconds;
    pSnapshot->ulFileSizeBytes = l_tRecorderContext.ulFileSizeBytes;
    pSnapshot->ulDroppedPcmBytes = l_tRecorderContext.ulDroppedPcmBytes;
    pSnapshot->ulUploadDroppedPackets =
        l_tRecorderContext.ulUploadDroppedPackets;
    pSnapshot->lLastError = l_tRecorderContext.lLastError;
    (void)rt_mutex_release(&l_tRecorderMutex);

    return RT_EOK;
}

/***************************
 * RECORDER_RefreshFiles: 重新扫描 recordings 目录并按名称从新到旧缓存录音。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
rt_err_t RECORDER_RefreshFiles(void)
{
    DIR *pDirectory;
    struct dirent *pEntry;
    struct stat tFileStat;
    RECORDER_FORMAT eFormat;
    RECORDER_FILE_INFO tFileInfo;
    rt_err_t tResult;

    tResult = TF_CARD_EnsureMounted();
    if (RT_EOK != tResult)
    {
        return tResult;
    }
    if (0 != mkdir(RECORDER_DIRECTORY_PATH, 0777))
    {
        tResult = Recorder_GetErrnoResult();
        if (-EEXIST != tResult)
        {
            return tResult;
        }
    }

    pDirectory = opendir(RECORDER_DIRECTORY_PATH);
    if (NULL == pDirectory)
    {
        return -RT_ERROR;
    }
    tResult = rt_mutex_take(&l_tRecorderMutex, RT_WAITING_FOREVER);
    if (RT_EOK != tResult)
    {
        (void)closedir(pDirectory);
        return tResult;
    }
    l_usRecorderFileCount = 0U;
    rt_memset(l_aRecorderFiles, 0, sizeof(l_aRecorderFiles));
    while (NULL != (pEntry = readdir(pDirectory)))
    {
        uint16_t usInsertIndex;

        if ((false == Recorder_FormatFromName(pEntry->d_name, &eFormat)) ||
                (RECORDER_FILE_MAX <= l_usRecorderFileCount))
        {
            continue;
        }
        rt_memset(&tFileInfo, 0, sizeof(tFileInfo));
        if ((0 > rt_snprintf(tFileInfo.aPath,
                             sizeof(tFileInfo.aPath),
                             RECORDER_DIRECTORY_PATH "/%s",
                             pEntry->d_name)) ||
                (0 != stat(tFileInfo.aPath, &tFileStat)) ||
                (false != S_ISDIR(tFileStat.st_mode)))
        {
            continue;
        }
        rt_strncpy(tFileInfo.aName,
                   pEntry->d_name,
                   sizeof(tFileInfo.aName) - 1U);
        tFileInfo.eFormat = eFormat;
        tFileInfo.ulSizeBytes = (uint32_t)tFileStat.st_size;

        usInsertIndex = l_usRecorderFileCount;
        while ((0U < usInsertIndex) &&
                (0 < rt_strcmp(tFileInfo.aName,
                               l_aRecorderFiles[usInsertIndex - 1U].aName)))
        {
            l_aRecorderFiles[usInsertIndex] =
                l_aRecorderFiles[usInsertIndex - 1U];
            usInsertIndex--;
        }
        l_aRecorderFiles[usInsertIndex] = tFileInfo;
        l_usRecorderFileCount++;
    }
    (void)rt_mutex_release(&l_tRecorderMutex);
    (void)closedir(pDirectory);

    return RT_EOK;
}

/***************************
 * RECORDER_GetFileCount: 返回当前缓存的录音文件数量。
 * 参数：无。
 * 返回值：范围 0~RECORDER_FILE_MAX 的文件数。
 ***************************/
uint16_t RECORDER_GetFileCount(void)
{
    uint16_t usCount;
    rt_err_t tResult;

    usCount = 0U;
    tResult = rt_mutex_take(&l_tRecorderMutex, RT_WAITING_FOREVER);
    if (RT_EOK == tResult)
    {
        usCount = l_usRecorderFileCount;
        (void)rt_mutex_release(&l_tRecorderMutex);
    }

    return usCount;
}

/***************************
 * RECORDER_GetFile: 按索引复制录音文件信息，索引 0 为最新文件。
 * 参数：
 *   - usIndex: 文件索引。
 *   - pFileInfo: 输出文件信息，不得为 NULL。
 * 返回值：成功返回 RT_EOK，索引无效返回 -RT_EINVAL。
 ***************************/
rt_err_t RECORDER_GetFile(uint16_t usIndex, RECORDER_FILE_INFO *pFileInfo)
{
    rt_err_t tResult;

    if (NULL == pFileInfo)
    {
        return -RT_EINVAL;
    }
    tResult = rt_mutex_take(&l_tRecorderMutex, RT_WAITING_FOREVER);
    if (RT_EOK != tResult)
    {
        return tResult;
    }
    if (l_usRecorderFileCount <= usIndex)
    {
        (void)rt_mutex_release(&l_tRecorderMutex);
        return -RT_EINVAL;
    }
    *pFileInfo = l_aRecorderFiles[usIndex];
    (void)rt_mutex_release(&l_tRecorderMutex);

    return RT_EOK;
}

/***************************
 * RECORDER_Play: 使用 FFmpeg 媒体服务播放指定 MP3、AAC 或 Ogg Opus 文件。
 * 参数：
 *   - pPath: SD 卡中的绝对录音路径。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
rt_err_t RECORDER_Play(const char *pPath)
{
    ffmpeg_config_t tConfiguration;
    struct stat tFileStat;
    rt_err_t tMountResult;
    int lResult;

    rt_kprintf("[REC] play request path=%s rec=%u play=%u\n",
               (NULL != pPath) ? pPath : "(null)",
               (unsigned int)l_tRecorderContext.eRecordState,
               (unsigned int)l_tRecorderContext.ePlaybackState);
    if ((NULL == pPath) ||
            (0 != rt_strncmp(pPath,
                             RECORDER_DIRECTORY_PATH "/",
                             rt_strlen(RECORDER_DIRECTORY_PATH) + 1U)))
    {
        rt_kprintf("[REC] play rejected invalid path\n");
        return -RT_EINVAL;
    }
    if ((RECORDER_RECORD_STATE_RECORDING ==
            l_tRecorderContext.eRecordState) ||
            (RECORDER_RECORD_STATE_PAUSED ==
             l_tRecorderContext.eRecordState) ||
            (RECORDER_RECORD_STATE_STARTING ==
             l_tRecorderContext.eRecordState) ||
            (RECORDER_RECORD_STATE_STOPPING ==
             l_tRecorderContext.eRecordState))
    {
        rt_kprintf("[REC] play rejected record busy state=%u\n",
                   (unsigned int)l_tRecorderContext.eRecordState);
        return -RT_EBUSY;
    }
    if (RT_TRUE == bt_audio_sink_is_streaming())
    {
        rt_kprintf("[REC] play rejected BT streaming\n");
        return -RT_EBUSY;
    }
    tMountResult = TF_CARD_EnsureMounted();
    if (RT_EOK != tMountResult)
    {
        rt_kprintf("[REC] play mount failed result=%d\n",
                   (int)tMountResult);
        return tMountResult;
    }
    if ((0 != stat(pPath, &tFileStat)) || (0U == tFileStat.st_size))
    {
        rt_kprintf("[REC] play file invalid errno=%d\n", rt_get_errno());
        return -RT_ERROR;
    }

    tMountResult = RECORDER_PlaybackStop();
    if (RT_EOK != tMountResult)
    {
        rt_kprintf("[REC] previous playback close failed result=%d\n",
                   (int)tMountResult);
        return tMountResult;
    }
    (void)local_music_stop();
    rt_strncpy(l_tRecorderContext.aPlaybackPath,
               pPath,
               sizeof(l_tRecorderContext.aPlaybackPath) - 1U);
    l_tRecorderContext.aPlaybackPath[
        sizeof(l_tRecorderContext.aPlaybackPath) - 1U] = '\0';
    l_tRecorderContext.ulPlaybackSeconds = 0U;
    l_tRecorderContext.ulPlaybackDurationSeconds = 0U;
    rt_kprintf("[REC] play open size=%lu duration=%lu\n",
               (unsigned long)tFileStat.st_size,
               (unsigned long)l_tRecorderContext.ulPlaybackDurationSeconds);
    l_tRecorderContext.ePlaybackState = RECORDER_PLAYBACK_STATE_STARTING;
    l_tRecorderContext.lLastError = 0;

    rt_memset(&tConfiguration, 0, sizeof(tConfiguration));
    tConfiguration.src = e_src_localfile;
    tConfiguration.fmt = IMG_DESC_FMT_RGB565;
    tConfiguration.is_loop = 0U;
    tConfiguration.audio_enable = 1U;
    tConfiguration.video_enable = 0U;
    tConfiguration.file_path = l_tRecorderContext.aPlaybackPath;
    tConfiguration.mem_malloc = ffmpeg_alloc;
    tConfiguration.mem_free = ffmpeg_free;
    tConfiguration.notify = Recorder_PlaybackNotify;
    tConfiguration.audio_ring_buffer =
        (uint8_t *)(void *)l_adAudioRingMemory;
    tConfiguration.audio_ring_buffer_size =
        (uint32_t)sizeof(l_adAudioRingMemory);
    lResult = ffmpeg_open(&l_tRecorderContext.pPlaybackHandle,
                          &tConfiguration,
                          0U);
    rt_kprintf("[REC] ffmpeg open result=%d handle=%p\n",
               lResult,
               l_tRecorderContext.pPlaybackHandle);
    if ((0 != lResult) || (NULL == l_tRecorderContext.pPlaybackHandle))
    {
        l_tRecorderContext.pPlaybackHandle = NULL;
        l_tRecorderContext.lLastError = (0 != lResult) ?
                                        lResult : -RT_ERROR;
        l_tRecorderContext.ePlaybackState = RECORDER_PLAYBACK_STATE_ERROR;
        return -RT_ERROR;
    }
    l_tRecorderContext.ulPlaybackDurationSeconds =
        ffmpeg_get_duration(l_tRecorderContext.pPlaybackHandle);
    rt_kprintf("[REC] play ready duration=%lu\n",
               (unsigned long)l_tRecorderContext.ulPlaybackDurationSeconds);
    l_tRecorderContext.ePlaybackState = RECORDER_PLAYBACK_STATE_PLAYING;

    return RT_EOK;
}

/***************************
 * RECORDER_PlaybackPause: 暂停当前本地播放。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，状态不允许返回 -RT_EINVAL。
 ***************************/
rt_err_t RECORDER_PlaybackPause(void)
{
    if ((NULL == l_tRecorderContext.pPlaybackHandle) ||
            (RECORDER_PLAYBACK_STATE_PLAYING !=
             l_tRecorderContext.ePlaybackState))
    {
        return -RT_EINVAL;
    }
    ffmpeg_pause(l_tRecorderContext.pPlaybackHandle);
    l_tRecorderContext.ePlaybackState = RECORDER_PLAYBACK_STATE_PAUSED;

    return RT_EOK;
}

/***************************
 * RECORDER_PlaybackResume: 恢复当前本地播放。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，状态不允许返回 -RT_EINVAL。
 ***************************/
rt_err_t RECORDER_PlaybackResume(void)
{
    if ((NULL == l_tRecorderContext.pPlaybackHandle) ||
            (RECORDER_PLAYBACK_STATE_PAUSED !=
             l_tRecorderContext.ePlaybackState))
    {
        return -RT_EINVAL;
    }
    ffmpeg_resume(l_tRecorderContext.pPlaybackHandle);
    l_tRecorderContext.ePlaybackState = RECORDER_PLAYBACK_STATE_PLAYING;

    return RT_EOK;
}

/***************************
 * RECORDER_PlaybackSeekRelative: 相对当前位置快进或快退并限制在有效时轴内。
 * 参数：
 *   - lDeltaSeconds: 正数快进、负数快退；UI 默认使用 10 秒步进。
 * 返回值：成功返回 RT_EOK，未播放返回 -RT_EINVAL。
 ***************************/
rt_err_t RECORDER_PlaybackSeekRelative(int32_t lDeltaSeconds)
{
    int64_t dTargetSeconds;

    if ((NULL == l_tRecorderContext.pPlaybackHandle) ||
            (RECORDER_PLAYBACK_STATE_IDLE ==
             l_tRecorderContext.ePlaybackState) ||
            (RECORDER_PLAYBACK_STATE_ERROR ==
             l_tRecorderContext.ePlaybackState))
    {
        return -RT_EINVAL;
    }
    dTargetSeconds = (int64_t)l_tRecorderContext.ulPlaybackSeconds +
                     lDeltaSeconds;
    if (0 > dTargetSeconds)
    {
        dTargetSeconds = 0;
    }
    if ((0U < l_tRecorderContext.ulPlaybackDurationSeconds) &&
            ((int64_t)l_tRecorderContext.ulPlaybackDurationSeconds <
             dTargetSeconds))
    {
        dTargetSeconds = l_tRecorderContext.ulPlaybackDurationSeconds;
    }
    ffmpeg_seek(l_tRecorderContext.pPlaybackHandle,
                (uint32_t)dTargetSeconds);
    l_tRecorderContext.ulPlaybackSeconds = (uint32_t)dTargetSeconds;

    return RT_EOK;
}

/***************************
 * RECORDER_PlaybackRestart: 从头重新打开并播放当前录音文件。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回负错误码。
 ***************************/
rt_err_t RECORDER_PlaybackRestart(void)
{
    char aPath[RECORDER_PATH_LENGTH];
    rt_err_t tResult;

    if ('\0' == l_tRecorderContext.aPlaybackPath[0])
    {
        return -RT_EINVAL;
    }
    rt_strncpy(aPath,
               l_tRecorderContext.aPlaybackPath,
               sizeof(aPath) - 1U);
    aPath[sizeof(aPath) - 1U] = '\0';
    tResult = RECORDER_PlaybackStop();
    if (RT_EOK != tResult)
    {
        return tResult;
    }

    return RECORDER_Play(aPath);
}

/***************************
 * RECORDER_PlaybackStop: 结束当前本地播放并释放 FFmpeg 实例。
 * 参数：无。
 * 返回值：释放完成返回 RT_EOK，超时返回 -RT_ETIMEOUT 并阻止复用音频工作区。
 ***************************/
rt_err_t RECORDER_PlaybackStop(void)
{
    ffmpeg_handle pPlaybackHandle;
    uint32_t ulStartMilliseconds;

    if (NULL != l_tRecorderContext.pPlaybackHandle)
    {
        pPlaybackHandle = l_tRecorderContext.pPlaybackHandle;
        ffmpeg_close(pPlaybackHandle);
        ulStartMilliseconds = rt_tick_get_millisecond();
        while (false == ffmpeg_is_closed(pPlaybackHandle))
        {
            if (RECORDER_PLAYBACK_STOP_TIMEOUT_MS <=
                    (rt_tick_get_millisecond() - ulStartMilliseconds))
            {
                l_tRecorderContext.lLastError = -RT_ETIMEOUT;
                l_tRecorderContext.ePlaybackState =
                    RECORDER_PLAYBACK_STATE_ERROR;
                rt_kprintf("[REC] playback close timeout handle=%p\n",
                           pPlaybackHandle);
                return -RT_ETIMEOUT;
            }
            rt_thread_mdelay(RECORDER_PLAYBACK_STOP_POLL_MS);
        }
        l_tRecorderContext.pPlaybackHandle = NULL;
    }
    l_tRecorderContext.ePlaybackState = RECORDER_PLAYBACK_STATE_IDLE;
    l_tRecorderContext.ulPlaybackSeconds = 0U;

    return RT_EOK;
}

/***************************
 * RECORDER_RegisterOpusUploader: 注册上位机实时上传的非阻塞 Opus 会话接口。
 * 参数：
 *   - pCallback: Opus 会话和编码包回调；传 NULL 可注销。
 *   - pContext: 原样回传给回调的上下文。
 * 返回值：成功返回 RT_EOK。
 ***************************/
rt_err_t RECORDER_RegisterOpusUploader(
    RECORDER_OPUS_UPLOAD_CALLBACK pCallback,
    void *pContext)
{
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    l_pOpusUploadCallback = pCallback;
    l_pOpusUploadContext = pContext;
    rt_hw_interrupt_enable(tLevel);

    return RT_EOK;
}

/***************************
 * Recorder_Init: 初始化静态缓冲区、同步对象、编码线程和 FFmpeg 扩展编解码器。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回 RT-Thread 错误码。
 ***************************/
static int Recorder_Init(void)
{
    size_t ulRecorderWorkBytes;
    rt_err_t tResult;

    ulRecorderWorkBytes = (size_t)(&__recorder_psram_bss_end__ -
                                   &__recorder_psram_bss_start__);
    rt_memset(&__recorder_psram_bss_start__, 0, ulRecorderWorkBytes);
    rt_memset(&l_tRecorderContext, 0, sizeof(l_tRecorderContext));
    l_tRecorderContext.lFileDescriptor = -1;
    l_tRecorderContext.eRecordState = RECORDER_RECORD_STATE_IDLE;
    l_tRecorderContext.ePlaybackState = RECORDER_PLAYBACK_STATE_IDLE;
    rt_ringbuffer_init(&l_tPcmRingBuffer,
                       l_aPcmRingMemory,
                       (rt_int16_t)sizeof(l_aPcmRingMemory));

    tResult = rt_mutex_init(&l_tRecorderMutex,
                            "rec_state",
                            RT_IPC_FLAG_PRIO);
    if (RT_EOK != tResult)
    {
        rt_kprintf("[REC] init mutex failed result=%d\n", (int)tResult);
        return tResult;
    }
    tResult = rt_event_init(&l_tRecorderEvent,
                            "rec_event",
                            RT_IPC_FLAG_PRIO);
    if (RT_EOK != tResult)
    {
        rt_kprintf("[REC] init event failed result=%d\n", (int)tResult);
        (void)rt_mutex_detach(&l_tRecorderMutex);
        return tResult;
    }
    tResult = rt_thread_init(&l_tRecorderThread,
                             "recorder",
                             Recorder_WorkerEntry,
                             NULL,
                             l_aRecorderThreadStack,
                             sizeof(l_aRecorderThreadStack),
                             RECORDER_WORKER_PRIORITY,
                             RECORDER_WORKER_TICK);
    if (RT_EOK != tResult)
    {
        rt_kprintf("[REC] init thread failed result=%d\n", (int)tResult);
        (void)rt_event_detach(&l_tRecorderEvent);
        (void)rt_mutex_detach(&l_tRecorderMutex);
        return tResult;
    }
    tResult = rt_thread_startup(&l_tRecorderThread);
    if (RT_EOK != tResult)
    {
        rt_kprintf("[REC] start thread failed result=%d\n", (int)tResult);
        (void)rt_thread_detach(&l_tRecorderThread);
        (void)rt_event_detach(&l_tRecorderEvent);
        (void)rt_mutex_detach(&l_tRecorderMutex);
        return tResult;
    }

    if (false == l_bFfmpegComponentsRegistered)
    {
        av_register_all();
        av_register_input_format(&ff_ogg_demuxer);
        avcodec_register(&ff_aac_encoder);
        avcodec_register(&ff_libopus_decoder);
        l_bFfmpegComponentsRegistered = true;
    }
    l_bRecorderServiceReady = true;
    rt_kprintf("[REC] service ready work=%lu stack=%u\n",
               (unsigned long)ulRecorderWorkBytes,
               (unsigned int)RECORDER_WORKER_STACK_BYTES);

    return RT_EOK;
}

/***************************
 * Recorder_DiagnosticCommand: expose bounded Opus recording diagnostics to MSH.
 * Parameters:
 *   - lArgumentCount: command argument count.
 *   - pArguments: start, stop, or status command arguments.
 * Return: none.
 ***************************/
static void Recorder_DiagnosticCommand(int lArgumentCount, char **pArguments)
{
    RECORDER_SNAPSHOT tSnapshot;
    rt_err_t tResult;

    if ((NULL == pArguments) || (2 != lArgumentCount))
    {
        rt_kprintf("usage: recdiag start | stop | status\n");
        return;
    }

    if (0 == rt_strcmp(pArguments[1], "start"))
    {
        tResult = RECORDER_Start(RECORDER_FORMAT_OPUS);
        rt_kprintf("[REC_DIAG] start result=%d\n", (int)tResult);
    }
    else if (0 == rt_strcmp(pArguments[1], "stop"))
    {
        tResult = RECORDER_Stop();
        rt_kprintf("[REC_DIAG] stop result=%d\n", (int)tResult);
    }
    else if (0 == rt_strcmp(pArguments[1], "status"))
    {
        tResult = RECORDER_GetSnapshot(&tSnapshot);
        if (RT_EOK == tResult)
        {
            rt_kprintf(
                "[REC_DIAG] state=%u seconds=%lu upload_drop=%lu "
                "pcm_drop=%lu error=%ld\n",
                (unsigned int)tSnapshot.eRecordState,
                (unsigned long)tSnapshot.ulRecordSeconds,
                (unsigned long)tSnapshot.ulUploadDroppedPackets,
                (unsigned long)tSnapshot.ulDroppedPcmBytes,
                (long)tSnapshot.lLastError);
        }
        else
        {
            rt_kprintf("[REC_DIAG] status result=%d\n", (int)tResult);
        }
    }
    else
    {
        rt_kprintf("usage: recdiag start | stop | status\n");
    }

    return;
}

MSH_CMD_EXPORT_ALIAS(Recorder_DiagnosticCommand,
                     recdiag,
                     recorder Opus diagnostic command);
INIT_APP_EXPORT(Recorder_Init);
