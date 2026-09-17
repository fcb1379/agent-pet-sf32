#include "bike_map.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define BIKE_MAP_LATITUDE_LIMIT_E7 (850511288)
#define BIKE_MAP_LONGITUDE_LIMIT_E7 (1800000000)
#define BIKE_MAP_DEGREE_E7 (10000000.0)
#define BIKE_MAP_PI (3.14159265358979323846)

/* BikeMap_IsExtensionValid: 校验瓦片扩展名只含字母和数字。
 * 参数：
 *   - pExtension: 不含点号的扩展名
 * 返回值：可安全组合路径返回 true，否则返回 false
 */
static bool BikeMap_IsExtensionValid(const char *pExtension)
{
    const char *pCursor;

    if ((NULL == pExtension) || ('\0' == pExtension[0]))
    {
        return false;
    }
    pCursor = pExtension;
    while ('\0' != *pCursor)
    {
        if (((('a' > *pCursor) || ('z' < *pCursor)) &&
             (('A' > *pCursor) || ('Z' < *pCursor))) &&
            (('0' > *pCursor) || ('9' < *pCursor)))
        {
            return false;
        }
        pCursor++;
    }

    return true;
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
        (!BikeMap_IsExtensionValid(pExtension)) ||
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
