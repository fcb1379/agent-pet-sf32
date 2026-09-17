#include "bike_gpx.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef BIKE_GPX_HOST_BUILD
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#else
#include <dfs.h>
#include <dfs_posix.h>
#endif

#include "bike_ride_model.h"

#define BIKE_GPX_FREE_SPACE_MIN (64ULL * 1024ULL)
#define BIKE_GPX_SYNC_POINT_COUNT (5U)
#define BIKE_GPX_MIN_SEGMENT_MM (1000U)
#define BIKE_GPX_MAX_SEGMENT_MM (100000U)
#define BIKE_GPX_LINE_MAX (256U)
#define BIKE_GPX_FILE_TRY_MAX (100U)

/* l_aBikeGpxHeader: 标准 GPX 1.1 文件头和轨迹段起始标记。 */
static const char l_aBikeGpxHeader[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<gpx version=\"1.1\" creator=\"SF32 X-TRACK Port\" "
    "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
    "<trk><name>SF32 Ride</name><trkseg>\n";

/* l_aBikeGpxFooter: 正常关闭或恢复时写入的尾标记。 */
static const char l_aBikeGpxFooter[] = "</trkseg></trk></gpx>\n";

/* l_aBikeGpxSegmentBreak: 重定位后断开异常跳点连线。 */
static const char l_aBikeGpxSegmentBreak[] = "</trkseg><trkseg>\n";

/* BikeGpx_SetError: 记录错误并进入错误状态。
 * 参数：
 *   - pWriter: GPX 写入状态
 *   - eError: 错误原因
 * 返回值：固定返回 false，便于错误路径直接返回
 */
static bool BikeGpx_SetError(BIKE_GPX_WRITER *pWriter, BIKE_GPX_ERROR eError)
{
    if (NULL != pWriter)
    {
        if (0 <= pWriter->lFileDescriptor)
        {
            (void)close((int)pWriter->lFileDescriptor);
            pWriter->lFileDescriptor = -1;
        }
        pWriter->eError = eError;
        pWriter->eState = BIKE_GPX_STATE_ERROR;
    }

    return false;
}

/* BikeGpx_WriteAll: 处理短写并写完固定长度数据。
 * 参数：
 *   - lFileDescriptor: 文件描述符
 *   - pData: 输入数据
 *   - ulLength: 输入长度
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeGpx_WriteAll(int32_t lFileDescriptor, const char *pData, size_t ulLength)
{
    size_t ulOffset;
    int lWritten;

    if ((0 > lFileDescriptor) || (NULL == pData))
    {
        return false;
    }

    ulOffset = 0U;
    while (ulOffset < ulLength)
    {
        lWritten = write((int)lFileDescriptor, &pData[ulOffset], ulLength - ulOffset);
        if (0 >= lWritten)
        {
            return false;
        }
        ulOffset += (size_t)lWritten;
    }

    return true;
}

/* BikeGpx_IsUtcValid: 验证 GPX 文件名和时间戳所需 UTC 字段。
 * 参数：
 *   - pGnss: GNSS 数据
 * 返回值：有效返回 true，否则返回 false
 */
static bool BikeGpx_IsUtcValid(const BIKE_GNSS_DATA *pGnss)
{
    return (NULL != pGnss) && pGnss->bFixValid &&
           (2000U <= pGnss->usYear) && (2099U >= pGnss->usYear) &&
           (1U <= pGnss->ucMonth) && (12U >= pGnss->ucMonth) &&
           (1U <= pGnss->ucDay) && (31U >= pGnss->ucDay) &&
           (24U > pGnss->ucHour) && (60U > pGnss->ucMinute) &&
           (60U > pGnss->ucSecond) &&
           (-900000000 <= pGnss->lLatitudeE7) && (900000000 >= pGnss->lLatitudeE7) &&
           (-1800000000 <= pGnss->lLongitudeE7) && (1800000000 >= pGnss->lLongitudeE7);
}

/* BikeGpx_GetUtcKey: 生成可比较的 YYYYMMDDhhmmss UTC 键。
 * 参数：
 *   - pGnss: GNSS 数据
 * 返回值：UTC 排序键
 */
static uint64_t BikeGpx_GetUtcKey(const BIKE_GNSS_DATA *pGnss)
{
    uint64_t udKey;

    udKey = (uint64_t)pGnss->usYear;
    udKey = (udKey * 100ULL) + pGnss->ucMonth;
    udKey = (udKey * 100ULL) + pGnss->ucDay;
    udKey = (udKey * 100ULL) + pGnss->ucHour;
    udKey = (udKey * 100ULL) + pGnss->ucMinute;
    udKey = (udKey * 100ULL) + pGnss->ucSecond;

    return udKey;
}

/* BikeGpx_FormatDecimalE7: 将有符号 1e-7 度坐标格式化为十进制度。
 * 参数：
 *   - lValueE7: 坐标
 *   - pBuffer: 输出缓冲
 *   - ulBufferSize: 缓冲大小
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeGpx_FormatDecimalE7(int32_t lValueE7, char *pBuffer, size_t ulBufferSize)
{
    int64_t dAbsolute;
    int lLength;
    const char *pSign;

    if ((NULL == pBuffer) || (0U == ulBufferSize))
    {
        return false;
    }

    dAbsolute = lValueE7;
    pSign = "";
    if (0LL > dAbsolute)
    {
        pSign = "-";
        dAbsolute = -dAbsolute;
    }

    lLength = snprintf(pBuffer, ulBufferSize, "%s%lld.%07lld", pSign,
                       (long long)(dAbsolute / 10000000LL),
                       (long long)(dAbsolute % 10000000LL));

    return (0 < lLength) && ((size_t)lLength < ulBufferSize);
}

/* BikeGpx_GetFreeBytes: 查询记录目录所在文件系统剩余空间。
 * 参数：
 *   - pPath: 文件系统路径
 *   - pFreeBytes: 输出空闲字节数
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeGpx_GetFreeBytes(const char *pPath, uint64_t *pFreeBytes)
{
#ifdef BIKE_GPX_HOST_BUILD
    struct statvfs tFileSystem;
#else
    struct statfs tFileSystem;
#endif

    if ((NULL == pPath) || (NULL == pFreeBytes))
    {
        return false;
    }

#ifdef BIKE_GPX_HOST_BUILD
    if (0 != statvfs(pPath, &tFileSystem))
    {
        return false;
    }
    *pFreeBytes = (uint64_t)tFileSystem.f_bsize * (uint64_t)tFileSystem.f_bavail;
#else
    if (0 != statfs(pPath, &tFileSystem))
    {
        return false;
    }
    *pFreeBytes = (uint64_t)tFileSystem.f_bsize * (uint64_t)tFileSystem.f_bfree;
#endif

    return true;
}

/* BikeGpx_BuildControlPath: 构造恢复标记路径。
 * 参数：
 *   - pDirectory: 记录目录
 *   - pName: 控制文件名
 *   - pPath: 输出路径
 *   - ulPathSize: 输出容量
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeGpx_BuildControlPath(const char *pDirectory, const char *pName,
                                     char *pPath, size_t ulPathSize)
{
    int lLength;

    if ((NULL == pDirectory) || (NULL == pName) || (NULL == pPath))
    {
        return false;
    }

    lLength = snprintf(pPath, ulPathSize, "%s/%s", pDirectory, pName);

    return (0 < lLength) && ((size_t)lLength < ulPathSize);
}

/* BikeGpx_WriteMarker: 原子更新未关闭轨迹标记。
 * 参数：
 *   - pDirectory: 记录目录
 *   - pPartPath: 临时轨迹文件路径
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeGpx_WriteMarker(const char *pDirectory, const char *pPartPath)
{
    char aMarkerPath[BIKE_GPX_PATH_MAX];
    char aTemporaryPath[BIKE_GPX_PATH_MAX];
    int32_t lFileDescriptor;
    bool bResult;

    if ((!BikeGpx_BuildControlPath(pDirectory, ".active", aMarkerPath, sizeof(aMarkerPath))) ||
            (!BikeGpx_BuildControlPath(pDirectory, ".active.tmp", aTemporaryPath,
                                       sizeof(aTemporaryPath))))
    {
        return false;
    }

    lFileDescriptor = (int32_t)open(aTemporaryPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (0 > lFileDescriptor)
    {
        return false;
    }

    bResult = BikeGpx_WriteAll(lFileDescriptor, pPartPath, strlen(pPartPath)) &&
              BikeGpx_WriteAll(lFileDescriptor, "\n", 1U) &&
              (0 == fsync((int)lFileDescriptor));
    if (0 != close((int)lFileDescriptor))
    {
        bResult = false;
    }
    if (!bResult)
    {
        (void)unlink(aTemporaryPath);
        return false;
    }

    if (0 == access(aMarkerPath, 0))
    {
        (void)unlink(aMarkerPath);
    }
    if (0 != rename(aTemporaryPath, aMarkerPath))
    {
        (void)unlink(aTemporaryPath);
        return false;
    }

    return true;
}

/* BikeGpx_RemoveMarker: 删除当前记录会话标记。
 * 参数：
 *   - pDirectory: 记录目录
 * 返回值：成功或标记不存在返回 true
 */
static bool BikeGpx_RemoveMarker(const char *pDirectory)
{
    char aMarkerPath[BIKE_GPX_PATH_MAX];

    if (!BikeGpx_BuildControlPath(pDirectory, ".active", aMarkerPath, sizeof(aMarkerPath)))
    {
        return false;
    }

    return (0 != access(aMarkerPath, 0)) || (0 == unlink(aMarkerPath));
}

/* BikeGpx_GetDirectory: 从最终文件路径提取记录目录。
 * 参数：
 *   - pPath: 文件路径
 *   - pDirectory: 输出目录
 *   - ulDirectorySize: 输出容量
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeGpx_GetDirectory(const char *pPath, char *pDirectory, size_t ulDirectorySize)
{
    const char *pSlash;
    size_t ulLength;

    if ((NULL == pPath) || (NULL == pDirectory) || (0U == ulDirectorySize))
    {
        return false;
    }

    pSlash = strrchr(pPath, '/');
    if (NULL == pSlash)
    {
        return false;
    }
    ulLength = (size_t)(pSlash - pPath);
    if ((0U == ulLength) || (ulLength >= ulDirectorySize))
    {
        return false;
    }

    (void)memcpy(pDirectory, pPath, ulLength);
    pDirectory[ulLength] = '\0';

    return true;
}

/* BIKE_GPX_Init: 初始化固定容量 GPX 写入器。
 * 参数：
 *   - pWriter: GPX 写入状态
 * 返回值：无
 */
void BIKE_GPX_Init(BIKE_GPX_WRITER *pWriter)
{
    if (NULL != pWriter)
    {
        (void)memset(pWriter, 0, sizeof(*pWriter));
        pWriter->lFileDescriptor = -1;
        pWriter->eState = BIKE_GPX_STATE_IDLE;
    }

    return;
}

/* BIKE_GPX_Start: 创建带恢复标记的 GPX 临时文件并写入首点。
 * 参数：
 *   - pWriter: GPX 写入状态
 *   - pDirectory: 记录目录
 *   - pGnss: 首个有效定位点
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_GPX_Start(BIKE_GPX_WRITER *pWriter, const char *pDirectory,
                    const BIKE_GNSS_DATA *pGnss)
{
    uint64_t udFreeBytes;
    uint32_t ulIndex;
    int lLength;

    if ((NULL == pWriter) || (NULL == pDirectory) || (NULL == pGnss))
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_ARGUMENT);
    }
    if ((!BikeGpx_IsUtcValid(pGnss)) ||
            ((BIKE_GPX_STATE_IDLE != pWriter->eState) &&
             (BIKE_GPX_STATE_COMPLETE != pWriter->eState)))
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_TIME);
    }

    BIKE_GPX_Init(pWriter);
    if ((0 != access(pDirectory, 0)) && (0 != mkdir(pDirectory, 0777)))
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_PATH);
    }
    if ((!BikeGpx_GetFreeBytes(pDirectory, &udFreeBytes)) ||
            (BIKE_GPX_FREE_SPACE_MIN > udFreeBytes))
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_NO_SPACE);
    }

    for (ulIndex = 0U; ulIndex < BIKE_GPX_FILE_TRY_MAX; ulIndex++)
    {
        if (0U == ulIndex)
        {
            lLength = snprintf(pWriter->aFinalPath, sizeof(pWriter->aFinalPath),
                               "%s/TRK_%04u%02u%02u_%02u%02u%02u.gpx", pDirectory,
                               pGnss->usYear, pGnss->ucMonth, pGnss->ucDay,
                               pGnss->ucHour, pGnss->ucMinute, pGnss->ucSecond);
        }
        else
        {
            lLength = snprintf(pWriter->aFinalPath, sizeof(pWriter->aFinalPath),
                               "%s/TRK_%04u%02u%02u_%02u%02u%02u_%02lu.gpx", pDirectory,
                               pGnss->usYear, pGnss->ucMonth, pGnss->ucDay,
                               pGnss->ucHour, pGnss->ucMinute, pGnss->ucSecond,
                               (unsigned long)ulIndex);
        }
        if ((0 >= lLength) || ((size_t)lLength >= sizeof(pWriter->aFinalPath)))
        {
            return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_PATH);
        }
        lLength = snprintf(pWriter->aPartPath, sizeof(pWriter->aPartPath), "%s.part",
                           pWriter->aFinalPath);
        if ((0 >= lLength) || ((size_t)lLength >= sizeof(pWriter->aPartPath)))
        {
            return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_PATH);
        }
        if ((0 != access(pWriter->aFinalPath, 0)) && (0 != access(pWriter->aPartPath, 0)))
        {
            break;
        }
    }
    if (BIKE_GPX_FILE_TRY_MAX == ulIndex)
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_PATH);
    }

    pWriter->lFileDescriptor = (int32_t)open(pWriter->aPartPath,
                                              O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (0 > pWriter->lFileDescriptor)
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_OPEN);
    }
    if ((!BikeGpx_WriteAll(pWriter->lFileDescriptor, l_aBikeGpxHeader,
                           sizeof(l_aBikeGpxHeader) - 1U)) ||
            (0 != fsync((int)pWriter->lFileDescriptor)) ||
            (!BikeGpx_WriteMarker(pDirectory, pWriter->aPartPath)))
    {
        (void)close((int)pWriter->lFileDescriptor);
        pWriter->lFileDescriptor = -1;
        (void)unlink(pWriter->aPartPath);
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_WRITE);
    }

    pWriter->eState = BIKE_GPX_STATE_ACTIVE;
    pWriter->eError = BIKE_GPX_ERROR_NONE;

    return BIKE_GPX_Append(pWriter, pGnss);
}

/* BIKE_GPX_Append: 按 UTC 顺序和跳点阈值流式写入一个轨迹点。
 * 参数：
 *   - pWriter: GPX 写入状态
 *   - pGnss: 定位点
 * 返回值：成功写入或安全跳过返回 true，IO 错误返回 false
 */
bool BIKE_GPX_Append(BIKE_GPX_WRITER *pWriter, const BIKE_GNSS_DATA *pGnss)
{
    char aLatitude[24];
    char aLongitude[24];
    char aLine[BIKE_GPX_LINE_MAX];
    uint64_t udUtcKey;
    uint32_t ulSegmentMm;
    uint32_t ulCandidateSegmentMm;
    int64_t dAltitudeCm;
    int lLength;

    if ((NULL == pWriter) || (NULL == pGnss) ||
            (BIKE_GPX_STATE_ACTIVE != pWriter->eState) || (0 > pWriter->lFileDescriptor))
    {
        return false;
    }
    if (!BikeGpx_IsUtcValid(pGnss))
    {
        return true;
    }

    udUtcKey = BikeGpx_GetUtcKey(pGnss);
    if (pWriter->bHasRecoveryCandidate)
    {
        ulCandidateSegmentMm = BIKE_RIDE_CalculateDistanceMm(
                                   pWriter->lCandidateLatitudeE7,
                                   pWriter->lCandidateLongitudeE7,
                                   pGnss->lLatitudeE7,
                                   pGnss->lLongitudeE7);
        if (BIKE_GPX_MAX_SEGMENT_MM >= ulCandidateSegmentMm)
        {
            if (!BikeGpx_WriteAll(pWriter->lFileDescriptor,
                                  l_aBikeGpxSegmentBreak,
                                  sizeof(l_aBikeGpxSegmentBreak) - 1U))
            {
                return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_WRITE);
            }
            pWriter->bHasPreviousPoint = false;
            pWriter->bHasRecoveryCandidate = false;
        }
        else
        {
            pWriter->lCandidateLatitudeE7 = pGnss->lLatitudeE7;
            pWriter->lCandidateLongitudeE7 = pGnss->lLongitudeE7;
            return true;
        }
    }
    if (pWriter->bHasPreviousPoint)
    {
        if (pWriter->udLastUtcKey >= udUtcKey)
        {
            return true;
        }
        ulSegmentMm = BIKE_RIDE_CalculateDistanceMm(pWriter->lPreviousLatitudeE7,
                                                    pWriter->lPreviousLongitudeE7,
                                                    pGnss->lLatitudeE7,
                                                    pGnss->lLongitudeE7);
        if (BIKE_GPX_MIN_SEGMENT_MM > ulSegmentMm)
        {
            return true;
        }
        if (BIKE_GPX_MAX_SEGMENT_MM < ulSegmentMm)
        {
            pWriter->lCandidateLatitudeE7 = pGnss->lLatitudeE7;
            pWriter->lCandidateLongitudeE7 = pGnss->lLongitudeE7;
            pWriter->bHasRecoveryCandidate = true;
            return true;
        }
    }

    if ((!BikeGpx_FormatDecimalE7(pGnss->lLatitudeE7, aLatitude, sizeof(aLatitude))) ||
            (!BikeGpx_FormatDecimalE7(pGnss->lLongitudeE7, aLongitude, sizeof(aLongitude))))
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_ARGUMENT);
    }

    dAltitudeCm = pGnss->lAltitudeCm;
    lLength = snprintf(aLine, sizeof(aLine),
                       "<trkpt lat=\"%s\" lon=\"%s\"><ele>%s%lld.%02lld</ele>"
                       "<time>%04u-%02u-%02uT%02u:%02u:%02uZ</time></trkpt>\n",
                       aLatitude, aLongitude, (0LL > dAltitudeCm) ? "-" : "",
                       (long long)((0LL > dAltitudeCm ? -dAltitudeCm : dAltitudeCm) / 100LL),
                       (long long)((0LL > dAltitudeCm ? -dAltitudeCm : dAltitudeCm) % 100LL),
                       pGnss->usYear, pGnss->ucMonth, pGnss->ucDay,
                       pGnss->ucHour, pGnss->ucMinute, pGnss->ucSecond);
    if ((0 >= lLength) || ((size_t)lLength >= sizeof(aLine)) ||
            (!BikeGpx_WriteAll(pWriter->lFileDescriptor, aLine, (size_t)lLength)))
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_WRITE);
    }

    pWriter->ulPointCount++;
    pWriter->udLastUtcKey = udUtcKey;
    pWriter->lPreviousLatitudeE7 = pGnss->lLatitudeE7;
    pWriter->lPreviousLongitudeE7 = pGnss->lLongitudeE7;
    pWriter->bHasPreviousPoint = true;
    pWriter->bHasRecoveryCandidate = false;
    if ((0U == (pWriter->ulPointCount % BIKE_GPX_SYNC_POINT_COUNT)) &&
            (0 != fsync((int)pWriter->lFileDescriptor)))
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_SYNC);
    }

    return true;
}

