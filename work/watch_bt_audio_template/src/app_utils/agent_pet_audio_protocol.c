#include "agent_pet_audio_protocol.h"

#include <stddef.h>

/***************************
 * AGENTPETAUDIOPROTO_ParseControl: validate one desktop stream command.
 * Parameters:
 *   - pFrame: five-byte control frame input.
 *   - usFrameLength: input length, exactly five bytes.
 *   - pCommand: validated start or stop command output.
 * Return: true for a valid CRC-protected control frame, otherwise false.
 ***************************/
bool AGENTPETAUDIOPROTO_ParseControl(
    const uint8_t *pFrame,
    uint16_t usFrameLength,
    AGENTPET_AUDIO_CONTROL_COMMAND *pCommand)
{
    if ((NULL == pFrame) || (NULL == pCommand) ||
        (AGENTPET_AUDIO_CONTROL_FRAME_SIZE != usFrameLength) ||
        (AGENTPET_AUDIO_CONTROL_MAGIC_FIRST != pFrame[0]) ||
        (AGENTPET_AUDIO_CONTROL_MAGIC_SECOND != pFrame[1]) ||
        (AGENTPET_AUDIO_CONTROL_VERSION != pFrame[2]) ||
        ((AGENTPET_AUDIO_CONTROL_START != pFrame[3]) &&
         (AGENTPET_AUDIO_CONTROL_STOP != pFrame[3])) ||
        (pFrame[4] != AGENTPETAUDIOPROTO_Crc8Atm(pFrame, 4U)))
    {
        return false;
    }

    *pCommand = (AGENTPET_AUDIO_CONTROL_COMMAND)pFrame[3];

    return true;
}

/***************************
 * AGENTPETAUDIOPROTO_Crc8Atm: calculate CRC-8/ATM for one bounded frame.
 * Parameters:
 *   - pData: input buffer; NULL is accepted only when length is zero.
 *   - usLength: input length in bytes.
 * Return: CRC value, or zero for an invalid NULL input.
 ***************************/
uint8_t AGENTPETAUDIOPROTO_Crc8Atm(const uint8_t *pData,
                                   uint16_t usLength)
{
    uint16_t usIndex;
    uint8_t ucBit;
    uint8_t ucCrc;

    if ((NULL == pData) && (0U < usLength))
    {
        return 0U;
    }

    ucCrc = 0U;
    for (usIndex = 0U; usIndex < usLength; usIndex++)
    {
        ucCrc ^= pData[usIndex];
        for (ucBit = 0U; ucBit < 8U; ucBit++)
        {
            ucCrc = (0U != (ucCrc & 0x80U)) ?
                (uint8_t)((ucCrc << 1U) ^ 0x07U) :
                (uint8_t)(ucCrc << 1U);
        }
    }

    return ucCrc;
}

/***************************
 * AGENTPETAUDIOPROTO_BuildFrame: serialize one CRC-protected audio frame.
 * Parameters:
 *   - eType: start, data, end, or error.
 *   - usSession: nonzero stream session identifier.
 *   - ulSequence: 24-bit Opus packet sequence or final packet count.
 *   - ucFragmentIndex: zero-based fragment index.
 *   - ucFragmentCount: nonzero total fragments for this packet.
 *   - pPayload: payload input; may be NULL only for a zero-length payload.
 *   - ucPayloadLength: payload length, up to 231 bytes.
 *   - pFrame: caller-owned output buffer.
 *   - usFrameCapacity: output capacity, up to the negotiated ATT value size.
 * Return: serialized frame length, or zero when validation fails.
 ***************************/
uint16_t AGENTPETAUDIOPROTO_BuildFrame(
    AGENTPET_AUDIO_FRAME_TYPE eType,
    uint16_t usSession,
    uint32_t ulSequence,
    uint8_t ucFragmentIndex,
    uint8_t ucFragmentCount,
    const uint8_t *pPayload,
    uint8_t ucPayloadLength,
    uint8_t *pFrame,
    uint16_t usFrameCapacity)
{
    uint16_t usFrameLength;
    uint8_t ucIndex;

    usFrameLength = AGENTPET_AUDIO_FRAME_OVERHEAD +
        (uint16_t)ucPayloadLength;
    if ((AGENTPET_AUDIO_FRAME_START > eType) ||
        (AGENTPET_AUDIO_FRAME_ERROR < eType) ||
        (0U == usSession) ||
        (AGENTPET_AUDIO_SEQUENCE_MAX < ulSequence) ||
        (0U == ucFragmentCount) ||
        (ucFragmentCount <= ucFragmentIndex) ||
        (AGENTPET_AUDIO_PAYLOAD_MAX_SIZE < ucPayloadLength) ||
        ((0U < ucPayloadLength) && (NULL == pPayload)) ||
        (NULL == pFrame) ||
        (usFrameCapacity < usFrameLength))
    {
        return 0U;
    }

    pFrame[0] = AGENTPET_AUDIO_MAGIC_FIRST;
    pFrame[1] = AGENTPET_AUDIO_MAGIC_SECOND;
    pFrame[2] = AGENTPET_AUDIO_PROTOCOL_VERSION;
    pFrame[3] = (uint8_t)eType;
    pFrame[4] = (uint8_t)(usSession & 0xFFU);
    pFrame[5] = (uint8_t)((usSession >> 8U) & 0xFFU);
    pFrame[6] = (uint8_t)(ulSequence & 0xFFU);
    pFrame[7] = (uint8_t)((ulSequence >> 8U) & 0xFFU);
    pFrame[8] = (uint8_t)((ulSequence >> 16U) & 0xFFU);
    pFrame[9] = ucFragmentIndex;
    pFrame[10] = ucFragmentCount;
    pFrame[11] = ucPayloadLength;
    for (ucIndex = 0U; ucIndex < ucPayloadLength; ucIndex++)
    {
        pFrame[12U + ucIndex] = pPayload[ucIndex];
    }
    pFrame[usFrameLength - 1U] = AGENTPETAUDIOPROTO_Crc8Atm(
        pFrame,
        usFrameLength - 1U);

    return usFrameLength;
}
