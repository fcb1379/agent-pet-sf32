#ifndef AGENT_PET_AUDIO_PROTOCOL_H
#define AGENT_PET_AUDIO_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define AGENTPET_AUDIO_MAGIC_FIRST          (0x41U)
#define AGENTPET_AUDIO_MAGIC_SECOND         (0x4FU)
#define AGENTPET_AUDIO_PROTOCOL_VERSION     (1U)
#define AGENTPET_AUDIO_FRAME_MAX_SIZE       (244U)
#define AGENTPET_AUDIO_FRAME_OVERHEAD       (13U)
#define AGENTPET_AUDIO_PAYLOAD_MAX_SIZE     (AGENTPET_AUDIO_FRAME_MAX_SIZE - \
                                              AGENTPET_AUDIO_FRAME_OVERHEAD)
#define AGENTPET_AUDIO_SEQUENCE_MAX         (0x00FFFFFFUL)
#define AGENTPET_AUDIO_CONTROL_MAGIC_FIRST  (0x41U)
#define AGENTPET_AUDIO_CONTROL_MAGIC_SECOND (0x43U)
#define AGENTPET_AUDIO_CONTROL_VERSION      (1U)
#define AGENTPET_AUDIO_CONTROL_FRAME_SIZE   (5U)

typedef enum _AGENTPET_AUDIO_FRAME_TYPE
{
    AGENTPET_AUDIO_FRAME_START = 1,
    AGENTPET_AUDIO_FRAME_DATA,
    AGENTPET_AUDIO_FRAME_END,
    AGENTPET_AUDIO_FRAME_ERROR,
} AGENTPET_AUDIO_FRAME_TYPE;

typedef enum _AGENTPET_AUDIO_CONTROL_COMMAND
{
    AGENTPET_AUDIO_CONTROL_START = 1,
    AGENTPET_AUDIO_CONTROL_STOP,
} AGENTPET_AUDIO_CONTROL_COMMAND;

/* AGENTPET_AUDIO_START_PAYLOAD: little-endian metadata sent once per stream.
 * Members:
 *   - ulSampleRateHz: Opus input sample rate, currently 16000 Hz
 *   - ucChannelCount: channel count, currently mono
 *   - usFrameSamples: samples in one encoded packet, currently 320 (20 ms)
 *   - usPreSkip: Opus decoder pre-skip in 48 kHz granule units
 *   - aBitrateBps: target encoder bitrate encoded as unsigned LE24
 */
#define AGENTPET_AUDIO_START_PAYLOAD_SIZE    (12U)
#define AGENTPET_AUDIO_END_PAYLOAD_SIZE      (8U)

uint8_t AGENTPETAUDIOPROTO_Crc8Atm(const uint8_t *pData,
                                   uint16_t usLength);
uint16_t AGENTPETAUDIOPROTO_BuildFrame(
    AGENTPET_AUDIO_FRAME_TYPE eType,
    uint16_t usSession,
    uint32_t ulSequence,
    uint8_t ucFragmentIndex,
    uint8_t ucFragmentCount,
    const uint8_t *pPayload,
    uint8_t ucPayloadLength,
    uint8_t *pFrame,
    uint16_t usFrameCapacity);
bool AGENTPETAUDIOPROTO_ParseControl(
    const uint8_t *pFrame,
    uint16_t usFrameLength,
    AGENTPET_AUDIO_CONTROL_COMMAND *pCommand);

#endif /* AGENT_PET_AUDIO_PROTOCOL_H */
