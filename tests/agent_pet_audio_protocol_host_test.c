#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../work/watch_bt_audio_template/src/app_utils/agent_pet_audio_protocol.h"

static void Test_CrcAndFrameLayout(void)
{
    static const uint8_t aCheck[] = "123456789";
    uint8_t aPayload[AGENTPET_AUDIO_START_PAYLOAD_SIZE];
    uint8_t aFrame[AGENTPET_AUDIO_FRAME_MAX_SIZE];
    uint16_t usLength;

    (void)memset(aPayload, 0x5AU, sizeof(aPayload));
    assert(0xF4U == AGENTPETAUDIOPROTO_Crc8Atm(
        aCheck,
        (uint16_t)(sizeof(aCheck) - 1U)));

    usLength = AGENTPETAUDIOPROTO_BuildFrame(
        AGENTPET_AUDIO_FRAME_START,
        0x1234U,
        0x00563412UL,
        0U,
        1U,
        aPayload,
        sizeof(aPayload),
        aFrame,
        sizeof(aFrame));
    assert((AGENTPET_AUDIO_FRAME_OVERHEAD + sizeof(aPayload)) == usLength);
    assert(AGENTPET_AUDIO_MAGIC_FIRST == aFrame[0]);
    assert(AGENTPET_AUDIO_MAGIC_SECOND == aFrame[1]);
    assert(AGENTPET_AUDIO_PROTOCOL_VERSION == aFrame[2]);
    assert(AGENTPET_AUDIO_FRAME_START == aFrame[3]);
    assert(0x34U == aFrame[4]);
    assert(0x12U == aFrame[5]);
    assert(0x12U == aFrame[6]);
    assert(0x34U == aFrame[7]);
    assert(0x56U == aFrame[8]);
    assert(0U == aFrame[9]);
    assert(1U == aFrame[10]);
    assert(sizeof(aPayload) == aFrame[11]);
    assert(0 == memcmp(&aFrame[12], aPayload, sizeof(aPayload)));
    assert(aFrame[usLength - 1U] ==
        AGENTPETAUDIOPROTO_Crc8Atm(aFrame, usLength - 1U));

    return;
}

static void Test_AudioControl(void)
{
    uint8_t aFrame[AGENTPET_AUDIO_CONTROL_FRAME_SIZE] = {
        AGENTPET_AUDIO_CONTROL_MAGIC_FIRST,
        AGENTPET_AUDIO_CONTROL_MAGIC_SECOND,
        AGENTPET_AUDIO_CONTROL_VERSION,
        AGENTPET_AUDIO_CONTROL_START,
        0U
    };
    AGENTPET_AUDIO_CONTROL_COMMAND eCommand;

    aFrame[4] = AGENTPETAUDIOPROTO_Crc8Atm(aFrame, 4U);
    assert(AGENTPETAUDIOPROTO_ParseControl(
        aFrame,
        sizeof(aFrame),
        &eCommand));
    assert(AGENTPET_AUDIO_CONTROL_START == eCommand);

    aFrame[3] = AGENTPET_AUDIO_CONTROL_STOP;
    aFrame[4] = AGENTPETAUDIOPROTO_Crc8Atm(aFrame, 4U);
    assert(AGENTPETAUDIOPROTO_ParseControl(
        aFrame,
        sizeof(aFrame),
        &eCommand));
    assert(AGENTPET_AUDIO_CONTROL_STOP == eCommand);

    aFrame[4] ^= 0x01U;
    assert(!AGENTPETAUDIOPROTO_ParseControl(
        aFrame,
        sizeof(aFrame),
        &eCommand));
    assert(!AGENTPETAUDIOPROTO_ParseControl(
        aFrame,
        sizeof(aFrame) - 1U,
        &eCommand));
}

static void Test_InvalidBounds(void)
{
    uint8_t aPayload[4] = {1U, 2U, 3U, 4U};
    uint8_t aFrame[AGENTPET_AUDIO_FRAME_MAX_SIZE];

    assert(0U == AGENTPETAUDIOPROTO_BuildFrame(
        AGENTPET_AUDIO_FRAME_DATA,
        0U,
        0U,
        0U,
        1U,
        aPayload,
        sizeof(aPayload),
        aFrame,
        sizeof(aFrame)));
    assert(0U == AGENTPETAUDIOPROTO_BuildFrame(
        AGENTPET_AUDIO_FRAME_DATA,
        1U,
        0U,
        1U,
        1U,
        aPayload,
        sizeof(aPayload),
        aFrame,
        sizeof(aFrame)));
    assert(0U == AGENTPETAUDIOPROTO_BuildFrame(
        AGENTPET_AUDIO_FRAME_DATA,
        1U,
        AGENTPET_AUDIO_SEQUENCE_MAX + 1U,
        0U,
        1U,
        aPayload,
        sizeof(aPayload),
        aFrame,
        sizeof(aFrame)));
    assert(0U == AGENTPETAUDIOPROTO_BuildFrame(
        AGENTPET_AUDIO_FRAME_DATA,
        1U,
        0U,
        0U,
        1U,
        aPayload,
        sizeof(aPayload),
        aFrame,
        AGENTPET_AUDIO_FRAME_OVERHEAD));

    return;
}

int main(void)
{
    Test_CrcAndFrameLayout();
    Test_InvalidBounds();
    Test_AudioControl();
    (void)printf("agent_pet_audio_protocol_host_test: PASS\n");

    return 0;
}
