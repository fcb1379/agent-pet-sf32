#include "bike_storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef BIKE_STORAGE_HOST_BUILD
#include <dfs_fs.h>
#include <dfs_posix.h>
#include <rtdevice.h>
#include <rtthread.h>

#ifdef RT_USING_SPI_MSD
extern int rt_spi_msd_init(void);
#endif

#define LOG_TAG "bike.storage"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>
#else
#include <dirent.h>
#include <sys/statvfs.h>
#endif

#define BIKE_STORAGE_TF_DEVICE "sd0"
#define BIKE_STORAGE_TF_MOUNT_POINT "/sd"
#define BIKE_STORAGE_INTERNAL_TRACK_DIRECTORY "/tracks"
#define BIKE_STORAGE_TF_TRACK_DIRECTORY "/sd/tracks"
#define BIKE_STORAGE_INTERNAL_MAP_ROOT "/MAP"
#define BIKE_STORAGE_TF_MAP_ROOT "/sd/MAP"
#define BIKE_STORAGE_MAP_PATH_MAX (128U)
#define BIKE_STORAGE_MAP_ZOOM_MAX (19U)

/* l_bBikeStorageInitialized: 存储选择已完成标志，只在系统初始化阶段写入。 */
static bool l_bBikeStorageInitialized;

/* l_bBikeTfMounted: TF 卡已挂载到独立 /sd 路径的状态标志。 */
static bool l_bBikeTfMounted;

/* l_tBikeStorageInfo: 当前介质的容量快照，目标板上只在互斥锁
 * 保护下读写；数值范围为 0~UINT64_MAX bytes。
 */
static BIKE_STORAGE_INFO l_tBikeStorageInfo;

#ifndef BIKE_STORAGE_HOST_BUILD
/* l_tBikeStorageMutex: 保护记录线程更新与 UI 读取的容量快照。 */
static struct rt_mutex l_tBikeStorageMutex;

/* l_bBikeStorageMutexReady: 容量快照互斥锁可用标志。 */
static bool l_bBikeStorageMutexReady;
#endif

/* BikeStorage_ParseMapZoom: 解析地图根目录下的纯数字缩放目录名。
 * 参数：
 *   - pName: 目录项名称
 *   - pZoom: 解析后的缩放级别，范围 0~19
 * 返回值：合法缩放目录名返回 true，否则返回 false
 */
static bool BikeStorage_ParseMapZoom(const char *pName, uint8_t *pZoom)
{
    uint32_t ulZoom;

    if ((NULL == pName) || (NULL == pZoom) || ('\0' == pName[0]))
    {
        return false;
    }
    ulZoom = 0U;
    while ('\0' != *pName)
    {
        if (('0' > *pName) || ('9' < *pName))
        {
            return false;
        }
        ulZoom = (ulZoom * 10U) + (uint32_t)(*pName - '0');
        if (BIKE_STORAGE_MAP_ZOOM_MAX < ulZoom)
        {
            return false;
        }
        pName++;
    }
    *pZoom = (uint8_t)ulZoom;

    return true;
}

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
#ifndef BIKE_STORAGE_HOST_BUILD
    rt_err_t eResult;
#endif

    if (l_bBikeStorageInitialized)
    {
        return true;
    }

    l_bBikeTfMounted = false;
    (void)memset(&l_tBikeStorageInfo, 0, sizeof(l_tBikeStorageInfo));

#ifndef BIKE_STORAGE_HOST_BUILD
    eResult = rt_mutex_init(&l_tBikeStorageMutex, "bike_fs",
                            RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        LOG_E("storage mutex init failed: %d", eResult);
        return false;
    }
    l_bBikeStorageMutexReady = true;
#endif
    l_bBikeStorageInitialized = true;
    (void)BIKE_STORAGE_RetryTfMount();

    return true;
}

/* BIKE_STORAGE_RetryTfMount: 在应用进入地图页前重新探测并挂载 TF 卡。
 * SPI-MSD 启动探测失败后允许在后续插卡时再次初始化；不格式化介质，
 * 已挂载时直接返回，避免重复注册块设备或重复挂载。
 * 返回值：TF 卡已挂载到 /sd 返回 true，否则返回 false
 */
