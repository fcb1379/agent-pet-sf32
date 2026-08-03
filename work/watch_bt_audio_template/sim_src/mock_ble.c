/* PC simulator-only mock of agent_pet_ble_service.c. The hardware firmware
 * uses the real implementation; this file is never compiled into firmware. */
#include <string.h>

#include "agent_pet_protocol.h"
#include "agent_pet_ble_service.h"

static AGENTPET_BLE_STATUS s_status;

void AGENTPETBLE_Init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.bConnected = true;
    s_status.bHasSnapshot = true;
    s_status.ulGeneration = 1U;

    /* Demo snapshot: one active Claude Code session in Running state.
     * Edit this block to preview other pet UI states:
     *   idle(0) / running(1) / needs_input(2) / completed(3) / error(4) */
    s_status.tSnapshot.ucAggregateState = AGENTPET_STATE_RUNNING;
    s_status.tSnapshot.ucSessionCount = 1U;
    s_status.tSnapshot.aSessions[0].ucState = AGENTPET_STATE_RUNNING;
    s_status.tSnapshot.aSessions[0].ucProvider = 2U; /* Claude Code */
    s_status.tSnapshot.aSessions[0].ucSource = 1U;  /* Windows */
    s_status.tSnapshot.aSessions[0].ucFlags = AGENTPET_TASK_FLAG_ACTIVE;
    s_status.tSnapshot.aSessions[0].ulTaskHash = 0x1234ABCDUL;
    s_status.tSnapshot.aSessions[0].usAgeSeconds = 5U;
}

bool AGENTPETBLE_RegisterService(void)
{
    return true;
}

void AGENTPETBLE_SetConnected(bool bConnected)
{
    s_status.bConnected = bConnected;
}

bool AGENTPETBLE_GetStatus(AGENTPET_BLE_STATUS *pStatus)
{
    if (NULL == pStatus)
    {
        return false;
    }
    *pStatus = s_status;
    return true;
}
