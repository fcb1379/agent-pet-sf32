#ifndef AGENT_PET_MERIT_H
#define AGENT_PET_MERIT_H

#include <stdbool.h>
#include <stdint.h>

#define AGENTPET_MERIT_FRAME_SIZE (16U)
#define AGENTPET_MERIT_MAX_COUNT  (0x7FFFFFFFUL)

/* AGENTPET_MERIT_SNAPSHOT: coherent daily merit state shared by BLE and LVGL.
 * Members:
 *   - ulDay: local calendar date encoded as YYYYMMDD, or zero before time is valid
 *   - ulCount: daily merit count, range 0..INT32_MAX for preferences compatibility
 *   - ulGeneration: increments whenever the day or count changes
 */
typedef struct _AGENTPET_MERIT_SNAPSHOT
{
    uint32_t ulDay;
    uint32_t ulCount;
    uint32_t ulGeneration;
} AGENTPET_MERIT_SNAPSHOT;

void AGENTPETMERIT_Init(void);
bool AGENTPETMERIT_GetSnapshot(AGENTPET_MERIT_SNAPSHOT *pSnapshot);
bool AGENTPETMERIT_Merge(uint32_t ulDay, uint32_t ulCount);
uint32_t AGENTPETMERIT_Increment(void);
void AGENTPETMERIT_RefreshDay(void);
void AGENTPETMERIT_Save(void);

#endif /* AGENT_PET_MERIT_H */
