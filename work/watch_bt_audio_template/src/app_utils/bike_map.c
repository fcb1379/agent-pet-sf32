#include "bike_map.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define BIKE_MAP_LATITUDE_LIMIT_E7 (850511288)
#define BIKE_MAP_LONGITUDE_LIMIT_E7 (1800000000)
#define BIKE_MAP_DEGREE_E7 (10000000.0)
#define BIKE_MAP_PI (3.14159265358979323846)
#define BIKE_MAP_GCJ_AXIS (6378245.0)
#define BIKE_MAP_GCJ_ECCENTRICITY (0.00669342162296594323)
#define BIKE_MAP_CHINA_LATITUDE_MIN_E7 (8293000)
#define BIKE_MAP_CHINA_LATITUDE_MAX_E7 (558271000)
#define BIKE_MAP_CHINA_LONGITUDE_MIN_E7 (720040000)
#define BIKE_MAP_CHINA_LONGITUDE_MAX_E7 (1378347000)

/* BikeMap_TransformLatitude: 计算 WGS-84 到 GCJ-02 的纬度偏移中间量。
 * 参数：
 *   - dLongitudeOffset/dLatitudeOffset: 相对 105E/35N 的角度偏移
 * 返回值：纬度偏移中间量
 */
static double BikeMap_TransformLatitude(double dLongitudeOffset,
                                        double dLatitudeOffset)
{
    double dResult;

    dResult = -100.0 + (2.0 * dLongitudeOffset) +
              (3.0 * dLatitudeOffset) +
              (0.2 * dLatitudeOffset * dLatitudeOffset) +
              (0.1 * dLongitudeOffset * dLatitudeOffset) +
              (0.2 * sqrt(fabs(dLongitudeOffset)));
    dResult += ((20.0 * sin(6.0 * dLongitudeOffset * BIKE_MAP_PI)) +
                (20.0 * sin(2.0 * dLongitudeOffset * BIKE_MAP_PI))) *
               2.0 / 3.0;
    dResult += ((20.0 * sin(dLatitudeOffset * BIKE_MAP_PI)) +
                (40.0 * sin(dLatitudeOffset * BIKE_MAP_PI / 3.0))) *
               2.0 / 3.0;
    dResult += ((160.0 * sin(dLatitudeOffset * BIKE_MAP_PI / 12.0)) +
                (320.0 * sin(dLatitudeOffset * BIKE_MAP_PI / 30.0))) *
               2.0 / 3.0;

    return dResult;
}

/* BikeMap_TransformLongitude: 计算 WGS-84 到 GCJ-02 的经度偏移中间量。
 * 参数：
 *   - dLongitudeOffset/dLatitudeOffset: 相对 105E/35N 的角度偏移
 * 返回值：经度偏移中间量
 */
static double BikeMap_TransformLongitude(double dLongitudeOffset,
                                         double dLatitudeOffset)
{
    double dResult;

    dResult = 300.0 + dLongitudeOffset + (2.0 * dLatitudeOffset) +
              (0.1 * dLongitudeOffset * dLongitudeOffset) +
              (0.1 * dLongitudeOffset * dLatitudeOffset) +
              (0.1 * sqrt(fabs(dLongitudeOffset)));
    dResult += ((20.0 * sin(6.0 * dLongitudeOffset * BIKE_MAP_PI)) +
                (20.0 * sin(2.0 * dLongitudeOffset * BIKE_MAP_PI))) *
               2.0 / 3.0;
    dResult += ((20.0 * sin(dLongitudeOffset * BIKE_MAP_PI)) +
                (40.0 * sin(dLongitudeOffset * BIKE_MAP_PI / 3.0))) *
               2.0 / 3.0;
    dResult += ((150.0 * sin(dLongitudeOffset * BIKE_MAP_PI / 12.0)) +
                (300.0 * sin(dLongitudeOffset * BIKE_MAP_PI / 30.0))) *
               2.0 / 3.0;

    return dResult;
}

/* BikeMap_DegreeToE7: 将双精度角度四舍五入为 1e-7 度定点值。 */
static int32_t BikeMap_DegreeToE7(double dDegree)
{
    double dScaled;

    dScaled = dDegree * BIKE_MAP_DEGREE_E7;
    if (0.0 <= dScaled)
    {
        dScaled += 0.5;
    }
    else
    {
        dScaled -= 0.5;
    }

    return (int32_t)dScaled;
}

