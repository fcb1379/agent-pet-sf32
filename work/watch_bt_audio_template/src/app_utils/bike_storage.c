#include "bike_storage.h"

#ifndef BIKE_STORAGE_HOST_BUILD
#include <dfs_fs.h>
#include <dfs_posix.h>
#include <rtdevice.h>
#include <rtthread.h>

#define LOG_TAG "bike.storage"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>
#endif

#define BIKE_STORAGE_TF_DEVICE "sd0"
#define BIKE_STORAGE_TF_MOUNT_POINT "/sd"
#define BIKE_STORAGE_INTERNAL_TRACK_DIRECTORY "/tracks"
#define BIKE_STORAGE_TF_TRACK_DIRECTORY "/sd/tracks"
#define BIKE_STORAGE_INTERNAL_MAP_ROOT "/MAP"
#define BIKE_STORAGE_TF_MAP_ROOT "/sd/MAP"

/* l_bBikeStorageInitialized: 存储选择已完成标志，只在系统初始化阶段写入。 */
static bool l_bBikeStorageInitialized;

/* l_bBikeTfMounted: TF 卡已挂载到独立 /sd 路径的状态标志。 */
static bool l_bBikeTfMounted;

#ifndef BIKE_STORAGE_HOST_BUILD
/* BikeStorage_PrepareMountPoint: 在内部文件系统创建 TF 卡挂载点。
 * 返回值：挂载点存在或创建成功返回 true，否则返回 false
 */
static bool BikeStorage_PrepareMountPoint(void)
{
    if (0 == access(BIKE_STORAGE_TF_MOUNT_POINT, 0))
    {
        return true;
    }
    if (0 != mkdir(BIKE_STORAGE_TF_MOUNT_POINT, 0777))
    {
        LOG_W("create %s failed: %d", BIKE_STORAGE_TF_MOUNT_POINT,
              rt_get_errno());
        return false;
    }

    return true;
}
#endif

/* BIKE_STORAGE_Init: 尝试将已有 FAT 文件系统的 TF 卡挂载到 /sd。
 * 未插卡、未格式化或挂载失败时保留内部文件系统作为可用回退，不自动格式化介质。
 * 返回值：存储路径选择完成返回 true
 */
bool BIKE_STORAGE_Init(void)
{
    if (l_bBikeStorageInitialized)
    {
        return true;
    }

    l_bBikeStorageInitialized = true;
    l_bBikeTfMounted = false;

#ifndef BIKE_STORAGE_HOST_BUILD
    if (NULL == rt_device_find(BIKE_STORAGE_TF_DEVICE))
    {
        LOG_W("%s unavailable; use internal storage",
              BIKE_STORAGE_TF_DEVICE);
        return true;
    }
    if (!BikeStorage_PrepareMountPoint())
    {
        LOG_W("TF mount point unavailable; use internal storage");
        return true;
    }
    if (0 != dfs_mount(BIKE_STORAGE_TF_DEVICE,
                       BIKE_STORAGE_TF_MOUNT_POINT, "elm", 0, NULL))
    {
        LOG_W("mount %s on %s failed: %d; use internal storage",
              BIKE_STORAGE_TF_DEVICE, BIKE_STORAGE_TF_MOUNT_POINT,
              rt_get_errno());
        return true;
    }

    l_bBikeTfMounted = true;
    LOG_I("mounted %s on %s", BIKE_STORAGE_TF_DEVICE,
          BIKE_STORAGE_TF_MOUNT_POINT);
#endif

    return true;
}

/* BIKE_STORAGE_IsTfMounted: 查询 TF 卡是否已成功挂载。
 * 返回值：TF 卡可用返回 true，否则返回 false
 */
bool BIKE_STORAGE_IsTfMounted(void)
{
    return l_bBikeTfMounted;
}

/* BIKE_STORAGE_SelectTrackDirectory: 根据介质状态选择轨迹目录。
 * 参数：
 *   - bTfMounted: TF 卡已挂载标志
 * 返回值：静态绝对路径
 */
const char *BIKE_STORAGE_SelectTrackDirectory(bool bTfMounted)
{
    return bTfMounted ? BIKE_STORAGE_TF_TRACK_DIRECTORY :
           BIKE_STORAGE_INTERNAL_TRACK_DIRECTORY;
}

/* BIKE_STORAGE_SelectMapRoot: 根据介质状态选择离线地图根目录。
 * 参数：
 *   - bTfMounted: TF 卡已挂载标志
 * 返回值：静态绝对路径
 */
const char *BIKE_STORAGE_SelectMapRoot(bool bTfMounted)
{
    return bTfMounted ? BIKE_STORAGE_TF_MAP_ROOT :
           BIKE_STORAGE_INTERNAL_MAP_ROOT;
}

/* BIKE_STORAGE_GetTrackDirectory: 获取当前轨迹目录。
 * 返回值：TF 卡优先、内部文件系统回退的静态绝对路径
 */
const char *BIKE_STORAGE_GetTrackDirectory(void)
{
    return BIKE_STORAGE_SelectTrackDirectory(l_bBikeTfMounted);
}

/* BIKE_STORAGE_GetMapRoot: 获取当前离线地图根目录。
 * 返回值：TF 卡优先、内部文件系统回退的静态绝对路径
 */
const char *BIKE_STORAGE_GetMapRoot(void)
{
    return BIKE_STORAGE_SelectMapRoot(l_bBikeTfMounted);
}
