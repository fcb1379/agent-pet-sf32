#include "agent_pet_audio_upload.h"

#include <stdint.h>
#include <string.h>

#include <rtthread.h>
#include <rthw.h>

#include "agent_pet_audio_protocol.h"
#include "agent_pet_ble_service.h"
#include "recorder_service.h"

#define LOG_TAG "agent_pet_audio"
#include "log.h"

#ifdef AGENT_PET_USING_TF_CARD

#define AGENTPET_AUDIO_QUEUE_DEPTH          (32U)
#define AGENTPET_AUDIO_QUEUE_RESERVED       (2U)
#define AGENTPET_AUDIO_THREAD_STACK_SIZE    (4096U)
#define AGENTPET_AUDIO_THREAD_PRIORITY      (18U)
#define AGENTPET_AUDIO_THREAD_TIME_SLICE    (10U)
#define AGENTPET_AUDIO_SEND_RETRY_MS        (2U)
#define AGENTPET_AUDIO_SEND_TIMEOUT_MS      (500U)
#define AGENTPET_AUDIO_NOTIFY_PACE_MS        (1U)
#define AGENTPET_AUDIO_CONTROL_SETTLE_MS     (50U)
#define AGENTPET_AUDIO_ERROR_TRANSPORT      (1U)
#define AGENTPET_AUDIO_ERROR_BUSY           (2U)
#define AGENTPET_AUDIO_ERROR_STATE          (3U)

/* AGENTPET_AUDIO_QUEUE_KIND: distinguishes recorder data from controls. */
typedef enum _AGENTPET_AUDIO_QUEUE_KIND
{
    AGENTPET_AUDIO_QUEUE_RECORDER = 0,
    AGENTPET_AUDIO_QUEUE_CONTROL,
} AGENTPET_AUDIO_QUEUE_KIND;

/* AGENTPET_AUDIO_QUEUE_ENTRY: one recorder event copied into the upload queue.
 * Members:
 *   - eEvent: recorder stream lifecycle event
 *   - usSession: uploader-assigned nonzero BLE stream identifier
 *   - usPacketLength: valid byte count in aPacket
 *   - ulSequence: source Opus packet sequence or final packet count
 *   - ulTimestampMs: source stream timestamp for diagnostics
 *   - ulDroppedPackets: uploader queue drops observed before the end event
 *   - aPacket: fixed-capacity raw Opus packet storage
 */
typedef struct _AGENTPET_AUDIO_QUEUE_ENTRY
{
    RECORDER_OPUS_UPLOAD_EVENT eEvent;
    uint16_t usSession;
    AGENTPET_AUDIO_QUEUE_KIND eKind;
    bool bStartControl;
    uint16_t usPacketLength;
    uint32_t ulSequence;
    uint32_t ulTimestampMs;
    uint32_t ulDroppedPackets;
    uint8_t aPacket[RECORDER_OPUS_MAX_PACKET_BYTES];
} AGENTPET_AUDIO_QUEUE_ENTRY;

/* Static queue: 32 entries, approximately 41 KiB including RT-Thread links.
 * Two entries stay reserved for lifecycle control so END/ERROR cannot be
 * displaced by a short BLE congestion burst. */
static struct rt_messagequeue l_tAudioQueue;
static uint8_t l_aAudioQueuePool[
    (RT_ALIGN(sizeof(AGENTPET_AUDIO_QUEUE_ENTRY), RT_ALIGN_SIZE) +
     sizeof(void *)) * AGENTPET_AUDIO_QUEUE_DEPTH];
/* Static worker thread: fragments raw Opus packets and waits for BLE TX slots. */
static struct rt_thread l_tAudioThread;
static uint8_t l_aAudioThreadStack[AGENTPET_AUDIO_THREAD_STACK_SIZE];
/* Upload state shared by the recorder callback and CCCD handler. */
static bool l_bAudioWorkerReady;
static bool l_bAudioSubscribed;
static bool l_bAudioSessionActive;
static bool l_bAudioStopPending;
static uint16_t l_usAudioSessionCounter;
static uint16_t l_usAudioActiveSession;
static uint32_t l_ulAudioDroppedPackets;

/***************************
 * Local_WriteLe16: encode a bounded little-endian 16-bit field.
 * Parameters: pData is a two-byte output, usValue is the input value.
 * Return: none.
 ***************************/