/* BIKE_MAP_ConvertCoordinate: 按离线瓦片坐标系转换 GNSS WGS-84 坐标。
 * 参数：
 *   - lLatitudeE7/lLongitudeE7: GNSS WGS-84 坐标，单位 1e-7 度
 *   - eCoordinateSystem: 目标坐标系
 *   - pMapLatitudeE7/pMapLongitudeE7: 转换后的地图坐标
 * 返回值：成功返回 true，参数非法返回 false
 */
bool BIKE_MAP_ConvertCoordinate(int32_t lLatitudeE7, int32_t lLongitudeE7,
                                BIKE_MAP_COORDINATE_SYSTEM eCoordinateSystem,
                                int32_t *pMapLatitudeE7,
                                int32_t *pMapLongitudeE7)
{
    double dLatitude;
    double dLongitude;
    double dLatitudeOffset;
    double dLongitudeOffset;
    double dLatitudeRadians;
    double dMagic;
    double dSqrtMagic;

    if ((NULL == pMapLatitudeE7) || (NULL == pMapLongitudeE7) ||
        (-900000000 > lLatitudeE7) || (900000000 < lLatitudeE7) ||
        (-BIKE_MAP_LONGITUDE_LIMIT_E7 > lLongitudeE7) ||
        (BIKE_MAP_LONGITUDE_LIMIT_E7 < lLongitudeE7) ||
        ((BIKE_MAP_COORDINATE_WGS84 != eCoordinateSystem) &&
         (BIKE_MAP_COORDINATE_GCJ02 != eCoordinateSystem)))
    {
        return false;
    }
    *pMapLatitudeE7 = lLatitudeE7;
    *pMapLongitudeE7 = lLongitudeE7;
    if ((BIKE_MAP_COORDINATE_WGS84 == eCoordinateSystem) ||
        (BIKE_MAP_CHINA_LATITUDE_MIN_E7 > lLatitudeE7) ||
        (BIKE_MAP_CHINA_LATITUDE_MAX_E7 < lLatitudeE7) ||
        (BIKE_MAP_CHINA_LONGITUDE_MIN_E7 > lLongitudeE7) ||
        (BIKE_MAP_CHINA_LONGITUDE_MAX_E7 < lLongitudeE7))
    {
        return true;
    }

    dLatitude = (double)lLatitudeE7 / BIKE_MAP_DEGREE_E7;
    dLongitude = (double)lLongitudeE7 / BIKE_MAP_DEGREE_E7;
    dLatitudeOffset = BikeMap_TransformLatitude(dLongitude - 105.0,
                                                dLatitude - 35.0);
    dLongitudeOffset = BikeMap_TransformLongitude(dLongitude - 105.0,
                                                  dLatitude - 35.0);
    dLatitudeRadians = dLatitude * BIKE_MAP_PI / 180.0;
    dMagic = sin(dLatitudeRadians);
    dMagic = 1.0 -
             (BIKE_MAP_GCJ_ECCENTRICITY * dMagic * dMagic);
    dSqrtMagic = sqrt(dMagic);
    dLatitudeOffset = (dLatitudeOffset * 180.0) /
                      (((BIKE_MAP_GCJ_AXIS *
                         (1.0 - BIKE_MAP_GCJ_ECCENTRICITY)) /
                        (dMagic * dSqrtMagic)) * BIKE_MAP_PI);
    dLongitudeOffset = (dLongitudeOffset * 180.0) /
                       ((BIKE_MAP_GCJ_AXIS / dSqrtMagic) *
                        cos(dLatitudeRadians) * BIKE_MAP_PI);
    *pMapLatitudeE7 = BikeMap_DegreeToE7(dLatitude + dLatitudeOffset);
    *pMapLongitudeE7 = BikeMap_DegreeToE7(dLongitude + dLongitudeOffset);

    return true;
}

/* BIKE_MAP_IsExtensionValid: 校验瓦片扩展名只含字母和数字。
 * 参数：
 *   - pExtension: 不含点号的扩展名
 * 返回值：可安全组合路径返回 true，否则返回 false
 */
bool BIKE_MAP_IsExtensionValid(const char *pExtension)
{
    size_t ulIndex;
    char cValue;

    if ((NULL == pExtension) || ('\0' == pExtension[0]))
    {
        return false;
    }
    for (ulIndex = 0U; ulIndex < BIKE_MAP_EXTENSION_MAX; ulIndex++)
    {
        cValue = pExtension[ulIndex];
        if ('\0' == cValue)
        {
            return true;
        }
        if (((('a' > cValue) || ('z' < cValue)) &&
             (('A' > cValue) || ('Z' < cValue))) &&
            (('0' > cValue) || ('9' < cValue)))
        {
            return false;
        }
    }

    return false;
}

