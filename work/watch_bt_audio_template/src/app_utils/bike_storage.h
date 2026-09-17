#ifndef BIKE_STORAGE_H
#define BIKE_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_STORAGE_MAP_DIRECTORY_MAX (32U)
#define BIKE_STORAGE_MAP_ROOT_MAX (40U)

bool BIKE_STORAGE_Init(void);
bool BIKE_STORAGE_IsTfMounted(void);
const char *BIKE_STORAGE_SelectTrackDirectory(bool bTfMounted);
const char *BIKE_STORAGE_SelectMapRoot(bool bTfMounted);
const char *BIKE_STORAGE_GetTrackDirectory(void);
const char *BIKE_STORAGE_GetMapRoot(void);
bool BIKE_STORAGE_IsMapDirectoryValid(const char *pDirectory);
bool BIKE_STORAGE_FormatMapRoot(bool bTfMounted, const char *pDirectory,
                                char *pMapRoot, size_t ulMapRootSize);
bool BIKE_STORAGE_FindMapZoomRange(const char *pMapRoot,
                                   uint8_t *pMinimumZoom,
                                   uint8_t *pMaximumZoom);
bool BIKE_STORAGE_GetMapZoomRange(uint8_t *pMinimumZoom,
                                  uint8_t *pMaximumZoom);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_STORAGE_H */