static void Local_WriteLe16(uint8_t *pData, uint16_t usValue)
{
    pData[0] = (uint8_t)(usValue & 0xFFU);
    pData[1] = (uint8_t)((usValue >> 8U) & 0xFFU);

    return;
}

/***************************
 * Local_WriteLe24: encode a bounded little-endian 24-bit field.
 * Parameters: pData is a three-byte output, ulValue is limited to 24 bits.
 * Return: none.
 ***************************/
static void Local_WriteLe24(uint8_t *pData, uint32_t ulValue)
{
    pData[0] = (uint8_t)(ulValue & 0xFFU);
    pData[1] = (uint8_t)((ulValue >> 8U) & 0xFFU);
    pData[2] = (uint8_t)((ulValue >> 16U) & 0xFFU);

    return;
}

/***************************
 * Local_WriteLe32: encode a bounded little-endian 32-bit field.
 * Parameters: pData is a four-byte output, ulValue is the input value.
 * Return: none.
 ***************************/
static void Local_WriteLe32(uint8_t *pData, uint32_t ulValue)
{
    pData[0] = (uint8_t)(ulValue & 0xFFU);
    pData[1] = (uint8_t)((ulValue >> 8U) & 0xFFU);
    pData[2] = (uint8_t)((ulValue >> 16U) & 0xFFU);
    pData[3] = (uint8_t)((ulValue >> 24U) & 0xFFU);

    return;
}

/***************************
 * Local_SendFrame: submit one notification with bounded TX-slot retry.
 * Parameters:
 *   - pFrame: validated notification frame.
 *   - usFrameLength: frame length within the negotiated ATT value limit.
 * Return: true after the BLE stack copies the frame, otherwise false.
 ***************************/
static bool Local_SendFrame(const uint8_t *pFrame, uint16_t usFrameLength)
{
    uint32_t ulStartedAt;
    int32_t lResult;

    ulStartedAt = rt_tick_get_millisecond();
    do
    {
        lResult = AGENTPETBLE_SendAudioNotification(pFrame, usFrameLength);
        if ((int32_t)usFrameLength == lResult)
        {
            /* Yield below the GUI after each accepted notification.  The BLE
             * stack owns only eight TX slots, so an unpaced tail burst can
             * otherwise monopolize the CPU and destabilize the connection. */
            rt_thread_mdelay(AGENTPET_AUDIO_NOTIFY_PACE_MS);
            return true;
        }
        if (0 > lResult)
        {
            return false;
        }
        rt_thread_mdelay(AGENTPET_AUDIO_SEND_RETRY_MS);
    } while (AGENTPET_AUDIO_SEND_TIMEOUT_MS >
             (rt_tick_get_millisecond() - ulStartedAt));

    return false;
}

/***************************
 * Local_SendPayload: fragment and send one logical protocol payload.
 * Parameters:
 *   - eType: start, data, end, or error frame type.
 *   - usSession: current nonzero session identifier.
 *   - ulSequence: 24-bit source sequence.
 *   - pPayload: payload bytes; NULL is valid when length is zero.
 *   - usPayloadLength: payload length up to one raw Opus packet.
 * Return: true when every fragment is queued to the BLE stack.
 ***************************/