/* BIKE_MAP_Project: 将 WGS84 定点经纬度投影到 Web Mercator 瓦片平面。
 * 参数：
 *   - lLatitudeE7/lLongitudeE7: 经纬度，单位 1e-7 度
 *   - ucZoom: 缩放级别，范围 0~19
 *   - pPoint: 输出瓦片与像素坐标
 * 返回值：投影成功返回 true，参数非法返回 false
 */
bool BIKE_MAP_Project(int32_t lLatitudeE7, int32_t lLongitudeE7,
                      uint8_t ucZoom, BIKE_MAP_POINT *pPoint)
{
    uint32_t ulMapSize;
    double dLatitude;
    double dLongitude;
    double dLatitudeRadians;
    double dSine;
    double dNormalizedX;
    double dNormalizedY;
    double dPixelX;
    double dPixelY;

    if ((NULL == pPoint) || (BIKE_MAP_ZOOM_MAX < ucZoom) ||
        (-BIKE_MAP_LONGITUDE_LIMIT_E7 > lLongitudeE7) ||
        (BIKE_MAP_LONGITUDE_LIMIT_E7 < lLongitudeE7) ||
        (-900000000 > lLatitudeE7) || (900000000 < lLatitudeE7))
    {
        return false;
    }
    if (-BIKE_MAP_LATITUDE_LIMIT_E7 > lLatitudeE7)
    {
        lLatitudeE7 = -BIKE_MAP_LATITUDE_LIMIT_E7;
    }
    else if (BIKE_MAP_LATITUDE_LIMIT_E7 < lLatitudeE7)
    {
        lLatitudeE7 = BIKE_MAP_LATITUDE_LIMIT_E7;
    }

    ulMapSize = BIKE_MAP_TILE_SIZE_PX << ucZoom;
    dLatitude = (double)lLatitudeE7 / BIKE_MAP_DEGREE_E7;
    dLongitude = (double)lLongitudeE7 / BIKE_MAP_DEGREE_E7;
    dLatitudeRadians = dLatitude * BIKE_MAP_PI / 180.0;
    dSine = sin(dLatitudeRadians);
    dNormalizedX = (dLongitude + 180.0) / 360.0;
    dNormalizedY = 0.5 - (log((1.0 + dSine) / (1.0 - dSine)) /
                              (4.0 * BIKE_MAP_PI));
    dPixelX = dNormalizedX * (double)ulMapSize + 0.5;
    dPixelY = dNormalizedY * (double)ulMapSize + 0.5;
    if (0.0 > dPixelX)
    {
        dPixelX = 0.0;
    }
    else if ((double)(ulMapSize - 1U) < dPixelX)
    {
        dPixelX = (double)(ulMapSize - 1U);
    }
    if (0.0 > dPixelY)
    {
        dPixelY = 0.0;
    }
    else if ((double)(ulMapSize - 1U) < dPixelY)
    {
        dPixelY = (double)(ulMapSize - 1U);
    }

    pPoint->ulPixelX = (uint32_t)dPixelX;
    pPoint->ulPixelY = (uint32_t)dPixelY;
    pPoint->ulTileX = pPoint->ulPixelX / BIKE_MAP_TILE_SIZE_PX;
    pPoint->ulTileY = pPoint->ulPixelY / BIKE_MAP_TILE_SIZE_PX;
    pPoint->usOffsetX = (uint16_t)(pPoint->ulPixelX % BIKE_MAP_TILE_SIZE_PX);
    pPoint->usOffsetY = (uint16_t)(pPoint->ulPixelY % BIKE_MAP_TILE_SIZE_PX);
    pPoint->ucZoom = ucZoom;

    return true;
}

/* BIKE_MAP_ProjectCoordinate: 将 GNSS WGS-84 坐标按瓦片坐标系转换后投影。
 * 参数：
 *   - lLatitudeE7/lLongitudeE7: GNSS WGS-84 坐标，单位 1e-7 度
 *   - ucZoom: 缩放级别，范围 0~19
 *   - eCoordinateSystem: 离线瓦片使用的坐标系
 *   - pPoint: 输出瓦片与像素坐标
 * 返回值：转换和投影成功返回 true，否则返回 false
 */
