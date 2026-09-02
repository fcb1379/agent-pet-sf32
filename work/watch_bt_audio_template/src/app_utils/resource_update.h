#ifndef RESOURCE_UPDATE_H
#define RESOURCE_UPDATE_H

#include <stddef.h>
#include <stdint.h>

#include <rtthread.h>
#include "bf0_sibles_watchface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RESOURCE_UPDATE_VERSION_MAX_LENGTH (15U)
#define RESOURCE_UPDATE_MAX_PAYLOAD_BYTES (512U * 1024U)

/* Start a resource transaction after verifying the installed base version. */
int RESUPDATE_Begin(const char *pBaseVersion, const char *pTargetVersion);

/* Cancel a prepared or active resource transaction and delete staged files. */
int RESUPDATE_Cancel(void);

/* Format the persistent version and current transaction state for HWS1. */
int RESUPDATE_Status(char *pResult, size_t ulResultSize);

/* Return non-zero only after a valid HWS1 RESOURCE BEGIN request. */
uint8_t RESUPDATE_IsPrepared(void);

/* Consume a customized SiFli watchface transport event and send its response. */
watchface_event_ack_t RESUPDATE_HandleWatchfaceEvent(uint16_t usEvent,
                                                     uint16_t usLength,
                                                     void *pParameter);

#ifdef __cplusplus
}
#endif

#endif /* RESOURCE_UPDATE_H */