static bool Local_SendPayload(AGENTPET_AUDIO_FRAME_TYPE eType,
                              uint16_t usSession,
                              uint32_t ulSequence,
                              const uint8_t *pPayload,
                              uint16_t usPayloadLength)
{
    uint8_t aFrame[AGENTPET_AUDIO_FRAME_MAX_SIZE];
    uint16_t usFrameLimit;
    uint16_t usPayloadLimit;
    uint16_t usOffset;
    uint16_t usFrameLength;
    uint8_t ucFragmentCount;
    uint8_t ucFragmentIndex;
    uint8_t ucFragmentLength;

    if ((0U < usPayloadLength) && (NULL == pPayload))
    {
        return false;
    }
    usFrameLimit = AGENTPETBLE_GetAudioFrameLimit();
    if ((AGENTPET_AUDIO_FRAME_OVERHEAD >= usFrameLimit) ||
        (AGENTPET_AUDIO_FRAME_MAX_SIZE < usFrameLimit))
    {
        return false;
    }
    usPayloadLimit = usFrameLimit - AGENTPET_AUDIO_FRAME_OVERHEAD;
    ucFragmentCount = (0U == usPayloadLength) ? 1U :
        (uint8_t)((usPayloadLength + usPayloadLimit - 1U) / usPayloadLimit);
    if ((0U == ucFragmentCount) ||
        (255U < ((usPayloadLength + usPayloadLimit - 1U) / usPayloadLimit)))
    {
        return false;
    }

    usOffset = 0U;
    for (ucFragmentIndex = 0U;
         ucFragmentIndex < ucFragmentCount;
         ucFragmentIndex++)
    {
        ucFragmentLength = (uint8_t)((usPayloadLength - usOffset) >
            usPayloadLimit ? usPayloadLimit : (usPayloadLength - usOffset));
        usFrameLength = AGENTPETAUDIOPROTO_BuildFrame(
            eType,
            usSession,
            ulSequence,
            ucFragmentIndex,
            ucFragmentCount,
            (0U == ucFragmentLength) ? NULL : &pPayload[usOffset],
            ucFragmentLength,
            aFrame,
            usFrameLimit);
        if ((0U == usFrameLength) ||
            !Local_SendFrame(aFrame, usFrameLength))
        {
            return false;
        }
        usOffset += ucFragmentLength;
    }

    return true;
}

/***************************
 * Local_SendStart: serialize the fixed 16 kHz mono Opus stream metadata.
 * Parameters: usSession is the current nonzero stream identifier.
 * Return: true when the complete control payload is queued.
 ***************************/
static bool Local_SendStart(uint16_t usSession)
{
    uint8_t aPayload[AGENTPET_AUDIO_START_PAYLOAD_SIZE];

    Local_WriteLe32(&aPayload[0], RECORDER_OPUS_SAMPLE_RATE_HZ);
    aPayload[4] = RECORDER_OPUS_CHANNEL_COUNT;
    Local_WriteLe16(&aPayload[5], RECORDER_OPUS_FRAME_SAMPLES);
    Local_WriteLe16(&aPayload[7], RECORDER_OPUS_PRE_SKIP);
    Local_WriteLe24(&aPayload[9], RECORDER_OPUS_BITRATE_BPS);

    return Local_SendPayload(AGENTPET_AUDIO_FRAME_START,
                             usSession,
                             0U,
                             aPayload,
                             sizeof(aPayload));
}

/***************************
 * Local_SendEnd: send packet totals and any source-side queue loss.
 * Parameters:
 *   - pEntry: validated STOPPED or ERROR queue entry.
 *   - bTransportFailed: true when an earlier BLE fragment failed.
 * Return: none.
 ***************************/
static void Local_SendEnd(const AGENTPET_AUDIO_QUEUE_ENTRY *pEntry,
                          bool bTransportFailed)
{
    uint8_t aPayload[AGENTPET_AUDIO_END_PAYLOAD_SIZE];
    uint8_t ucError;

    if ((NULL == pEntry) || bTransportFailed ||
        (RECORDER_OPUS_UPLOAD_ERROR == pEntry->eEvent))
    {
        ucError = AGENTPET_AUDIO_ERROR_TRANSPORT;
        if (NULL != pEntry)
        {
            (void)Local_SendPayload(AGENTPET_AUDIO_FRAME_ERROR,
                                    pEntry->usSession,
                                    pEntry->ulSequence &
                                        AGENTPET_AUDIO_SEQUENCE_MAX,
                                    &ucError,
                                    sizeof(ucError));
        }
        return;
    }

    Local_WriteLe32(&aPayload[0], pEntry->ulSequence);
    Local_WriteLe32(&aPayload[4], pEntry->ulDroppedPackets);
    (void)Local_SendPayload(AGENTPET_AUDIO_FRAME_END,
                            pEntry->usSession,
                            pEntry->ulSequence &
                                AGENTPET_AUDIO_SEQUENCE_MAX,
                            aPayload,
                            sizeof(aPayload));

    return;
}

/***************************
 * Local_AudioWorker: serialize queued recorder events onto the notify stream.
 * Parameters: pParameter is unused.
 * Return: none.
 ***************************/
