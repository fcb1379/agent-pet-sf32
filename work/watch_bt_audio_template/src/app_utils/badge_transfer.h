#ifndef BADGE_TRANSFER_H
#define BADGE_TRANSFER_H

#include <stdint.h>

#define BADGE_IMAGE_PATH "/badge.jpg"

typedef enum
{
    BADGE_TRANSFER_IDLE = 0,
    BADGE_TRANSFER_RECEIVING,
    BADGE_TRANSFER_READY,
    BADGE_TRANSFER_ERROR,
} badge_transfer_state_t;

typedef struct
{
    badge_transfer_state_t state;
    uint32_t received;
    uint32_t total;
    uint32_t generation;
    uint32_t last_activity_tick;
    int16_t last_error;
    uint8_t image_available;
} badge_transfer_snapshot_t;

void badge_transfer_get_snapshot(badge_transfer_snapshot_t *snapshot);
int badge_transfer_clear(void);
int badge_transfer_cancel(void);

#endif /* BADGE_TRANSFER_H */