bool BIKE_STORAGE_RetryTfMount(void)
{
    if (!l_bBikeStorageInitialized)
    {
        return false;
    }
    if (l_bBikeTfMounted)
    {
        return true;
    }

#ifndef BIKE_STORAGE_HOST_BUILD
#ifdef RT_USING_SPI_MSD
    if ((NULL == rt_device_find(BIKE_STORAGE_TF_DEVICE)) &&
        (RT_EOK != rt_spi_msd_init()))
    {
        LOG_W("retry %s probe failed; use internal storage",
              BIKE_STORAGE_TF_DEVICE);
    }
#endif /* RT_USING_SPI_MSD */
    if (NULL == rt_device_find(BIKE_STORAGE_TF_DEVICE))
    {
        LOG_W("%s unavailable; use internal storage",
              BIKE_STORAGE_TF_DEVICE);
    }
    else if (!BikeStorage_PrepareMountPoint())
    {
        LOG_W("TF mount point unavailable; use internal storage");
    }
    else if (0 != dfs_mount(BIKE_STORAGE_TF_DEVICE,
                            BIKE_STORAGE_TF_MOUNT_POINT, "elm", 0, NULL))
    {
        LOG_W("mount %s on %s failed: %d; use internal storage",
              BIKE_STORAGE_TF_DEVICE, BIKE_STORAGE_TF_MOUNT_POINT,
              rt_get_errno());
    }
    else
    {
        l_bBikeTfMounted = true;
        LOG_I("mounted %s on %s", BIKE_STORAGE_TF_DEVICE,
              BIKE_STORAGE_TF_MOUNT_POINT);
    }
#endif

    (void)BIKE_STORAGE_RefreshInfo();

    return l_bBikeTfMounted;
}

/* BIKE_STORAGE_IsTfMounted: 查询 TF 卡是否已成功挂载。
 * 返回值：TF 卡可用返回 true，否则返回 false
 */
bool BIKE_STORAGE_IsTfMounted(void)
{
    return l_bBikeTfMounted;
}

/* BIKE_STORAGE_RefreshInfo: 从当前选中介质更新文件系统容量快照。
 * 目标板调用可能发生文件系统 IO，不应从 ISR 或 LVGL 回调调用。
 * 返回值：容量查询成功返回 true，否则返回 false
 */
bool BIKE_STORAGE_RefreshInfo(void)
{
    BIKE_STORAGE_INFO tInfo;
    uint64_t udBlockSize;
    uint64_t udBlockCount;
    uint64_t udFreeBlockCount;
    bool bResult;
#ifndef BIKE_STORAGE_HOST_BUILD
    struct statfs tFileSystem;
    rt_err_t eResult;
#else
    struct statvfs tFileSystem;
#endif

    if (!l_bBikeStorageInitialized)
    {
        return false;
    }
    (void)memset(&tInfo, 0, sizeof(tInfo));
    (void)memset(&tFileSystem, 0, sizeof(tFileSystem));
    tInfo.eMedium = l_bBikeTfMounted ? BIKE_STORAGE_MEDIUM_TF :
                    BIKE_STORAGE_MEDIUM_INTERNAL;
#ifndef BIKE_STORAGE_HOST_BUILD
    bResult = (0 == dfs_statfs(l_bBikeTfMounted ?
                               BIKE_STORAGE_TF_MOUNT_POINT : "/",
                               &tFileSystem));
    udBlockSize = (uint64_t)tFileSystem.f_bsize;
    udBlockCount = (uint64_t)tFileSystem.f_blocks;
    udFreeBlockCount = (uint64_t)tFileSystem.f_bfree;
#else
    bResult = (0 == statvfs(".", &tFileSystem));
    udBlockSize = (uint64_t)tFileSystem.f_frsize;
    udBlockCount = (uint64_t)tFileSystem.f_blocks;
    udFreeBlockCount = (uint64_t)tFileSystem.f_bavail;
#endif
    if (bResult)
    {
        tInfo.bAvailable = true;
        tInfo.udTotalBytes = udBlockSize * udBlockCount;
        tInfo.udFreeBytes = udBlockSize * udFreeBlockCount;
    }

#ifndef BIKE_STORAGE_HOST_BUILD
    if (!l_bBikeStorageMutexReady)
    {
        return false;
    }
    eResult = rt_mutex_take(&l_tBikeStorageMutex, RT_WAITING_FOREVER);
    if (RT_EOK != eResult)
    {
        LOG_E("storage mutex take failed: %d", eResult);
        return false;
    }
#endif
    tInfo.ulQueryErrorCount = l_tBikeStorageInfo.ulQueryErrorCount;
    if ((!bResult) && (UINT32_MAX != tInfo.ulQueryErrorCount))
    {
        tInfo.ulQueryErrorCount++;
    }
    l_tBikeStorageInfo = tInfo;
#ifndef BIKE_STORAGE_HOST_BUILD
    eResult = rt_mutex_release(&l_tBikeStorageMutex);
    if (RT_EOK != eResult)
    {
        LOG_E("storage mutex release failed: %d", eResult);
        return false;
    }
#endif

    return bResult;
}