bool BIKE_MAP_ProjectCoordinate(int32_t lLatitudeE7, int32_t lLongitudeE7,
                                uint8_t ucZoom,
                                BIKE_MAP_COORDINATE_SYSTEM eCoordinateSystem,
                                BIKE_MAP_POINT *pPoint)
{
    int32_t lMapLatitudeE7;
    int32_t lMapLongitudeE7;

    if (!BIKE_MAP_ConvertCoordinate(lLatitudeE7, lLongitudeE7,
                                    eCoordinateSystem, &lMapLatitudeE7,
                                    &lMapLongitudeE7))
    {
        return false;
    }

    return BIKE_MAP_Project(lMapLatitudeE7, lMapLongitudeE7, ucZoom, pPoint);
}

/* BIKE_MAP_ConvertPixelLevel: 在 Web Mercator 缩放级别之间转换像素坐标。
 * 参数：
 *   - ulSourcePixelX/ulSourcePixelY: 源缩放级别的全局像素坐标
 *   - ucSourceZoom: 源缩放级别，范围 0~19
 *   - ucDestinationZoom: 目标缩放级别，范围 0~19
 *   - pDestinationPixelX/pDestinationPixelY: 目标全局像素坐标
 * 返回值：转换成功返回 true，参数非法返回 false
 */
bool BIKE_MAP_ConvertPixelLevel(uint32_t ulSourcePixelX,
                                uint32_t ulSourcePixelY,
                                uint8_t ucSourceZoom,
                                uint8_t ucDestinationZoom,
                                uint32_t *pDestinationPixelX,
                                uint32_t *pDestinationPixelY)
{
    uint32_t ulSourceMapSize;
    uint8_t ucLevelDifference;

    if ((NULL == pDestinationPixelX) ||
        (NULL == pDestinationPixelY) ||
        (BIKE_MAP_ZOOM_MAX < ucSourceZoom) ||
        (BIKE_MAP_ZOOM_MAX < ucDestinationZoom))
    {
        return false;
    }
    ulSourceMapSize = BIKE_MAP_TILE_SIZE_PX << ucSourceZoom;
    if ((ulSourceMapSize <= ulSourcePixelX) ||
        (ulSourceMapSize <= ulSourcePixelY))
    {
        return false;
    }
    if (ucSourceZoom >= ucDestinationZoom)
    {
        ucLevelDifference = ucSourceZoom - ucDestinationZoom;
        *pDestinationPixelX = ulSourcePixelX >> ucLevelDifference;
        *pDestinationPixelY = ulSourcePixelY >> ucLevelDifference;
    }
    else
    {
        ucLevelDifference = ucDestinationZoom - ucSourceZoom;
        *pDestinationPixelX = ulSourcePixelX << ucLevelDifference;
        *pDestinationPixelY = ulSourcePixelY << ucLevelDifference;
    }

    return true;
}

/* BIKE_MAP_FormatTilePath: 生成 X-TRACK 兼容的 root/zoom/x/y.ext 瓦片路径。
 * 参数：
 *   - pRoot: 绝对根目录，例如 /MAP
 *   - ucZoom/ulTileX/ulTileY: 瓦片坐标
 *   - pExtension: 不含点号的扩展名
 *   - pPath/ulPathSize: 输出缓冲及容量
 * 返回值：成功返回 true，路径非法或截断返回 false
 */
bool BIKE_MAP_FormatTilePath(const char *pRoot, uint8_t ucZoom,
                             uint32_t ulTileX, uint32_t ulTileY,
                             const char *pExtension, char *pPath,
                             size_t ulPathSize)
{
    uint32_t ulTileCount;
    size_t ulRootLength;
    int lLength;

    if ((NULL == pRoot) || (NULL == pPath) || (0U == ulPathSize) ||
        ('/' != pRoot[0]) || (BIKE_MAP_ZOOM_MAX < ucZoom) ||
        (!BIKE_MAP_IsExtensionValid(pExtension)) ||
        (NULL != strstr(pRoot, "..")) || (NULL != strchr(pRoot, '\\')))
    {
        return false;
    }
    ulRootLength = strlen(pRoot);
    if ((1U >= ulRootLength) || ('/' == pRoot[ulRootLength - 1U]))
    {
        return false;
    }
    ulTileCount = 1UL << ucZoom;
    if ((ulTileCount <= ulTileX) || (ulTileCount <= ulTileY))
    {
        return false;
    }
    lLength = snprintf(pPath, ulPathSize, "%s/%u/%lu/%lu.%s", pRoot,
                       (unsigned int)ucZoom, (unsigned long)ulTileX,
                       (unsigned long)ulTileY, pExtension);
    if ((0 >= lLength) || ((size_t)lLength >= ulPathSize))
    {
        pPath[0] = '\0';
        return false;
    }

    return true;
}
