#ifndef PET_STATE_ASSETS_H
#define PET_STATE_ASSETS_H

#include <stdint.h>

#include "pet_behavior.h"

#define PET_STATE_ASSET_MANIFEST_VERSION (1U)

/* PET_STATE_ASSET: read-only mapping from one visual state to an LVGL path. */
typedef struct _PET_STATE_ASSET
{
    PET_BEHAVIOR_VISUAL_STATE eState;
    const char *pPath;
} PET_STATE_ASSET;

const PET_STATE_ASSET *PETSTATEASSET_Get(PET_BEHAVIOR_VISUAL_STATE eState);
uint8_t PETSTATEASSET_Count(void);

#endif /* PET_STATE_ASSETS_H */