static void Local_AudioWorker(void *pParameter)
{
    AGENTPET_AUDIO_QUEUE_ENTRY tEntry;
    uint16_t usWorkerSession;
    uint8_t ucControlError;
    bool bTransportFailed;
    rt_err_t tResult;

    (void)pParameter;
    usWorkerSession = 0U;
    bTransportFailed = false;
    while (true)
    {
        tResult = rt_mq_recv(&l_tAudioQueue,
                             &tEntry,
                             sizeof(tEntry),
                             RT_WAITING_FOREVER);
        if (RT_EOK != tResult)
        {
            continue;
        }

        if (AGENTPET_AUDIO_QUEUE_CONTROL == tEntry.eKind)
        {
            rt_thread_mdelay(AGENTPET_AUDIO_CONTROL_SETTLE_MS);
            tResult = tEntry.bStartControl ?
                RECORDER_StartOpusStream() : RECORDER_StopOpusStream();
            if (RT_EOK != tResult)
            {
                rt_enter_critical();
                if (tEntry.bStartControl)
                {
                    l_bAudioSessionActive = false;
                }
                else
                {
                    l_bAudioStopPending = false;
                }
                rt_exit_critical();
                ucControlError = (-RT_EBUSY == tResult) ?
                    AGENTPET_AUDIO_ERROR_BUSY : AGENTPET_AUDIO_ERROR_STATE;
                if (0U == usWorkerSession)
                {
                    usWorkerSession = l_usAudioActiveSession;
                }
                if (0U != usWorkerSession)
                {
                    (void)Local_SendPayload(AGENTPET_AUDIO_FRAME_ERROR,
                                            usWorkerSession,
                                            0U,
                                            &ucControlError,
                                            sizeof(ucControlError));
                }
                usWorkerSession = 0U;
                bTransportFailed = false;
                LOG_W("Audio control rejected start=%u result=%d",
                      (unsigned int)tEntry.bStartControl,
                      (int)tResult);
            }
            continue;
        }

        if (RECORDER_OPUS_UPLOAD_STARTED == tEntry.eEvent)
        {
            usWorkerSession = tEntry.usSession;
            bTransportFailed = !Local_SendStart(tEntry.usSession);
        }
        else if ((RECORDER_OPUS_UPLOAD_PACKET == tEntry.eEvent) &&
                 (usWorkerSession == tEntry.usSession) &&
                 !bTransportFailed)
        {
            if ((AGENTPET_AUDIO_SEQUENCE_MAX < tEntry.ulSequence) ||
                !Local_SendPayload(AGENTPET_AUDIO_FRAME_DATA,
                                   tEntry.usSession,
                                   tEntry.ulSequence,
                                   tEntry.aPacket,
                                   tEntry.usPacketLength))
            {
                bTransportFailed = true;
            }
        }
        else if (((RECORDER_OPUS_UPLOAD_STOPPED == tEntry.eEvent) ||
                  (RECORDER_OPUS_UPLOAD_ERROR == tEntry.eEvent)) &&
                 ((usWorkerSession == tEntry.usSession) ||
                  ((0U == usWorkerSession) &&
                   (RECORDER_OPUS_UPLOAD_ERROR == tEntry.eEvent))))
        {
            usWorkerSession = tEntry.usSession;
            Local_SendEnd(&tEntry, bTransportFailed);
            usWorkerSession = 0U;
            bTransportFailed = false;
        }
    }
}

/***************************
 * Local_RecorderUploadCallback: copy recorder events without waiting for BLE.
 * Parameters mirror RECORDER_OPUS_UPLOAD_CALLBACK; pContext is unused.
 * Return: zero when accepted or intentionally inactive, nonzero on queue loss.
 ***************************/
