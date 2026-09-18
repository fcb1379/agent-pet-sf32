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

/* BIKE_STORAGE_MEDIUM: 当前轨迹与地图使用的存储介质。 */
typedef enum _BIKE_STORAGE_MEDIUM
{
    BIKE_STORAGE_MEDIUM_INTERNAL = 0,
    BIKE_STORAGE_MEDIUM_TF
} BIKE_STORAGE_MEDIUM;

/* BIKE_STORAGE_INFO: 文件系统容量快照。
 * 成员说明：
 *   - bAvailable: 容量查询是否成功
 *   - eMedium: 当前使用板载文件系统或 TF 卡
 *   - udTotalBytes: 文件系统总容量，单位 bytes
 *   - udFreeBytes: 文件系统可用容量，单位 bytes
 *   - ulQueryErrorCount: 启动后容量查询失败累计次数
 */
typedef struct _BIKE_STORAGE_INFO
{
    bool bAvailable;
    BIKE_STORAGE_MEDIUM eMedium;
    uint64_t udTotalBytes;
    uint64_t udFreeBytes;
    uint32_t ulQueryErrorCount;
} BIKE_STORAGE_INFO;

bool BIKE_STORAGE_Init(void);
bool BIKE_STORAGE_RetryTfMount(void);
bool BIKE_STORAGE_IsTfMounted(void);
bool BIKE_STORAGE_RefreshInfo(void);
bool BIKE_STORAGE_GetInfo(BIKE_STORAGE_INFO *pInfo);
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