/* BIKE_GPX_Pause: 同步文件并暂停写点。
 * 参数：
 *   - pWriter: GPX 写入状态
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_GPX_Pause(BIKE_GPX_WRITER *pWriter)
{
    if ((NULL == pWriter) || (BIKE_GPX_STATE_ACTIVE != pWriter->eState))
    {
        return false;
    }
    if (0 != fsync((int)pWriter->lFileDescriptor))
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_SYNC);
    }
    pWriter->eState = BIKE_GPX_STATE_PAUSED;

    return true;
}

/* BIKE_GPX_Resume: 从暂停状态继续写点。
 * 参数：
 *   - pWriter: GPX 写入状态
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_GPX_Resume(BIKE_GPX_WRITER *pWriter)
{
    if ((NULL == pWriter) || (BIKE_GPX_STATE_PAUSED != pWriter->eState))
    {
        return false;
    }
    pWriter->eState = BIKE_GPX_STATE_ACTIVE;

    return true;
}

/* BIKE_GPX_Stop: 写入尾标记、同步并原子发布最终 GPX 文件。
 * 参数：
 *   - pWriter: GPX 写入状态
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_GPX_Stop(BIKE_GPX_WRITER *pWriter)
{
    char aDirectory[BIKE_GPX_PATH_MAX];
    bool bResult;

    if ((NULL == pWriter) ||
            ((BIKE_GPX_STATE_ACTIVE != pWriter->eState) &&
             (BIKE_GPX_STATE_PAUSED != pWriter->eState)))
    {
        return false;
    }

    bResult = BikeGpx_WriteAll(pWriter->lFileDescriptor, l_aBikeGpxFooter,
                               sizeof(l_aBikeGpxFooter) - 1U) &&
              (0 == fsync((int)pWriter->lFileDescriptor));
    if (0 != close((int)pWriter->lFileDescriptor))
    {
        bResult = false;
    }
    pWriter->lFileDescriptor = -1;
    if (!bResult)
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_SYNC);
    }
    if (0 != rename(pWriter->aPartPath, pWriter->aFinalPath))
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_RENAME);
    }
    if (!BikeGpx_GetDirectory(pWriter->aFinalPath, aDirectory, sizeof(aDirectory)))
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_RENAME);
    }

    pWriter->eState = BIKE_GPX_STATE_COMPLETE;
    pWriter->eError = BIKE_GPX_ERROR_NONE;

    /* 文件已原子发布后，即使标记删除失败也不能把已保存会话误报为损坏。 */
    (void)BikeGpx_RemoveMarker(aDirectory);

    return true;
}

