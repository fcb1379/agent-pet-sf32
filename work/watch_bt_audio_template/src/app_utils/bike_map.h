#ifndef BIKE_MAP_H
#define BIKE_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_MAP_ZOOM_MIN (0U)
#define BIKE_MAP_ZOOM_MAX (19U)
#define BIKE_MAP_TILE_SIZE_PX (256U)

/* BIKE_MAP_COORDINATE_SYSTEM: 离线瓦片采用的地理坐标系。 */
typedef enum _BIKE_MAP_COORDINATE_SYSTEM
{
    BIKE_MAP_COORDINATE_WGS84 = 0,
    BIKE_MAP_COORDINATE_GCJ02
} BIKE_MAP_COORDINATE_SYSTEM;

/* BIKE_MAP_POINT: 地图坐标在 Web Mercator 瓦片平面上的定位结果。
 * 成员说明：
 *   - ulPixelX/ulPixelY: 当前缩放级别的全局像素坐标
 *   - ulTileX/ulTileY: 256 x 256 瓦片索引
 *   - usOffsetX/usOffsetY: 瓦片内像素偏移，范围 0~255
 *   - ucZoom: 缩放级别，范围 0~19
 */
typedef struct _BIKE_MAP_POINT
{
    uint32_t ulPixelX;
    uint32_t ulPixelY;
    uint32_t ulTileX;
    uint32_t ulTileY;
    uint16_t usOffsetX;
    uint16_t usOffsetY;
    uint8_t ucZoom;
} BIKE_MAP_POINT;

bool BIKE_MAP_Project(int32_t lLatitudeE7, int32_t lLongitudeE7,
                      uint8_t ucZoom, BIKE_MAP_POINT *pPoint);
bool BIKE_MAP_ConvertCoordinate(int32_t lLatitudeE7, int32_t lLongitudeE7,
                                BIKE_MAP_COORDINATE_SYSTEM eCoordinateSystem,
                                int32_t *pMapLatitudeE7,
                                int32_t *pMapLongitudeE7);
bool BIKE_MAP_ProjectCoordinate(int32_t lLatitudeE7, int32_t lLongitudeE7,
                                uint8_t ucZoom,
                                BIKE_MAP_COORDINATE_SYSTEM eCoordinateSystem,
                                BIKE_MAP_POINT *pPoint);
bool BIKE_MAP_ConvertPixelLevel(uint32_t ulSourcePixelX,
                                uint32_t ulSourcePixelY,
                                uint8_t ucSourceZoom,
                                uint8_t ucDestinationZoom,
                                uint32_t *pDestinationPixelX,
                                uint32_t *pDestinationPixelY);
bool BIKE_MAP_FormatTilePath(const char *pRoot, uint8_t ucZoom,
                             uint32_t ulTileX, uint32_t ulTileY,
                             const char *pExtension, char *pPath,
                             size_t ulPathSize);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_MAP_H */