static int Local_RecorderUploadCallback(
    RECORDER_OPUS_UPLOAD_EVENT eEvent,
    const uint8_t *pPacket,
    uint16_t usPacketLength,
    uint32_t ulSequence,
    uint32_t ulTimestampMs,
    void *pContext)
{
    AGENTPET_AUDIO_QUEUE_ENTRY tEntry;
    rt_base_t tLevel;
    bool bQueueEvent;
    uint16_t usQueueEntries;
    rt_err_t tResult;

    (void)pContext;
    if (!l_bAudioWorkerReady)
    {
        return 0;
    }

    (void)memset(&tEntry, 0, sizeof(tEntry));
    tEntry.eKind = AGENTPET_AUDIO_QUEUE_RECORDER;
    tEntry.eEvent = eEvent;
    tEntry.ulSequence = ulSequence;
    tEntry.ulTimestampMs = ulTimestampMs;

    tLevel = rt_hw_interrupt_disable();
    bQueueEvent = l_bAudioSubscribed && l_bAudioSessionActive;
    tEntry.usSession = l_usAudioActiveSession;
    usQueueEntries = l_tAudioQueue.entry;
    rt_hw_interrupt_enable(tLevel);

    if (!bQueueEvent)
    {
        return 0;
    }
    if ((RECORDER_OPUS_UPLOAD_PACKET == eEvent) &&
        ((NULL == pPacket) ||
         (0U == usPacketLength) ||
         (RECORDER_OPUS_MAX_PACKET_BYTES < usPacketLength) ||
         ((AGENTPET_AUDIO_QUEUE_DEPTH - AGENTPET_AUDIO_QUEUE_RESERVED) <=
          usQueueEntries)))
    {
        tLevel = rt_hw_interrupt_disable();
        l_ulAudioDroppedPackets++;
        rt_hw_interrupt_enable(tLevel);
        return -RT_EFULL;
    }
    if (RECORDER_OPUS_UPLOAD_PACKET == eEvent)
    {
        tEntry.usPacketLength = usPacketLength;
        (void)memcpy(tEntry.aPacket, pPacket, usPacketLength);
    }
    if ((RECORDER_OPUS_UPLOAD_STOPPED == eEvent) ||
        (RECORDER_OPUS_UPLOAD_ERROR == eEvent))
    {
        tLevel = rt_hw_interrupt_disable();
        tEntry.ulDroppedPackets = l_ulAudioDroppedPackets;
        l_bAudioSessionActive = false;
        l_bAudioStopPending = false;
        rt_hw_interrupt_enable(tLevel);
    }

    tResult = rt_mq_send(&l_tAudioQueue, &tEntry, sizeof(tEntry));
    if (RT_EOK != tResult)
    {
        tLevel = rt_hw_interrupt_disable();
        if (RECORDER_OPUS_UPLOAD_PACKET == eEvent)
        {
            l_ulAudioDroppedPackets++;
        }
        else
        {
            l_bAudioSessionActive = false;
            l_bAudioStopPending = false;
        }
        rt_hw_interrupt_enable(tLevel);
        return (int)tResult;
    }

    return 0;
}

/***************************
 * AGENTPETAUDIO_RequestStream: request desktop-owned microphone control.
 * Parameters: bStart selects start when true and stop when false.
 * Return: true when accepted by the worker; false when unavailable or busy.
 ***************************/
bool AGENTPETAUDIO_RequestStream(bool bStart)
{
    AGENTPET_AUDIO_QUEUE_ENTRY tEntry;
    rt_base_t tLevel;
    bool bCanQueue;
    rt_err_t tResult;

    tLevel = rt_hw_interrupt_disable();
    bCanQueue = l_bAudioWorkerReady && l_bAudioSubscribed &&
        (bStart ? !l_bAudioSessionActive :
         (l_bAudioSessionActive && !l_bAudioStopPending));
    if (bCanQueue && bStart)
    {
        l_usAudioSessionCounter++;
        if (0U == l_usAudioSessionCounter)
        {
            l_usAudioSessionCounter = 1U;
        }
        l_usAudioActiveSession = l_usAudioSessionCounter;
        l_ulAudioDroppedPackets = 0U;
        l_bAudioSessionActive = true;
    }
    else if (bCanQueue)
    {
        l_bAudioStopPending = true;
    }
    rt_hw_interrupt_enable(tLevel);
    if (!bCanQueue)
    {
        return false;
    }

    (void)memset(&tEntry, 0, sizeof(tEntry));
    tEntry.eKind = AGENTPET_AUDIO_QUEUE_CONTROL;
    tEntry.bStartControl = bStart;
    tEntry.usSession = l_usAudioActiveSession;
    tResult = bStart ?
        rt_mq_send(&l_tAudioQueue, &tEntry, sizeof(tEntry)) :
        rt_mq_urgent(&l_tAudioQueue, &tEntry, sizeof(tEntry));
    if (RT_EOK != tResult)
    {
        tLevel = rt_hw_interrupt_disable();
        if (bStart)
        {
            l_bAudioSessionActive = false;
        }
        else
        {
            l_bAudioStopPending = false;
        }
        rt_hw_interrupt_enable(tLevel);
        return false;
    }

    return true;
}