/* BIKE_GPX_Discard: 关闭并删除当前未发布轨迹及恢复标记。
 * 参数：
 *   - pWriter: GPX 写入状态
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_GPX_Discard(BIKE_GPX_WRITER *pWriter)
{
    char aDirectory[BIKE_GPX_PATH_MAX];
    bool bResult;

    if ((NULL == pWriter) ||
        ((BIKE_GPX_STATE_ACTIVE != pWriter->eState) &&
         (BIKE_GPX_STATE_PAUSED != pWriter->eState) &&
         (BIKE_GPX_STATE_ERROR != pWriter->eState)) ||
        ('\0' == pWriter->aPartPath[0]) ||
        (!BikeGpx_GetDirectory(pWriter->aPartPath, aDirectory,
                               sizeof(aDirectory))))
    {
        return false;
    }

    bResult = true;
    if (0 <= pWriter->lFileDescriptor)
    {
        if (0 != close((int)pWriter->lFileDescriptor))
        {
            bResult = false;
        }
        pWriter->lFileDescriptor = -1;
    }
    if ((0 == access(pWriter->aPartPath, 0)) &&
        (0 != unlink(pWriter->aPartPath)))
    {
        bResult = false;
    }
    if (!BikeGpx_RemoveMarker(aDirectory))
    {
        bResult = false;
    }
    if (!bResult)
    {
        return BikeGpx_SetError(pWriter, BIKE_GPX_ERROR_DISCARD);
    }

    BIKE_GPX_Init(pWriter);

    return true;
}

/* BikeGpx_IsClosingLine: 判断恢复输入是否为旧的 GPX 关闭标记。
 * 参数：
 *   - pLine: 完整文本行
 * 返回值：是关闭标记返回 true
 */