/* BIKE_STORAGE_GetInfo: 读取当前介质的线程安全容量快照。
 * 参数：
 *   - pInfo: 输出容量快照
 * 返回值：参数合法且快照已读取返回 true，否则返回 false
 */
bool BIKE_STORAGE_GetInfo(BIKE_STORAGE_INFO *pInfo)
{
#ifndef BIKE_STORAGE_HOST_BUILD
    rt_err_t eResult;
#endif

    if ((NULL == pInfo) || (!l_bBikeStorageInitialized))
    {
        return false;
    }
#ifndef BIKE_STORAGE_HOST_BUILD
    if (!l_bBikeStorageMutexReady)
    {
        return false;
    }
    eResult = rt_mutex_take(&l_tBikeStorageMutex, RT_WAITING_FOREVER);
    if (RT_EOK != eResult)
    {
        return false;
    }
#endif
    *pInfo = l_tBikeStorageInfo;
#ifndef BIKE_STORAGE_HOST_BUILD
    eResult = rt_mutex_release(&l_tBikeStorageMutex);
    if (RT_EOK != eResult)
    {
        return false;
    }
#endif

    return true;
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

/* BIKE_STORAGE_IsMapDirectoryValid: 校验当前介质内的地图逻辑绝对路径。
 * 路径必须以单个斜杠开头，支持多级 ASCII 字母、数字、下划线和连字符，
 * 禁止根目录、尾随/连续斜杠、点号和路径穿越。
 * 参数：
 *   - pDirectory: 待校验的逻辑路径
 * 返回值：路径可安全拼接到当前介质挂载点时返回 true
 */
bool BIKE_STORAGE_IsMapDirectoryValid(const char *pDirectory)
{
    size_t ulIndex;
    bool bComponentHasCharacter;
    char cValue;

    if ((NULL == pDirectory) || ('/' != pDirectory[0]))
    {
        return false;
    }
    bComponentHasCharacter = false;
    for (ulIndex = 1U; ulIndex < BIKE_STORAGE_MAP_DIRECTORY_MAX; ulIndex++)
    {
        cValue = pDirectory[ulIndex];
        if ('\0' == cValue)
        {
            return bComponentHasCharacter;
        }
        if ('/' == cValue)
        {
            if (!bComponentHasCharacter)
            {
                return false;
            }
            bComponentHasCharacter = false;
        }
        else if ((('a' <= cValue) && ('z' >= cValue)) ||
                 (('A' <= cValue) && ('Z' >= cValue)) ||
                 (('0' <= cValue) && ('9' >= cValue)) ||
                 ('_' == cValue) || ('-' == cValue))
        {
            bComponentHasCharacter = true;
        }
        else
        {
            return false;
        }
    }

    return false;
}

/* BIKE_STORAGE_FormatMapRoot: 将地图逻辑路径解析为当前介质实际路径。
 * 参数：
 *   - bTfMounted: TF 卡是否挂载到 /sd
 *   - pDirectory: 当前介质内的逻辑绝对路径
 *   - pMapRoot: 输出实际地图根目录
 *   - ulMapRootSize: 输出缓冲容量
 * 返回值：路径合法且完整写入返回 true，否则返回 false
 */
bool BIKE_STORAGE_FormatMapRoot(bool bTfMounted, const char *pDirectory,
                                char *pMapRoot, size_t ulMapRootSize)
{
    int lWrittenLength;

    if ((NULL == pMapRoot) || (0U == ulMapRootSize))
    {
        return false;
    }
    pMapRoot[0] = '\0';
    if (!BIKE_STORAGE_IsMapDirectoryValid(pDirectory))
    {
        return false;
    }
    lWrittenLength = snprintf(pMapRoot, ulMapRootSize, "%s%s",
                              bTfMounted ? BIKE_STORAGE_TF_MOUNT_POINT : "",
                              pDirectory);
    if ((0 > lWrittenLength) ||
        (ulMapRootSize <= (size_t)lWrittenLength))
    {
        pMapRoot[0] = '\0';
        return false;
    }

    return true;
}

/* BIKE_STORAGE_FindMapZoomRange: 扫描地图根目录下实际存在的缩放目录。
 * 参数：
 *   - pMapRoot: 地图根目录
 *   - pMinimumZoom/pMaximumZoom: 检出的最小和最大缩放级别
 * 返回值：至少发现一个 0~19 级纯数字目录返回 true，否则返回 false
 */
bool BIKE_STORAGE_FindMapZoomRange(const char *pMapRoot,
                                   uint8_t *pMinimumZoom,
                                   uint8_t *pMaximumZoom)
{
    DIR *pDirectory;
    struct dirent *pEntry;
    struct stat tStatus;
    char aEntryPath[BIKE_STORAGE_MAP_PATH_MAX];
    uint8_t ucMinimumZoom;
    uint8_t ucMaximumZoom;
    uint8_t ucZoom;
    int lLength;
    bool bFound;

    if ((NULL == pMapRoot) || ('\0' == pMapRoot[0]) ||
        (NULL == pMinimumZoom) || (NULL == pMaximumZoom))
    {
        return false;
    }
    pDirectory = opendir(pMapRoot);
    if (NULL == pDirectory)
    {
        return false;
    }
    ucMinimumZoom = BIKE_STORAGE_MAP_ZOOM_MAX;
    ucMaximumZoom = 0U;
    bFound = false;
    pEntry = readdir(pDirectory);
    while (NULL != pEntry)
    {
        if (BikeStorage_ParseMapZoom(pEntry->d_name, &ucZoom))
        {
            lLength = snprintf(aEntryPath, sizeof(aEntryPath), "%s/%s",
                               pMapRoot, pEntry->d_name);
            if ((0 < lLength) && ((size_t)lLength < sizeof(aEntryPath)) &&
                (0 == stat(aEntryPath, &tStatus)) &&
                S_ISDIR(tStatus.st_mode))
            {
                if ((!bFound) || (ucMinimumZoom > ucZoom))
                {
                    ucMinimumZoom = ucZoom;
                }
                if ((!bFound) || (ucMaximumZoom < ucZoom))
                {
                    ucMaximumZoom = ucZoom;
                }
                bFound = true;
            }
        }
        pEntry = readdir(pDirectory);
    }
    (void)closedir(pDirectory);
    if (bFound)
    {
        *pMinimumZoom = ucMinimumZoom;
        *pMaximumZoom = ucMaximumZoom;
    }

    return bFound;
}

/* BIKE_STORAGE_GetMapZoomRange: 扫描当前选中地图介质的缩放范围。
 * 参数：
 *   - pMinimumZoom/pMaximumZoom: 检出的最小和最大缩放级别
 * 返回值：检测到合法缩放目录返回 true，否则返回 false
 */
bool BIKE_STORAGE_GetMapZoomRange(uint8_t *pMinimumZoom,
                                  uint8_t *pMaximumZoom)
{
    return BIKE_STORAGE_FindMapZoomRange(BIKE_STORAGE_GetMapRoot(),
                                         pMinimumZoom, pMaximumZoom);
}