/***************************
 * AGENTPETAUDIO_SetSubscribed: update CCCD state and abort stale sessions.
 * Parameters: bSubscribed is true only while the desktop owns notifications.
 * Return: none.
 ***************************/
void AGENTPETAUDIO_SetSubscribed(bool bSubscribed)
{
    rt_base_t tLevel;
    rt_err_t tResult;
    bool bStopStream;

    if (bSubscribed && l_bAudioWorkerReady)
    {
        tResult = RECORDER_RegisterOpusUploader(
            Local_RecorderUploadCallback,
            NULL);
        if (RT_EOK != tResult)
        {
            LOG_E("Recorder uploader registration failed result=%d", tResult);
            return;
        }
    }

    tLevel = rt_hw_interrupt_disable();
    bStopStream = !bSubscribed && l_bAudioSessionActive;
    l_bAudioSubscribed = bSubscribed;
    if (!bSubscribed)
    {
        l_bAudioSessionActive = false;
        l_usAudioActiveSession = 0U;
        l_ulAudioDroppedPackets = 0U;
        l_bAudioStopPending = false;
    }
    rt_hw_interrupt_enable(tLevel);
    if (!bSubscribed && l_bAudioWorkerReady)
    {
        if (bStopStream)
        {
            (void)RECORDER_StopOpusStream();
        }
        (void)RECORDER_RegisterOpusUploader(NULL, NULL);
        (void)rt_mq_control(&l_tAudioQueue, RT_IPC_CMD_RESET, NULL);
    }

    return;
}

/***************************
 * AGENTPETAUDIO_Init: initialize the static queue/thread and recorder hook.
 * Parameters: none.
 * Return: none; failures leave audio notification disabled and are logged.
 ***************************/
void AGENTPETAUDIO_Init(void)
{
    rt_err_t tResult;

    l_bAudioWorkerReady = false;
    l_bAudioSubscribed = false;
    l_bAudioSessionActive = false;
    l_usAudioSessionCounter = 0U;
    l_usAudioActiveSession = 0U;
    l_bAudioStopPending = false;
    l_ulAudioDroppedPackets = 0U;

    tResult = rt_mq_init(&l_tAudioQueue,
                         "pet_audio_q",
                         l_aAudioQueuePool,
                         sizeof(AGENTPET_AUDIO_QUEUE_ENTRY),
                         sizeof(l_aAudioQueuePool),
                         RT_IPC_FLAG_FIFO);
    if (RT_EOK != tResult)
    {
        LOG_E("Audio upload queue init failed result=%d", tResult);
        return;
    }
    tResult = rt_thread_init(&l_tAudioThread,
                             "pet_audio",
                             Local_AudioWorker,
                             NULL,
                             l_aAudioThreadStack,
                             sizeof(l_aAudioThreadStack),
                             AGENTPET_AUDIO_THREAD_PRIORITY,
                             AGENTPET_AUDIO_THREAD_TIME_SLICE);
    if (RT_EOK != tResult)
    {
        LOG_E("Audio upload thread init failed result=%d", tResult);
        (void)rt_mq_detach(&l_tAudioQueue);
        return;
    }
    tResult = rt_thread_startup(&l_tAudioThread);
    if (RT_EOK != tResult)
    {
        LOG_E("Audio upload thread start failed result=%d", tResult);
        (void)rt_thread_detach(&l_tAudioThread);
        (void)rt_mq_detach(&l_tAudioQueue);
        return;
    }
    l_bAudioWorkerReady = true;
    LOG_I("Audio upload transport ready queue=%u bytes stack=%u bytes",
          (unsigned int)sizeof(l_aAudioQueuePool),
          (unsigned int)sizeof(l_aAudioThreadStack));

    return;
}

#else
bool AGENTPETAUDIO_RequestStream(bool bStart)
{
    (void)bStart;

    return false;
}


void AGENTPETAUDIO_SetSubscribed(bool bSubscribed)
{
    (void)bSubscribed;

    return;
}

void AGENTPETAUDIO_Init(void)
{
    return;
}

#endif /* AGENT_PET_USING_TF_CARD */