static bool BikeGpx_IsClosingLine(const char *pLine)
{
    return NULL != strstr(pLine, "</gpx>");
}

/* BikeGpx_CopyCompleteLines: 恢复时仅复制完整行并去除旧关闭标记。
 * 参数：
 *   - lInputFd: 输入临时文件
 *   - lOutputFd: 输出恢复文件
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeGpx_CopyCompleteLines(int32_t lInputFd, int32_t lOutputFd)
{
    char aLine[BIKE_GPX_LINE_MAX];
    size_t ulLength;
    char cValue;
    int lReadLength;

    ulLength = 0U;
    while (true)
    {
        lReadLength = read((int)lInputFd, &cValue, 1U);
        if (0 == lReadLength)
        {
            break;
        }
        if (0 > lReadLength)
        {
            return false;
        }
        if (ulLength >= (sizeof(aLine) - 1U))
        {
            return false;
        }
        aLine[ulLength] = cValue;
        ulLength++;
        if ('\n' == cValue)
        {
            aLine[ulLength] = '\0';
            if ((!BikeGpx_IsClosingLine(aLine)) &&
                    (!BikeGpx_WriteAll(lOutputFd, aLine, ulLength)))
            {
                return false;
            }
            ulLength = 0U;
        }
    }

    return true;
}

/* BIKE_GPX_Recover: 将上次断电遗留的 part 文件安全闭合为 GPX。
 * 参数：
 *   - pDirectory: 记录目录
 *   - pRecoveredPath: 可选的已恢复文件路径输出
 *   - ulRecoveredPathSize: 输出容量
 * 返回值：无待恢复、恢复成功或恢复失败
 */
