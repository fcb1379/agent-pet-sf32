#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../work/watch_bt_audio_template/src/app_utils/agent_pet_protocol.h"

#define TEST_ASSERT(tCondition) \
    do \
    { \
        if (!(tCondition)) \
        { \
            (void)printf("FAIL line %d: %s\n", __LINE__, #tCondition); \
            return 1; \
        } \
    } while (0)

static void TEST_FinalizeFrame(uint8_t *pFrame)
{
    pFrame[19] = AGENTPET_Crc8Atm(pFrame, 19U);

    return;
}

static int TEST_CrcVector(void)
{
    static const uint8_t l_aInput[] =
    {
        0x31U, 0x32U, 0x33U, 0x34U, 0x35U,
        0x36U, 0x37U, 0x38U, 0x39U
    };

    TEST_ASSERT(0xF4U == AGENTPET_Crc8Atm(l_aInput, sizeof(l_aInput)));

    return 0;
}

static int TEST_IdleSnapshot(void)
{
    static const uint8_t l_aIdleFrame[AGENTPET_FRAME_SIZE] =
    {
        0x41U, 0x50U, 0x01U, 0x01U, 0x34U, 0x12U, 0x00U, 0x01U,
        0x06U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x18U
    };
    AGENTPET_SNAPSHOT tSnapshot;
    uint32_t ulGeneration;

    AGENTPET_ProtocolInit();
    TEST_ASSERT(
        AGENTPET_RESULT_SNAPSHOT_PUBLISHED ==
        AGENTPET_ProcessFrame(l_aIdleFrame, sizeof(l_aIdleFrame)));
    TEST_ASSERT(AGENTPET_GetSnapshot(&tSnapshot, &ulGeneration));
    TEST_ASSERT(0U == tSnapshot.ucAggregateState);
    TEST_ASSERT(0U == tSnapshot.ucSessionCount);
    TEST_ASSERT(0x1234U == tSnapshot.usSequence);
    TEST_ASSERT(1U == ulGeneration);
    TEST_ASSERT(
        AGENTPET_RESULT_DUPLICATE ==
        AGENTPET_ProcessFrame(l_aIdleFrame, sizeof(l_aIdleFrame)));

    return 0;
}

static int TEST_OutOfOrderSnapshot(void)
{
    uint8_t aFrame0[AGENTPET_FRAME_SIZE] = {0U};
    uint8_t aFrame1[AGENTPET_FRAME_SIZE] = {0U};
    AGENTPET_SNAPSHOT tSnapshot;

    aFrame0[0] = 0x41U;
    aFrame0[1] = 0x50U;
    aFrame0[2] = 1U;
    aFrame0[3] = 1U;
    aFrame0[4] = 2U;
    aFrame0[6] = 0U;
    aFrame0[7] = 2U;
    aFrame0[8] = 10U;
    aFrame0[9] = AGENTPET_STATE_RUNNING;
    aFrame0[10] = 1U;
    aFrame0[11] = 0x78U;
    aFrame0[12] = 0x56U;
    aFrame0[13] = 0x34U;
    aFrame0[14] = 0x12U;
    aFrame0[15] = AGENTPET_STATE_NEEDS_INPUT;
    aFrame0[16] = 1U;
    aFrame0[17] = 2U;
    aFrame0[18] = AGENTPET_TASK_FLAG_APPROVAL | AGENTPET_TASK_FLAG_ACTIVE;
    TEST_FinalizeFrame(aFrame0);

    (void)memcpy(aFrame1, aFrame0, sizeof(aFrame1));
    aFrame1[6] = 1U;
    aFrame1[8] = 6U;
    aFrame1[9] = 0x44U;
    aFrame1[10] = 0x33U;
    aFrame1[11] = 0x22U;
    aFrame1[12] = 0x11U;
    aFrame1[13] = 5U;
    aFrame1[14] = 0U;
    aFrame1[15] = 0U;
    aFrame1[16] = 0U;
    aFrame1[17] = 0U;
    aFrame1[18] = 0U;
    TEST_FinalizeFrame(aFrame1);

    AGENTPET_ProtocolInit();
    TEST_ASSERT(
        AGENTPET_RESULT_FRAME_ACCEPTED ==
        AGENTPET_ProcessFrame(aFrame1, sizeof(aFrame1)));
    TEST_ASSERT(
        AGENTPET_RESULT_SNAPSHOT_PUBLISHED ==
        AGENTPET_ProcessFrame(aFrame0, sizeof(aFrame0)));
    TEST_ASSERT(AGENTPET_GetSnapshot(&tSnapshot, NULL));
    TEST_ASSERT(1U == tSnapshot.ucSessionCount);
    TEST_ASSERT(AGENTPET_STATE_NEEDS_INPUT == tSnapshot.aSessions[0].ucState);
    TEST_ASSERT(0x11223344UL == tSnapshot.aSessions[0].ulTaskHash);
    TEST_ASSERT(5U == tSnapshot.aSessions[0].usAgeSeconds);

    return 0;
}

static int TEST_TimeSync(void)
{
    uint8_t aFrame[AGENTPET_FRAME_SIZE] = {0U};
    const uint32_t ulUtcEpoch = 1785812521UL;
    const int16_t sTimezoneOffsetMinutes = 480;
    AGENTPET_TIME_SYNC tTimeSync;
    uint32_t ulGeneration;

    aFrame[0] = 0x41U;
    aFrame[1] = 0x50U;
    aFrame[2] = 1U;
    aFrame[3] = 3U;
    aFrame[4] = 0x68U;
    aFrame[5] = 0x24U;
    aFrame[6] = 0U;
    aFrame[7] = 1U;
    aFrame[8] = 6U;
    aFrame[9] = (uint8_t)ulUtcEpoch;
    aFrame[10] = (uint8_t)(ulUtcEpoch >> 8U);
    aFrame[11] = (uint8_t)(ulUtcEpoch >> 16U);
    aFrame[12] = (uint8_t)(ulUtcEpoch >> 24U);
    aFrame[13] = (uint8_t)sTimezoneOffsetMinutes;
    aFrame[14] = (uint8_t)((uint16_t)sTimezoneOffsetMinutes >> 8U);
    TEST_FinalizeFrame(aFrame);

    AGENTPET_ProtocolInit();
    TEST_ASSERT(
        AGENTPET_RESULT_TIME_SYNC_PUBLISHED ==
        AGENTPET_ProcessFrame(aFrame, sizeof(aFrame)));
    TEST_ASSERT(AGENTPET_GetTimeSync(&tTimeSync, &ulGeneration));
    TEST_ASSERT(ulUtcEpoch == tTimeSync.ulUtcEpoch);
    TEST_ASSERT(sTimezoneOffsetMinutes == tTimeSync.sTimezoneOffsetMinutes);
    TEST_ASSERT(0x2468U == tTimeSync.usSequence);
    TEST_ASSERT(1U == ulGeneration);

    aFrame[13] = 0x49U;
    aFrame[14] = 0x03U;
    TEST_FinalizeFrame(aFrame);
    TEST_ASSERT(
        AGENTPET_ERROR_TIME_SYNC ==
        AGENTPET_ProcessFrame(aFrame, sizeof(aFrame)));

    return 0;
}

static int TEST_AnimationEvent(void)
{
    uint8_t aFrame[AGENTPET_FRAME_SIZE] = {0U};
    AGENTPET_ANIMATION_EVENT tEvent;
    uint32_t ulGeneration;

    aFrame[0] = 0x41U;
    aFrame[1] = 0x50U;
    aFrame[2] = 1U;
    aFrame[3] = 4U;
    aFrame[4] = 0x34U;
    aFrame[5] = 0x12U;
    aFrame[6] = 0U;
    aFrame[7] = 1U;
    aFrame[8] = 2U;
    aFrame[9] = AGENTPET_ANIMATION_ACTION_PLAY;
    aFrame[10] = 3U;
    TEST_FinalizeFrame(aFrame);

    AGENTPET_ProtocolInit();
    TEST_ASSERT(
        AGENTPET_RESULT_ANIMATION_PUBLISHED ==
        AGENTPET_ProcessFrame(aFrame, sizeof(aFrame)));
    TEST_ASSERT(AGENTPET_GetAnimationEvent(&tEvent, &ulGeneration));
    TEST_ASSERT(AGENTPET_ANIMATION_ACTION_PLAY == tEvent.ucAction);
    TEST_ASSERT(3U == tEvent.ucSlot);
    TEST_ASSERT(0x1234U == tEvent.usSequence);
    TEST_ASSERT(1U == ulGeneration);
    TEST_ASSERT(
        AGENTPET_RESULT_DUPLICATE ==
        AGENTPET_ProcessFrame(aFrame, sizeof(aFrame)));

    aFrame[4]++;
    aFrame[10] = 5U;
    TEST_FinalizeFrame(aFrame);
    TEST_ASSERT(
        AGENTPET_ERROR_ANIMATION ==
        AGENTPET_ProcessFrame(aFrame, sizeof(aFrame)));

    aFrame[4]++;
    aFrame[9] = AGENTPET_ANIMATION_ACTION_TYPING_START;
    aFrame[10] = 0U;
    TEST_FinalizeFrame(aFrame);
    TEST_ASSERT(
        AGENTPET_RESULT_ANIMATION_PUBLISHED ==
        AGENTPET_ProcessFrame(aFrame, sizeof(aFrame)));
    TEST_ASSERT(AGENTPET_GetAnimationEvent(&tEvent, &ulGeneration));
    TEST_ASSERT(AGENTPET_ANIMATION_ACTION_TYPING_START == tEvent.ucAction);
    TEST_ASSERT(0U == tEvent.ucSlot);
    TEST_ASSERT(2U == ulGeneration);

    aFrame[4]++;
    aFrame[9] = AGENTPET_ANIMATION_ACTION_TYPING_STOP;
    TEST_FinalizeFrame(aFrame);
    TEST_ASSERT(
        AGENTPET_RESULT_ANIMATION_PUBLISHED ==
        AGENTPET_ProcessFrame(aFrame, sizeof(aFrame)));
    TEST_ASSERT(AGENTPET_GetAnimationEvent(&tEvent, &ulGeneration));
    TEST_ASSERT(AGENTPET_ANIMATION_ACTION_TYPING_STOP == tEvent.ucAction);
    TEST_ASSERT(3U == ulGeneration);

    return 0;
}

static int TEST_RejectInvalidFrame(void)
{
    uint8_t aFrame[AGENTPET_FRAME_SIZE] =
    {
        0x41U, 0x50U, 0x01U, 0x01U, 0x34U, 0x12U, 0x00U, 0x01U,
        0x06U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x18U
    };

    AGENTPET_ProtocolInit();
    aFrame[19] ^= 0x01U;
    TEST_ASSERT(
        AGENTPET_ERROR_CRC ==
        AGENTPET_ProcessFrame(aFrame, sizeof(aFrame)));
    TEST_ASSERT(!AGENTPET_GetSnapshot(&(AGENTPET_SNAPSHOT){0}, NULL));

    return 0;
}

int main(void)
{
    int lRetVal;

    lRetVal = TEST_CrcVector();
    if (0 != lRetVal)
    {
        return lRetVal;
    }
    lRetVal = TEST_IdleSnapshot();
    if (0 != lRetVal)
    {
        return lRetVal;
    }
    lRetVal = TEST_OutOfOrderSnapshot();
    if (0 != lRetVal)
    {
        return lRetVal;
    }
    lRetVal = TEST_TimeSync();
    if (0 != lRetVal)
    {
        return lRetVal;
    }
    lRetVal = TEST_AnimationEvent();
    if (0 != lRetVal)
    {
        return lRetVal;
    }
    lRetVal = TEST_RejectInvalidFrame();
    if (0 != lRetVal)
    {
        return lRetVal;
    }

    (void)printf("PASS agent_pet_protocol_host_test\n");

    return 0;
}
