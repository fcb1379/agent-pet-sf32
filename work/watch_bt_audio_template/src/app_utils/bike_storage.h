#ifndef BIKE_STORAGE_H
#define BIKE_STORAGE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool BIKE_STORAGE_Init(void);
bool BIKE_STORAGE_IsTfMounted(void);
const char *BIKE_STORAGE_SelectTrackDirectory(bool bTfMounted);
const char *BIKE_STORAGE_SelectMapRoot(bool bTfMounted);
const char *BIKE_STORAGE_GetTrackDirectory(void);
const char *BIKE_STORAGE_GetMapRoot(void);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_STORAGE_H */