BIKE_GPX_RECOVERY_RESULT BIKE_GPX_Recover(const char *pDirectory,
                                          char *pRecoveredPath,
                                          size_t ulRecoveredPathSize)
{
    char aMarkerPath[BIKE_GPX_PATH_MAX];
    char aPartPath[BIKE_GPX_PATH_MAX];
    char aFinalPath[BIKE_GPX_PATH_MAX];
    char aRecoveryPath[BIKE_GPX_PATH_MAX];
    int32_t lMarkerFd;
    int32_t lInputFd;
    int32_t lOutputFd;
    int lReadLength;
    size_t ulLength;
    bool bResult;

    if ((NULL == pDirectory) ||
            (!BikeGpx_BuildControlPath(pDirectory, ".active", aMarkerPath, sizeof(aMarkerPath))))
    {
        return BIKE_GPX_RECOVERY_ERROR;
    }
    if (0 != access(aMarkerPath, 0))
    {
        return BIKE_GPX_RECOVERY_NONE;
    }

    lMarkerFd = (int32_t)open(aMarkerPath, O_RDONLY, 0);
    if (0 > lMarkerFd)
    {
        return BIKE_GPX_RECOVERY_ERROR;
    }
    lReadLength = read((int)lMarkerFd, aPartPath, sizeof(aPartPath) - 1U);
    bResult = (0 == close((int)lMarkerFd));
    if ((0 >= lReadLength) || (!bResult))
    {
        return BIKE_GPX_RECOVERY_ERROR;
    }
    aPartPath[lReadLength] = '\0';
    ulLength = strcspn(aPartPath, "\r\n");
    aPartPath[ulLength] = '\0';
    if ((0U == ulLength) || (0U != strncmp(aPartPath, pDirectory, strlen(pDirectory))) ||
            ('/' != aPartPath[strlen(pDirectory)]) ||
            (NULL != strstr(aPartPath, "..")) || (ulLength <= 5U) ||
            (0 != strcmp(&aPartPath[ulLength - 5U], ".part")))
    {
        return BIKE_GPX_RECOVERY_ERROR;
    }

    if ((ulLength - 5U) >= sizeof(aFinalPath))
    {
        return BIKE_GPX_RECOVERY_ERROR;
    }
    (void)memcpy(aFinalPath, aPartPath, ulLength - 5U);
    aFinalPath[ulLength - 5U] = '\0';
    if ((0 != access(aPartPath, 0)) && (0 == access(aFinalPath, 0)))
    {
        (void)BikeGpx_RemoveMarker(pDirectory);
        if ((NULL != pRecoveredPath) && (0U < ulRecoveredPathSize))
        {
            (void)snprintf(pRecoveredPath, ulRecoveredPathSize, "%s", aFinalPath);
        }
        return BIKE_GPX_RECOVERY_DONE;
    }
    lReadLength = snprintf(aRecoveryPath, sizeof(aRecoveryPath), "%s.recover", aFinalPath);
    if ((0 >= lReadLength) || ((size_t)lReadLength >= sizeof(aRecoveryPath)))
    {
        return BIKE_GPX_RECOVERY_ERROR;
    }

    lInputFd = (int32_t)open(aPartPath, O_RDONLY, 0);
    lOutputFd = (int32_t)open(aRecoveryPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if ((0 > lInputFd) || (0 > lOutputFd))
    {
        if (0 <= lInputFd)
        {
            (void)close((int)lInputFd);
        }
        if (0 <= lOutputFd)
        {
            (void)close((int)lOutputFd);
        }
        return BIKE_GPX_RECOVERY_ERROR;
    }

    bResult = BikeGpx_CopyCompleteLines(lInputFd, lOutputFd) &&
              BikeGpx_WriteAll(lOutputFd, l_aBikeGpxFooter, sizeof(l_aBikeGpxFooter) - 1U) &&
              (0 == fsync((int)lOutputFd));
    if ((0 != close((int)lInputFd)) || (0 != close((int)lOutputFd)))
    {
        bResult = false;
    }
    if ((!bResult) || (0 == access(aFinalPath, 0)) ||
            (0 != rename(aRecoveryPath, aFinalPath)) ||
            (0 != unlink(aPartPath)) || (!BikeGpx_RemoveMarker(pDirectory)))
    {
        (void)unlink(aRecoveryPath);
        return BIKE_GPX_RECOVERY_ERROR;
    }

    if ((NULL != pRecoveredPath) && (0U < ulRecoveredPathSize))
    {
        lReadLength = snprintf(pRecoveredPath, ulRecoveredPathSize, "%s", aFinalPath);
        if ((0 >= lReadLength) || ((size_t)lReadLength >= ulRecoveredPathSize))
        {
            pRecoveredPath[0] = '\0';
        }
    }

    return BIKE_GPX_RECOVERY_DONE;
}
