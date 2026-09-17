#ifndef BIKE_STORAGE_H
#define BIKE_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool BIKE_STORAGE_Init(void);
bool BIKE_STORAGE_IsTfMounted(void);
const char *BIKE_STORAGE_SelectTrackDirectory(bool bTfMounted);
const char *BIKE_STORAGE_SelectMapRoot(bool bTfMounted);
const char *BIKE_STORAGE_GetTrackDirectory(void);
const char *BIKE_STORAGE_GetMapRoot(void);
bool BIKE_STORAGE_FindMapZoomRange(const char *pMapRoot,
                                   uint8_t *pMinimumZoom,
                                   uint8_t *pMaximumZoom);
bool BIKE_STORAGE_GetMapZoomRange(uint8_t *pMinimumZoom,
                                  uint8_t *pMaximumZoom);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_STORAGE_H */
