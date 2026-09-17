#include "bike_map_image.h"

#ifndef BIKE_MAP_IMAGE_HOST_BUILD
#include <dfs_posix.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#define BIKE_MAP_IMAGE_CF_MASK (0x1FU)
#define BIKE_MAP_IMAGE_ALWAYS_ZERO_SHIFT (5U)
#define BIKE_MAP_IMAGE_ALWAYS_ZERO_MASK (0x07U)
#define BIKE_MAP_IMAGE_RESERVED_SHIFT (8U)
#define BIKE_MAP_IMAGE_RESERVED_MASK (0x03U)
#define BIKE_MAP_IMAGE_WIDTH_SHIFT (10U)
#define BIKE_MAP_IMAGE_WIDTH_MASK (0x07FFU)
#define BIKE_MAP_IMAGE_HEIGHT_SHIFT (21U)
#define BIKE_MAP_IMAGE_HEIGHT_MASK (0x07FFU)

/* BikeMapImage_ReadExact: 从普通文件读取指定长度，兼容短读。
 * 参数：
 *   - lFileDescriptor: 已打开的只读文件描述符
 *   - pData: 输出缓冲区
 *   - ulLength: 期望读取长度
 * 返回值：完整读取返回 true，文件提前结束或读取失败返回 false
 */
static bool BikeMapImage_ReadExact(int lFileDescriptor, uint8_t *pData,
                                   size_t ulLength)
{
    size_t ulOffset;
    int lReadLength;

    if ((0 > lFileDescriptor) || (NULL == pData))
    {
        return false;
    }
    ulOffset = 0U;
    while (ulOffset < ulLength)
    {
        lReadLength = (int)read(lFileDescriptor, &pData[ulOffset],
                                ulLength - ulOffset);
        if (0 >= lReadLength)
        {
            return false;
        }
        ulOffset += (size_t)lReadLength;
    }

    return true;
}

/* BIKE_MAP_IMAGE_Load: 加载 X-TRACK/LVGL v8 RGB565 地图瓦片。
 * 参数：
 *   - pPath: `.bin` 瓦片文件路径
 *   - pData: 由调用方提供的固定像素缓冲区
 *   - ulCapacity: 像素缓冲区容量
 *   - pInfo: 输出的已校验图像元数据
 * 返回值：头部、尺寸、格式和文件长度全部合法时返回 true
 */
bool BIKE_MAP_IMAGE_Load(const char *pPath, uint8_t *pData,
                         size_t ulCapacity, BIKE_MAP_IMAGE_INFO *pInfo)
{
    uint8_t aHeader[BIKE_MAP_IMAGE_HEADER_SIZE];
    uint8_t ucExtraByte;
    uint8_t ucColorFormat;
    uint8_t ucAlwaysZero;
    uint8_t ucReserved;
    uint16_t usWidth;
    uint16_t usHeight;
    uint32_t ulHeader;
    int lFileDescriptor;
    int lReadLength;
    bool bLoaded;

    if ((NULL == pPath) || ('\0' == pPath[0]) || (NULL == pData) ||
        (BIKE_MAP_IMAGE_DATA_SIZE > ulCapacity) || (NULL == pInfo))
    {
        return false;
    }
    lFileDescriptor = open(pPath, O_RDONLY, 0);
    if (0 > lFileDescriptor)
    {
        return false;
    }

    bLoaded = false;
    if (BikeMapImage_ReadExact(lFileDescriptor, aHeader,
                               sizeof(aHeader)))
    {
        ulHeader = (uint32_t)aHeader[0] |
                   ((uint32_t)aHeader[1] << 8U) |
                   ((uint32_t)aHeader[2] << 16U) |
                   ((uint32_t)aHeader[3] << 24U);
        ucColorFormat = (uint8_t)(ulHeader & BIKE_MAP_IMAGE_CF_MASK);
        ucAlwaysZero = (uint8_t)((ulHeader >>
                                  BIKE_MAP_IMAGE_ALWAYS_ZERO_SHIFT) &
                                 BIKE_MAP_IMAGE_ALWAYS_ZERO_MASK);
        ucReserved = (uint8_t)((ulHeader >> BIKE_MAP_IMAGE_RESERVED_SHIFT) &
                               BIKE_MAP_IMAGE_RESERVED_MASK);
        usWidth = (uint16_t)((ulHeader >> BIKE_MAP_IMAGE_WIDTH_SHIFT) &
                             BIKE_MAP_IMAGE_WIDTH_MASK);
        usHeight = (uint16_t)((ulHeader >> BIKE_MAP_IMAGE_HEIGHT_SHIFT) &
                              BIKE_MAP_IMAGE_HEIGHT_MASK);
        if ((0U == ucAlwaysZero) && (0U == ucReserved) &&
            ((BIKE_MAP_IMAGE_CF_TRUE_COLOR == ucColorFormat) ||
             (BIKE_MAP_IMAGE_CF_TRUE_COLOR_CHROMA_KEYED == ucColorFormat)) &&
            (BIKE_MAP_IMAGE_TILE_SIZE_PX == usWidth) &&
            (BIKE_MAP_IMAGE_TILE_SIZE_PX == usHeight) &&
            BikeMapImage_ReadExact(lFileDescriptor, pData,
                                   BIKE_MAP_IMAGE_DATA_SIZE))
        {
            lReadLength = (int)read(lFileDescriptor, &ucExtraByte, 1U);
            if (0 == lReadLength)
            {
                pInfo->ucColorFormat = ucColorFormat;
                pInfo->usWidth = usWidth;
                pInfo->usHeight = usHeight;
                pInfo->ulDataSize = BIKE_MAP_IMAGE_DATA_SIZE;
                bLoaded = true;
            }
        }
    }
    (void)close(lFileDescriptor);

    return bLoaded;
}
