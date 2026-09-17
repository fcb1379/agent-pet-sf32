#ifndef BIKE_MAP_IMAGE_H
#define BIKE_MAP_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_MAP_IMAGE_HEADER_SIZE (4U)
#define BIKE_MAP_IMAGE_TILE_SIZE_PX (256U)
#define BIKE_MAP_IMAGE_BYTES_PER_PIXEL (2U)
#define BIKE_MAP_IMAGE_DATA_SIZE                                              \
    (BIKE_MAP_IMAGE_TILE_SIZE_PX * BIKE_MAP_IMAGE_TILE_SIZE_PX *             \
     BIKE_MAP_IMAGE_BYTES_PER_PIXEL)
#define BIKE_MAP_IMAGE_CF_TRUE_COLOR (4U)
#define BIKE_MAP_IMAGE_CF_TRUE_COLOR_CHROMA_KEYED (6U)

/* BIKE_MAP_IMAGE_INFO: 已校验的 LVGL 离线地图瓦片元数据。
 * 成员说明：
 *   - ucColorFormat: LVGL v8 原生 RGB565 色彩格式
 *   - usWidth/usHeight: 瓦片宽高，固定为 256 x 256
 *   - ulDataSize: 像素载荷长度，固定为 131072 bytes
 */
typedef struct _BIKE_MAP_IMAGE_INFO
{
    uint8_t ucColorFormat;
    uint16_t usWidth;
    uint16_t usHeight;
    uint32_t ulDataSize;
} BIKE_MAP_IMAGE_INFO;

bool BIKE_MAP_IMAGE_Load(const char *pPath, uint8_t *pData,
                         size_t ulCapacity, BIKE_MAP_IMAGE_INFO *pInfo);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_MAP_IMAGE_H */
