#include <rtthread.h>
#include <stdio.h>
#include <string.h>

#include "bike_map.h"
#include "bike_map_image.h"
#include "bike_service.h"
#include "bike_settings.h"
#include "bike_sound.h"
#include "bike_storage.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "mem_section.h"

#define APP_ID "Bike"
#define BIKE_UI_REFRESH_PERIOD_MS (500U)
#define BIKE_UI_PANEL_RADIUS (16)
#define BIKE_UI_MAP_TILE_COUNT (9U)
#define BIKE_UI_MAP_TRACK_POINT_MAX (128U)
#define BIKE_UI_MAP_PATH_MAX (96U)
#define BIKE_UI_MAP_ZOOM_DEFAULT (15U)
#define BIKE_UI_MAP_TRACK_LEVEL (16U)
#define BIKE_UI_MAP_TRACK_OFFSET_THRESHOLD_PX (2U)
#define BIKE_UI_MAP_MARKER_WIDTH_PX (16U)
#define BIKE_UI_MAP_MARKER_HEIGHT_PX (20U)
#define BIKE_UI_MAP_MARKER_X (187)
#define BIKE_UI_MAP_MARKER_Y (215)
#define BIKE_UI_MAP_PAGE_CENTER_X (195)
#define BIKE_UI_MAP_PAGE_CENTER_Y (225)
#define BIKE_UI_MAP_PAN_EDGE_WIDTH_PX (24)
#define BIKE_UI_MAP_SHENZHEN_LATITUDE_E7 (225430960)
#define BIKE_UI_MAP_SHENZHEN_LONGITUDE_E7 (1140578650)
#define BIKE_UI_XTRACK_REFERENCE_WIDTH_PX (240)
#define BIKE_UI_XTRACK_REFERENCE_HEIGHT_PX (240)
#define BIKE_UI_XTRACK_TOP_Y (-36)
#define BIKE_UI_XTRACK_TOP_HEIGHT (142)
#define BIKE_UI_XTRACK_METRIC_Y (106)
#define BIKE_UI_XTRACK_METRIC_HEIGHT (90)
#define BIKE_UI_XTRACK_CONTROL_Y (196)
#define BIKE_UI_XTRACK_CONTROL_HEIGHT (40)

#if 16 != LV_COLOR_DEPTH
#error "Bike map tiles require LVGL RGB565 color depth"
#endif

typedef char BIKE_UI_MAP_COLOR_FORMAT_CHECK[
    ((LV_IMG_CF_TRUE_COLOR == BIKE_MAP_IMAGE_CF_TRUE_COLOR) &&
     (LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED ==
      BIKE_MAP_IMAGE_CF_TRUE_COLOR_CHROMA_KEYED)) ? 1 : -1];

LV_IMG_DECLARE(img_bike_computer);

/* BIKE_UI_MAP_TRACK_POINT: 实时轨迹在固定缩放级别的全局像素坐标。
 * 成员说明：
 *   - ulPixelX/ulPixelY: BIKE_UI_MAP_TRACK_LEVEL 级全局像素坐标
 */
typedef struct _BIKE_UI_MAP_TRACK_POINT
{
    uint32_t ulPixelX;
    uint32_t ulPixelY;
} BIKE_UI_MAP_TRACK_POINT;

/* BIKE_UI_MAP_TILE_CACHE: 单个 PSRAM 瓦片槽保存的地图坐标和加载结果。
 * 成员说明：
 *   - ulTileX/ulTileY: Web Mercator 瓦片坐标
 *   - ucZoom: 缩放级别，范围 0~19
 *   - bValid: 当前槽已解析过该瓦片路径
 *   - bLoaded: 瓦片内容校验并加载成功
 */
typedef struct _BIKE_UI_MAP_TILE_CACHE
{
    uint32_t ulTileX;
    uint32_t ulTileY;
    uint8_t ucZoom;
    bool bValid;
    bool bLoaded;
} BIKE_UI_MAP_TILE_CACHE;

/* BIKE_UI_CONTEXT: 码表主页面的静态 LVGL 对象引用。
 * 成员说明：
 *   - pRoot: 页面根对象
 *   - pGpsLabel: GNSS 状态标签
 *   - pPowerLabel: 轨迹记录与电池状态标签
 *   - pSpeedLabel: 当前速度标签
 *   - pSpeedUnit: 当前速度单位和来源标签
 *   - pDistanceLabel: 本次里程标签
 *   - pAverageLabel: 平均速度标签
 *   - pTimeLabel: 移动时间标签
 *   - pCaloriesLabel: 卡路里标签
 *   - pStartLabel: 开始/暂停按钮文字
 *   - pLocationLabel: 定位详情页文本
 *   - pSummaryLabel: 骑行总结页文本
 *   - pMapContainer/apMapTiles: 离线地图瓦片容器和 3 x 3 固定瓦片
 *   - pMapTrackLine/pMapMarker/pMapTouchArea: 实时轨迹、当前位置和浏览触摸区
 *   - pMapEmptyLabel: 未找到离线瓦片时显示的路径提示
 *   - pMapSpeedLabel/pMapDistanceLabel/pMapTimeLabel: 地图页骑行数据叠层
 *   - aMapTrackPoints/aMapLinePoints: 固定级别轨迹点和可见线段点
 *   - atMapTileImages/atMapTileCache: LVGL 图像描述符和增量瓦片缓存键
 *   - tMapViewPoint: 无定位时可拖动的离线地图中心点
 *   - aMapDirectory/aMapRoot/aMapExtension: 地图目录、实际路径和扩展名
 *   - ucMapZoomMin/ucMapZoomMax: 当前地图介质实际可用的缩放范围
 *   - bMapUseWgs84: 当前离线瓦片坐标系选择
 *   - eMapArrowTheme: 当前定位箭头主题
 *   - pTimer: 500 ms UI 刷新定时器
 */
typedef struct _BIKE_UI_CONTEXT
{
    lv_obj_t *pRoot;
    lv_obj_t *pGpsLabel;
    lv_obj_t *pPowerLabel;
    lv_obj_t *pSpeedLabel;
    lv_obj_t *pSpeedUnit;
    lv_obj_t *pDistanceLabel;
    lv_obj_t *pAverageLabel;
    lv_obj_t *pTimeLabel;
    lv_obj_t *pCaloriesLabel;
    lv_obj_t *pStartLabel;
    lv_obj_t *pLocationLabel;
    lv_obj_t *pSummaryLabel;
    lv_obj_t *pMapContainer;
    lv_obj_t *apMapTiles[BIKE_UI_MAP_TILE_COUNT];
    lv_obj_t *pMapTrackLine;
    lv_obj_t *pMapMarker;
    lv_obj_t *pMapTouchArea;
    lv_obj_t *pMapEmptyLabel;
    lv_obj_t *pMapStatusLabel;
    lv_obj_t *pMapSpeedLabel;
    lv_obj_t *pMapDistanceLabel;
    lv_obj_t *pMapTimeLabel;
    BIKE_UI_MAP_TRACK_POINT aMapTrackPoints[BIKE_UI_MAP_TRACK_POINT_MAX];
    lv_point_t aMapLinePoints[BIKE_UI_MAP_TRACK_POINT_MAX];
    lv_img_dsc_t atMapTileImages[BIKE_UI_MAP_TILE_COUNT];
    BIKE_UI_MAP_TILE_CACHE atMapTileCache[BIKE_UI_MAP_TILE_COUNT];
    BIKE_MAP_POINT tMapViewPoint;
    char aMapDirectory[BIKE_STORAGE_MAP_DIRECTORY_MAX];
    char aMapRoot[BIKE_STORAGE_MAP_ROOT_MAX];
    char aMapExtension[BIKE_MAP_EXTENSION_MAX];
    uint32_t ulMapTrackPointCount;
    uint32_t ulMapCenterTileX;
    uint32_t ulMapCenterTileY;
    uint8_t ucMapZoom;
    uint8_t ucMapZoomMin;
    uint8_t ucMapZoomMax;
    uint8_t ucMapLoadedCount;
    BIKE_RIDE_MODE ePreviousRideMode;
    BIKE_MAP_ARROW_THEME eMapArrowTheme;
    bool bMapTilesLoaded;
    bool bMapUseWgs84;
    bool bMapViewValid;
    bool bMapGnssFixValid;
    lv_timer_t *pTimer;
} BIKE_UI_CONTEXT;

/* l_tBikeUi: 仅由 LVGL GUI 线程访问的码表页面上下文。 */
static BIKE_UI_CONTEXT l_tBikeUi;

/* BikeUi_ScaleX: 将 X-TRACK 240 像素参考画布的横向坐标映射到当前屏幕。
 * 参数：
 *   - sReferenceValue: X-TRACK 参考横向坐标或尺寸
 * 返回值：当前屏幕横向坐标或尺寸
 */
static lv_coord_t BikeUi_ScaleX(int16_t sReferenceValue)
{
    int32_t lScaledValue;

    lScaledValue = ((int32_t)sReferenceValue * (int32_t)LV_HOR_RES) /
                   BIKE_UI_XTRACK_REFERENCE_WIDTH_PX;

    return (lv_coord_t)lScaledValue;
}

/* BikeUi_ScaleY: 将 X-TRACK 240 像素参考画布的纵向坐标映射到当前屏幕。
 * 参数：
 *   - sReferenceValue: X-TRACK 参考纵向坐标或尺寸
 * 返回值：当前屏幕纵向坐标或尺寸
 */
static lv_coord_t BikeUi_ScaleY(int16_t sReferenceValue)
{
    int32_t lScaledValue;

    lScaledValue = ((int32_t)sReferenceValue * (int32_t)LV_VER_RES) /
                   BIKE_UI_XTRACK_REFERENCE_HEIGHT_PX;

    return (lv_coord_t)lScaledValue;
}

/* l_aBikeMapMarkerAlpha: 16 x 20 单色向上箭头的 1-bit Alpha 蒙版。
 * 取值范围 0x00~0xFF；仅在编译期固定，主题颜色由 LVGL recolor 设置。
 */
static const uint8_t l_aBikeMapMarkerAlpha[] =
{
    0x01U, 0x80U,
    0x03U, 0xC0U,
    0x07U, 0xE0U,
    0x0FU, 0xF0U,
    0x1FU, 0xF8U,
    0x3FU, 0xFCU,
    0x7FU, 0xFEU,
    0xFFU, 0xFFU,
    0x0FU, 0xF0U,
    0x07U, 0xE0U,
    0x07U, 0xE0U,
    0x07U, 0xE0U,
    0x07U, 0xE0U,
    0x07U, 0xE0U,
    0x07U, 0xE0U,
    0x07U, 0xE0U,
    0x07U, 0xE0U,
    0x07U, 0xE0U,
    0x07U, 0xE0U,
    0x07U, 0xE0U,
};

/* l_tBikeMapMarkerImage: 无像素颜色的固定箭头描述符，旋转时由 LVGL
 * 将 Alpha 蒙版和当前主题颜色组合，不占用运行期像素缓冲。
 */
static const lv_img_dsc_t l_tBikeMapMarkerImage =
{
    .header.always_zero = 0U,
    .header.w = BIKE_UI_MAP_MARKER_WIDTH_PX,
    .header.h = BIKE_UI_MAP_MARKER_HEIGHT_PX,
    .data_size = sizeof(l_aBikeMapMarkerAlpha),
    .header.cf = LV_IMG_CF_ALPHA_1BIT,
    .data = l_aBikeMapMarkerAlpha,
};

/* l_aaBikeMapTileData: 九块 256 x 256 RGB565 离线瓦片像素缓存，
 * 每块固定 131072 bytes，总计 1179648 bytes；仅由 LVGL GUI 线程读写，
 * 放置于 PSRAM 非缓存区以避免占用 HCPU SRAM 和 DMA 缓存一致性问题。
 */
L2_NON_RET_BSS_SECT_BEGIN(bike_map_tile)
L2_NON_RET_BSS_SECT(
    bike_map_tile,
    ALIGN(64) static uint8_t
        l_aaBikeMapTileData[BIKE_UI_MAP_TILE_COUNT][BIKE_MAP_IMAGE_DATA_SIZE]);
L2_NON_RET_BSS_SECT_END

static void BikeUi_Update(void);

/* BikeUi_MapApplyArrowTheme: 将 X-TRACK 主题映射为定位箭头颜色。
 * 参数：
 *   - eTheme: default/light/dark 主题
 * 返回值：主题合法且已应用返回 true，否则返回 false
 */
static bool BikeUi_MapApplyArrowTheme(BIKE_MAP_ARROW_THEME eTheme)
{
    lv_color_t tColor;

    if (NULL == l_tBikeUi.pMapMarker)
    {
        return false;
    }
    if (BIKE_MAP_ARROW_THEME_DEFAULT == eTheme)
    {
        tColor = lv_color_hex(0xFF7A00);
    }
    else if (BIKE_MAP_ARROW_THEME_LIGHT == eTheme)
    {
        tColor = lv_color_hex(0xFFFFFF);
    }
    else if (BIKE_MAP_ARROW_THEME_DARK == eTheme)
    {
        tColor = lv_color_hex(0x142F44);
    }
    else
    {
        return false;
    }
    lv_obj_set_style_img_recolor(l_tBikeUi.pMapMarker, tColor, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(l_tBikeUi.pMapMarker, LV_OPA_COVER,
                                     LV_PART_MAIN);
    l_tBikeUi.eMapArrowTheme = eTheme;

    return true;
}

/* BikeUi_MapApplySource: 应用地图目录和扩展名并重新扫描可用缩放级别。
 * 参数：
 *   - pDirectory: 当前介质内的地图逻辑绝对路径
 *   - pExtension: 不含点号的瓦片文件扩展名
 * 返回值：路径解析成功返回 true，否则返回 false
 */
static bool BikeUi_MapApplySource(const char *pDirectory,
                                  const char *pExtension)
{
    char aFallbackRoot[BIKE_STORAGE_MAP_ROOT_MAX];
    size_t ulDirectoryLength;
    size_t ulExtensionLength;
    uint8_t ucMinimumZoom;
    uint8_t ucMaximumZoom;
    bool bTfMounted;
    bool bZoomRangeFound;

    bTfMounted = BIKE_STORAGE_IsTfMounted();
    if ((!BIKE_MAP_IsExtensionValid(pExtension)) ||
        (!BIKE_STORAGE_FormatMapRoot(bTfMounted, pDirectory,
                                     l_tBikeUi.aMapRoot,
                                     sizeof(l_tBikeUi.aMapRoot))))
    {
        return false;
    }
    ulDirectoryLength = strlen(pDirectory);
    ulExtensionLength = strlen(pExtension);
    (void)memset(l_tBikeUi.aMapDirectory, 0,
                 sizeof(l_tBikeUi.aMapDirectory));
    (void)memcpy(l_tBikeUi.aMapDirectory, pDirectory,
                 ulDirectoryLength + 1U);
    (void)memset(l_tBikeUi.aMapExtension, 0,
                 sizeof(l_tBikeUi.aMapExtension));
    (void)memcpy(l_tBikeUi.aMapExtension, pExtension,
                 ulExtensionLength + 1U);
    ucMinimumZoom = BIKE_MAP_ZOOM_MIN;
    ucMaximumZoom = BIKE_MAP_ZOOM_MAX;
    bZoomRangeFound = BIKE_STORAGE_FindMapZoomRange(l_tBikeUi.aMapRoot,
                                                    &ucMinimumZoom,
                                                    &ucMaximumZoom);
    if ((!bZoomRangeFound) && bTfMounted &&
        BIKE_STORAGE_FormatMapRoot(false, pDirectory, aFallbackRoot,
                                   sizeof(aFallbackRoot)) &&
        BIKE_STORAGE_FindMapZoomRange(aFallbackRoot, &ucMinimumZoom,
                                      &ucMaximumZoom))
    {
        (void)memcpy(l_tBikeUi.aMapRoot, aFallbackRoot,
                     strlen(aFallbackRoot) + 1U);
        bZoomRangeFound = true;
    }
    l_tBikeUi.ucMapZoomMin = bZoomRangeFound ? ucMinimumZoom :
                               BIKE_MAP_ZOOM_MIN;
    l_tBikeUi.ucMapZoomMax = bZoomRangeFound ? ucMaximumZoom :
                               BIKE_MAP_ZOOM_MAX;
    if (l_tBikeUi.ucMapZoomMin > l_tBikeUi.ucMapZoom)
    {
        l_tBikeUi.ucMapZoom = l_tBikeUi.ucMapZoomMin;
    }
    else if (l_tBikeUi.ucMapZoomMax < l_tBikeUi.ucMapZoom)
    {
        l_tBikeUi.ucMapZoom = l_tBikeUi.ucMapZoomMax;
    }
    l_tBikeUi.bMapTilesLoaded = false;
    l_tBikeUi.ucMapLoadedCount = 0U;
    (void)memset(l_tBikeUi.atMapTileCache, 0,
                 sizeof(l_tBikeUi.atMapTileCache));

    return true;
}

/* BikeUi_MapClearTrack: 清空当前实时轨迹的固定点缓冲。
 * 返回值：无
 */
static void BikeUi_MapClearTrack(void)
{
    l_tBikeUi.ulMapTrackPointCount = 0U;
    if (NULL != l_tBikeUi.pMapTrackLine)
    {
        lv_obj_add_flag(l_tBikeUi.pMapTrackLine, LV_OBJ_FLAG_HIDDEN);
    }

    return;
}

/* BikeUi_MapTileCacheNeeded: 判断缓存槽是否属于新的 3 x 3 视口。
 * 参数：
 *   - pCache: 已解析的瓦片缓存键
 *   - pPoint: 新视口中心瓦片
 * 返回值：缓存瓦片仍在新视口内返回 true，否则返回 false
 */
static bool BikeUi_MapTileCacheNeeded(const BIKE_UI_MAP_TILE_CACHE *pCache,
                                      const BIKE_MAP_POINT *pPoint)
{
    int64_t dTileX;
    int64_t dTileY;
    uint32_t ulTileCount;
    uint32_t ulActualTileX;
    int8_t cColumn;
    int8_t cRow;

    if ((NULL == pCache) || (NULL == pPoint) || (!pCache->bValid) ||
        (pCache->ucZoom != pPoint->ucZoom))
    {
        return false;
    }
    ulTileCount = 1UL << pPoint->ucZoom;
    for (cRow = -1; cRow <= 1; cRow++)
    {
        dTileY = (int64_t)pPoint->ulTileY + cRow;
        if ((0LL > dTileY) || ((int64_t)ulTileCount <= dTileY))
        {
            continue;
        }
        for (cColumn = -1; cColumn <= 1; cColumn++)
        {
            dTileX = (int64_t)pPoint->ulTileX + cColumn;
            while (0LL > dTileX)
            {
                dTileX += ulTileCount;
            }
            ulActualTileX = (uint32_t)dTileX % ulTileCount;
            if ((pCache->ulTileX == ulActualTileX) &&
                (pCache->ulTileY == (uint32_t)dTileY))
            {
                return true;
            }
        }
    }

    return false;
}

/* BikeUi_MapReloadTiles: 按中心瓦片重载 3 x 3 离线地图。
 * 参数：
 *   - pPoint: 当前定位的投影坐标
 * 返回值：成功设置的瓦片数量
 */
static uint8_t BikeUi_MapReloadTiles(const BIKE_MAP_POINT *pPoint)
{
    BIKE_MAP_IMAGE_INFO tImageInfo;
    char aMapTilePath[BIKE_UI_MAP_PATH_MAX];
    bool abSlotUsed[BIKE_UI_MAP_TILE_COUNT];
    int64_t dTileX;
    int64_t dTileY;
    uint32_t ulTileCount;
    uint32_t ulActualTileX;
    uint32_t ulActualTileY;
    uint8_t ucIndex;
    uint8_t ucLoadedCount;
    uint8_t ucSlotIndex;
    int8_t cColumn;
    int8_t cRow;
    bool bTargetValid;

    if ((NULL == pPoint) || (NULL == l_tBikeUi.pMapContainer))
    {
        return 0U;
    }
    ulTileCount = 1UL << pPoint->ucZoom;
    ucLoadedCount = 0U;
    (void)memset(abSlotUsed, 0, sizeof(abSlotUsed));
    for (cRow = -1; cRow <= 1; cRow++)
    {
        for (cColumn = -1; cColumn <= 1; cColumn++)
        {
            dTileX = (int64_t)pPoint->ulTileX + cColumn;
            dTileY = (int64_t)pPoint->ulTileY + cRow;
            while (0LL > dTileX)
            {
                dTileX += ulTileCount;
            }
            ulActualTileX = (uint32_t)dTileX % ulTileCount;
            bTargetValid = (0LL <= dTileY) &&
                           ((int64_t)ulTileCount > dTileY);
            ulActualTileY = bTargetValid ? (uint32_t)dTileY : 0U;
            ucSlotIndex = BIKE_UI_MAP_TILE_COUNT;
            if (bTargetValid)
            {
                for (ucIndex = 0U; ucIndex < BIKE_UI_MAP_TILE_COUNT;
                     ucIndex++)
                {
                    if ((!abSlotUsed[ucIndex]) &&
                        l_tBikeUi.atMapTileCache[ucIndex].bValid &&
                        (pPoint->ucZoom ==
                         l_tBikeUi.atMapTileCache[ucIndex].ucZoom) &&
                        (ulActualTileX ==
                         l_tBikeUi.atMapTileCache[ucIndex].ulTileX) &&
                        (ulActualTileY ==
                         l_tBikeUi.atMapTileCache[ucIndex].ulTileY))
                    {
                        ucSlotIndex = ucIndex;
                        break;
                    }
                }
            }
            if (BIKE_UI_MAP_TILE_COUNT == ucSlotIndex)
            {
                for (ucIndex = 0U; ucIndex < BIKE_UI_MAP_TILE_COUNT;
                     ucIndex++)
                {
                    if ((!abSlotUsed[ucIndex]) &&
                        (!BikeUi_MapTileCacheNeeded(
                            &l_tBikeUi.atMapTileCache[ucIndex], pPoint)))
                    {
                        ucSlotIndex = ucIndex;
                        break;
                    }
                }
                if (BIKE_UI_MAP_TILE_COUNT == ucSlotIndex)
                {
                    for (ucIndex = 0U; ucIndex < BIKE_UI_MAP_TILE_COUNT;
                         ucIndex++)
                    {
                        if (!abSlotUsed[ucIndex])
                        {
                            ucSlotIndex = ucIndex;
                            break;
                        }
                    }
                }
                if (BIKE_UI_MAP_TILE_COUNT == ucSlotIndex)
                {
                    continue;
                }
                lv_img_cache_invalidate_src(
                    &l_tBikeUi.atMapTileImages[ucSlotIndex]);
                l_tBikeUi.atMapTileCache[ucSlotIndex].ulTileX =
                    ulActualTileX;
                l_tBikeUi.atMapTileCache[ucSlotIndex].ulTileY =
                    ulActualTileY;
                l_tBikeUi.atMapTileCache[ucSlotIndex].ucZoom =
                    pPoint->ucZoom;
                l_tBikeUi.atMapTileCache[ucSlotIndex].bValid =
                    bTargetValid;
                l_tBikeUi.atMapTileCache[ucSlotIndex].bLoaded = false;
                if (bTargetValid &&
                    BIKE_MAP_FormatTilePath(
                        l_tBikeUi.aMapRoot, pPoint->ucZoom, ulActualTileX,
                        ulActualTileY, l_tBikeUi.aMapExtension,
                        aMapTilePath, sizeof(aMapTilePath)) &&
                    BIKE_MAP_IMAGE_Load(
                        aMapTilePath, l_aaBikeMapTileData[ucSlotIndex],
                        sizeof(l_aaBikeMapTileData[ucSlotIndex]), &tImageInfo))
                {
                    l_tBikeUi.atMapTileImages[ucSlotIndex].header.always_zero =
                        0U;
                    l_tBikeUi.atMapTileImages[ucSlotIndex].header.reserved =
                        0U;
                    l_tBikeUi.atMapTileImages[ucSlotIndex].header.cf =
                        tImageInfo.ucColorFormat;
                    l_tBikeUi.atMapTileImages[ucSlotIndex].header.w =
                        tImageInfo.usWidth;
                    l_tBikeUi.atMapTileImages[ucSlotIndex].header.h =
                        tImageInfo.usHeight;
                    l_tBikeUi.atMapTileImages[ucSlotIndex].data_size =
                        tImageInfo.ulDataSize;
                    l_tBikeUi.atMapTileImages[ucSlotIndex].data =
                        l_aaBikeMapTileData[ucSlotIndex];
                    lv_img_set_src(l_tBikeUi.apMapTiles[ucSlotIndex],
                                   &l_tBikeUi.atMapTileImages[ucSlotIndex]);
                    l_tBikeUi.atMapTileCache[ucSlotIndex].bLoaded = true;
                }
            }
            abSlotUsed[ucSlotIndex] = true;
            lv_obj_set_pos(l_tBikeUi.apMapTiles[ucSlotIndex],
                           (lv_coord_t)((cColumn + 1) *
                                        (int32_t)BIKE_MAP_TILE_SIZE_PX),
                           (lv_coord_t)((cRow + 1) *
                                        (int32_t)BIKE_MAP_TILE_SIZE_PX));
            if (l_tBikeUi.atMapTileCache[ucSlotIndex].bLoaded)
            {
                lv_obj_clear_flag(l_tBikeUi.apMapTiles[ucSlotIndex],
                                  LV_OBJ_FLAG_HIDDEN);
                ucLoadedCount++;
            }
            else
            {
                lv_obj_add_flag(l_tBikeUi.apMapTiles[ucSlotIndex],
                                LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    l_tBikeUi.ulMapCenterTileX = pPoint->ulTileX;
    l_tBikeUi.ulMapCenterTileY = pPoint->ulTileY;
    l_tBikeUi.ucMapLoadedCount = ucLoadedCount;
    l_tBikeUi.bMapTilesLoaded = true;

    return ucLoadedCount;
}

/* BikeUi_MapRebuildLine: 将全局轨迹点转换为当前瓦片容器内坐标。
 * 参数：
 *   - pCenter: 当前地图中心点
 * 返回值：无
 */
static void BikeUi_MapRebuildLine(const BIKE_MAP_POINT *pCenter)
{
    int64_t dFirstPixelX;
    int64_t dFirstPixelY;
    int64_t dMapSize;
    int64_t dRelativeX;
    int64_t dRelativeY;
    uint32_t ulTrackPixelX;
    uint32_t ulTrackPixelY;
    uint32_t ulIndex;
    uint32_t ulVisibleCount;

    if ((NULL == pCenter) || (NULL == l_tBikeUi.pMapTrackLine))
    {
        return;
    }
    dMapSize = (int64_t)BIKE_MAP_TILE_SIZE_PX << pCenter->ucZoom;
    dFirstPixelX = ((int64_t)pCenter->ulTileX - 1LL) *
                   BIKE_MAP_TILE_SIZE_PX;
    dFirstPixelY = ((int64_t)pCenter->ulTileY - 1LL) *
                   BIKE_MAP_TILE_SIZE_PX;
    ulVisibleCount = 0U;
    for (ulIndex = 0U; ulIndex < l_tBikeUi.ulMapTrackPointCount; ulIndex++)
    {
        if (!BIKE_MAP_ConvertPixelLevel(
                l_tBikeUi.aMapTrackPoints[ulIndex].ulPixelX,
                l_tBikeUi.aMapTrackPoints[ulIndex].ulPixelY,
                BIKE_UI_MAP_TRACK_LEVEL, pCenter->ucZoom,
                &ulTrackPixelX, &ulTrackPixelY))
        {
            ulVisibleCount = 0U;
            continue;
        }
        dRelativeX = (int64_t)ulTrackPixelX - dFirstPixelX;
        if (0LL > dRelativeX)
        {
            dRelativeX += dMapSize;
        }
        else if (dMapSize <= dRelativeX)
        {
            dRelativeX -= dMapSize;
        }
        dRelativeY = (int64_t)ulTrackPixelY - dFirstPixelY;
        if ((0LL <= dRelativeX) && (768LL > dRelativeX) &&
            (0LL <= dRelativeY) && (768LL > dRelativeY))
        {
            l_tBikeUi.aMapLinePoints[ulVisibleCount].x =
                (lv_coord_t)dRelativeX;
            l_tBikeUi.aMapLinePoints[ulVisibleCount].y =
                (lv_coord_t)dRelativeY;
            ulVisibleCount++;
        }
        else if (0U < ulVisibleCount)
        {
            /* 只绘制最近的连续可见段，避免跨越屏外点连直线。 */
            ulVisibleCount = 0U;
        }
    }
    if (2U <= ulVisibleCount)
    {
        lv_line_set_points(l_tBikeUi.pMapTrackLine,
                           l_tBikeUi.aMapLinePoints, ulVisibleCount);
        lv_obj_clear_flag(l_tBikeUi.pMapTrackLine, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(l_tBikeUi.pMapTrackLine, LV_OBJ_FLAG_HIDDEN);
    }

    return;
}

/* BikeUi_MapAppendTrack: 追加固定容量轨迹点，满时对历史点二分抽稀。
 * 参数：
 *   - pPoint: 当前投影点
 * 返回值：无
 */
static void BikeUi_MapAppendTrack(const BIKE_MAP_POINT *pPoint)
{
    BIKE_UI_MAP_TRACK_POINT tTrackPoint;
    BIKE_UI_MAP_TRACK_POINT *pPrevious;
    uint32_t ulReadIndex;
    uint32_t ulWriteIndex;
    uint32_t ulDeltaX;
    uint32_t ulDeltaY;

    if (NULL == pPoint)
    {
        return;
    }
    if (!BIKE_MAP_ConvertPixelLevel(pPoint->ulPixelX, pPoint->ulPixelY,
                                    pPoint->ucZoom,
                                    BIKE_UI_MAP_TRACK_LEVEL,
                                    &tTrackPoint.ulPixelX,
                                    &tTrackPoint.ulPixelY))
    {
        return;
    }
    if (0U < l_tBikeUi.ulMapTrackPointCount)
    {
        pPrevious = &l_tBikeUi.aMapTrackPoints[
            l_tBikeUi.ulMapTrackPointCount - 1U];
        ulDeltaX = (pPrevious->ulPixelX > tTrackPoint.ulPixelX) ?
                   (pPrevious->ulPixelX - tTrackPoint.ulPixelX) :
                   (tTrackPoint.ulPixelX - pPrevious->ulPixelX);
        ulDeltaY = (pPrevious->ulPixelY > tTrackPoint.ulPixelY) ?
                   (pPrevious->ulPixelY - tTrackPoint.ulPixelY) :
                   (tTrackPoint.ulPixelY - pPrevious->ulPixelY);
        if ((BIKE_UI_MAP_TRACK_OFFSET_THRESHOLD_PX > ulDeltaX) &&
            (BIKE_UI_MAP_TRACK_OFFSET_THRESHOLD_PX > ulDeltaY))
        {
            return;
        }
    }
    if (BIKE_UI_MAP_TRACK_POINT_MAX == l_tBikeUi.ulMapTrackPointCount)
    {
        ulWriteIndex = 0U;
        for (ulReadIndex = 0U;
             ulReadIndex < l_tBikeUi.ulMapTrackPointCount;
             ulReadIndex += 2U)
        {
            l_tBikeUi.aMapTrackPoints[ulWriteIndex] =
                l_tBikeUi.aMapTrackPoints[ulReadIndex];
            ulWriteIndex++;
        }
        l_tBikeUi.ulMapTrackPointCount = ulWriteIndex;
    }
    l_tBikeUi.aMapTrackPoints[l_tBikeUi.ulMapTrackPointCount] = tTrackPoint;
    l_tBikeUi.ulMapTrackPointCount++;

    return;
}

/* BikeUi_MapSetPointPixels: 由全局像素坐标构造完整地图点。
 * 参数：
 *   - ulPixelX/ulPixelY: 当前缩放级别的全局像素坐标
 *   - ucZoom: 地图缩放级别
 *   - pPoint: 输出地图点
 * 返回值：参数有效返回 true，否则返回 false
 */
static bool BikeUi_MapSetPointPixels(uint32_t ulPixelX, uint32_t ulPixelY,
                                     uint8_t ucZoom,
                                     BIKE_MAP_POINT *pPoint)
{
    uint32_t ulMapSize;

    if ((NULL == pPoint) || (BIKE_MAP_ZOOM_MAX < ucZoom))
    {
        return false;
    }
    ulMapSize = BIKE_MAP_TILE_SIZE_PX << ucZoom;
    if ((ulMapSize <= ulPixelX) || (ulMapSize <= ulPixelY))
    {
        return false;
    }
    pPoint->ulPixelX = ulPixelX;
    pPoint->ulPixelY = ulPixelY;
    pPoint->ulTileX = ulPixelX / BIKE_MAP_TILE_SIZE_PX;
    pPoint->ulTileY = ulPixelY / BIKE_MAP_TILE_SIZE_PX;
    pPoint->usOffsetX = (uint16_t)(ulPixelX % BIKE_MAP_TILE_SIZE_PX);
    pPoint->usOffsetY = (uint16_t)(ulPixelY % BIKE_MAP_TILE_SIZE_PX);
    pPoint->ucZoom = ucZoom;

    return true;
}

/* BikeUi_MapResetDefaultView: 将无定位地图中心恢复到深圳市民中心。
 * 返回值：投影成功返回 true，否则返回 false
 */
static bool BikeUi_MapResetDefaultView(void)
{
    BIKE_MAP_COORDINATE_SYSTEM eCoordinateSystem;

    eCoordinateSystem = l_tBikeUi.bMapUseWgs84 ?
                        BIKE_MAP_COORDINATE_WGS84 :
                        BIKE_MAP_COORDINATE_GCJ02;
    l_tBikeUi.bMapViewValid = BIKE_MAP_ProjectCoordinate(
        BIKE_UI_MAP_SHENZHEN_LATITUDE_E7,
        BIKE_UI_MAP_SHENZHEN_LONGITUDE_E7, l_tBikeUi.ucMapZoom,
        eCoordinateSystem, &l_tBikeUi.tMapViewPoint);

    return l_tBikeUi.bMapViewValid;
}

/* BikeUi_MapUpdateEmptyState: 根据瓦片加载结果更新空地图提示。
 * 参数：
 *   - pPoint: 当前地图视图中心
 *   - ucLoadedCount: 成功加载的瓦片数量
 * 返回值：无
 */
static void BikeUi_MapUpdateEmptyState(const BIKE_MAP_POINT *pPoint,
                                       uint8_t ucLoadedCount)
{
    if ((NULL == pPoint) || (NULL == l_tBikeUi.pMapEmptyLabel))
    {
        return;
    }
    if (0U == ucLoadedCount)
    {
        lv_label_set_text_fmt(l_tBikeUi.pMapEmptyLabel,
                              "NO OFFLINE TILES\n%s/%u/x/y.%s",
                              l_tBikeUi.aMapRoot,
                              (unsigned int)pPoint->ucZoom,
                              l_tBikeUi.aMapExtension);
        lv_obj_clear_flag(l_tBikeUi.pMapEmptyLabel, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(l_tBikeUi.pMapEmptyLabel, LV_OBJ_FLAG_HIDDEN);
    }

    return;
}

/* BikeUi_MapRenderPoint: 按指定中心点加载并摆放 3 x 3 离线瓦片。
 * 参数：
 *   - pPoint: 当前地图视图中心
 * 返回值：成功加载的瓦片数量
 */
static uint8_t BikeUi_MapRenderPoint(const BIKE_MAP_POINT *pPoint)
{
    uint8_t ucLoadedCount;

    if ((NULL == pPoint) || (NULL == l_tBikeUi.pMapContainer))
    {
        return 0U;
    }
    if ((!l_tBikeUi.bMapTilesLoaded) ||
        (l_tBikeUi.ulMapCenterTileX != pPoint->ulTileX) ||
        (l_tBikeUi.ulMapCenterTileY != pPoint->ulTileY))
    {
        ucLoadedCount = BikeUi_MapReloadTiles(pPoint);
    }
    else
    {
        ucLoadedCount = l_tBikeUi.ucMapLoadedCount;
    }
    lv_obj_set_pos(l_tBikeUi.pMapContainer,
                   (lv_coord_t)(BIKE_UI_MAP_PAGE_CENTER_X -
                                (int32_t)BIKE_MAP_TILE_SIZE_PX -
                                pPoint->usOffsetX),
                   (lv_coord_t)(BIKE_UI_MAP_PAGE_CENTER_Y -
                                (int32_t)BIKE_MAP_TILE_SIZE_PX -
                                pPoint->usOffsetY));
    BikeUi_MapRebuildLine(pPoint);
    BikeUi_MapUpdateEmptyState(pPoint, ucLoadedCount);

    return ucLoadedCount;
}

/* BikeUi_MapPanEvent: 无 GNSS 定位时按拖动向量浏览离线地图。
 * 参数：
 *   - pEvent: LVGL 触摸事件
 * 返回值：无
 */
static void BikeUi_MapPanEvent(lv_event_t *pEvent)
{
    lv_indev_t *pInputDevice;
    lv_point_t tVector;
    int64_t dPixelX;
    int64_t dPixelY;
    int64_t dMapSize;
    const char *pCoordinateName;
    uint8_t ucLoadedCount;

    if ((NULL == pEvent) ||
        (LV_EVENT_PRESSING != lv_event_get_code(pEvent)) ||
        l_tBikeUi.bMapGnssFixValid || (!l_tBikeUi.bMapViewValid))
    {
        return;
    }
    pInputDevice = lv_indev_get_act();
    if (NULL == pInputDevice)
    {
        return;
    }
    lv_indev_get_vect(pInputDevice, &tVector);
    if ((0 == tVector.x) && (0 == tVector.y))
    {
        return;
    }

    dMapSize = (int64_t)BIKE_MAP_TILE_SIZE_PX << l_tBikeUi.ucMapZoom;
    dPixelX = (int64_t)l_tBikeUi.tMapViewPoint.ulPixelX - tVector.x;
    dPixelY = (int64_t)l_tBikeUi.tMapViewPoint.ulPixelY - tVector.y;
    dPixelX %= dMapSize;
    if (0LL > dPixelX)
    {
        dPixelX += dMapSize;
    }
    if (0LL > dPixelY)
    {
        dPixelY = 0LL;
    }
    else if (dMapSize <= dPixelY)
    {
        dPixelY = dMapSize - 1LL;
    }
    if (!BikeUi_MapSetPointPixels((uint32_t)dPixelX, (uint32_t)dPixelY,
                                  l_tBikeUi.ucMapZoom,
                                  &l_tBikeUi.tMapViewPoint))
    {
        return;
    }
    ucLoadedCount = BikeUi_MapRenderPoint(&l_tBikeUi.tMapViewPoint);
    pCoordinateName = l_tBikeUi.bMapUseWgs84 ? "WGS" : "GCJ";
    if (0U == ucLoadedCount)
    {
        lv_label_set_text_fmt(l_tBikeUi.pMapStatusLabel,
                              "MAP %s Z%u  0/%u  NO TILES", pCoordinateName,
                              (unsigned int)l_tBikeUi.ucMapZoom,
                              (unsigned int)BIKE_UI_MAP_TILE_COUNT);
    }
    else
    {
        lv_label_set_text_fmt(l_tBikeUi.pMapStatusLabel,
                              "MAP %s Z%u  %u/%u  BROWSE", pCoordinateName,
                              (unsigned int)l_tBikeUi.ucMapZoom,
                              (unsigned int)ucLoadedCount,
                              (unsigned int)BIKE_UI_MAP_TILE_COUNT);
    }

    return;
}

/* BikeUi_MapUpdate: 更新离线瓦片、当前位置和实时轨迹。
 * 参数：
 *   - pSnapshot: 骑行服务一致性快照
 * 返回值：无
 */
static void BikeUi_MapUpdate(const BIKE_SERVICE_SNAPSHOT *pSnapshot)
{
    BIKE_MAP_POINT tPoint;
    BIKE_SETTINGS_SNAPSHOT tSettings;
    BIKE_MAP_COORDINATE_SYSTEM eCoordinateSystem;
    BIKE_MAP_ARROW_THEME eArrowTheme;
    const char *pCoordinateName;
    uint8_t ucLoadedCount;
    bool bFixValid;

    if ((NULL == pSnapshot) || (NULL == l_tBikeUi.pMapStatusLabel) ||
        (NULL == l_tBikeUi.pMapContainer) ||
        (NULL == l_tBikeUi.pMapMarker))
    {
        return;
    }
    if ((BIKE_RIDE_MODE_STOPPED == l_tBikeUi.ePreviousRideMode) &&
        (BIKE_RIDE_MODE_RUNNING == pSnapshot->tRide.eMode))
    {
        BikeUi_MapClearTrack();
    }
    l_tBikeUi.ePreviousRideMode = pSnapshot->tRide.eMode;
    if (RT_EOK == BIKE_SETTINGS_GetSnapshot(&tSettings))
    {
        if (BIKE_MAP_ParseArrowTheme(tSettings.aArrowTheme,
                                     &eArrowTheme) &&
            (l_tBikeUi.eMapArrowTheme != eArrowTheme))
        {
            (void)BikeUi_MapApplyArrowTheme(eArrowTheme);
        }
        if (((l_tBikeUi.bMapUseWgs84 != tSettings.bMapUseWgs84) ||
             (0 != strcmp(l_tBikeUi.aMapDirectory,
                          tSettings.aMapDirectory)) ||
             (0 != strcmp(l_tBikeUi.aMapExtension,
                          tSettings.aMapExtension))) &&
            BikeUi_MapApplySource(tSettings.aMapDirectory,
                                  tSettings.aMapExtension))
        {
            l_tBikeUi.bMapUseWgs84 = tSettings.bMapUseWgs84;
            l_tBikeUi.bMapViewValid = false;
            BikeUi_MapClearTrack();
        }
    }
    if (l_tBikeUi.bMapUseWgs84)
    {
        eCoordinateSystem = BIKE_MAP_COORDINATE_WGS84;
        pCoordinateName = "WGS";
    }
    else
    {
        eCoordinateSystem = BIKE_MAP_COORDINATE_GCJ02;
        pCoordinateName = "GCJ";
    }
    bFixValid = pSnapshot->tGnss.bFixValid &&
                BIKE_MAP_ProjectCoordinate(pSnapshot->tGnss.lLatitudeE7,
                                           pSnapshot->tGnss.lLongitudeE7,
                                           l_tBikeUi.ucMapZoom,
                                           eCoordinateSystem, &tPoint);
    l_tBikeUi.bMapGnssFixValid = bFixValid;
    if (bFixValid)
    {
        l_tBikeUi.tMapViewPoint = tPoint;
        l_tBikeUi.bMapViewValid = true;
        if (NULL != l_tBikeUi.pMapTouchArea)
        {
            lv_obj_add_flag(l_tBikeUi.pMapTouchArea, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        if ((!l_tBikeUi.bMapViewValid) && (!BikeUi_MapResetDefaultView()))
        {
            lv_obj_add_flag(l_tBikeUi.pMapMarker, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_fmt(l_tBikeUi.pMapStatusLabel,
                                  "MAP %s Z%u  NO CENTER", pCoordinateName,
                                  (unsigned int)l_tBikeUi.ucMapZoom);
            return;
        }
        tPoint = l_tBikeUi.tMapViewPoint;
        if (NULL != l_tBikeUi.pMapTouchArea)
        {
            lv_obj_clear_flag(l_tBikeUi.pMapTouchArea, LV_OBJ_FLAG_HIDDEN);
        }
    }
    ucLoadedCount = BikeUi_MapRenderPoint(&tPoint);
    if (!bFixValid)
    {
        lv_obj_add_flag(l_tBikeUi.pMapMarker, LV_OBJ_FLAG_HIDDEN);
        if (0U == ucLoadedCount)
        {
            lv_label_set_text_fmt(l_tBikeUi.pMapStatusLabel,
                                  "MAP %s Z%u  0/%u  NO TILES",
                                  pCoordinateName,
                                  (unsigned int)l_tBikeUi.ucMapZoom,
                                  (unsigned int)BIKE_UI_MAP_TILE_COUNT);
        }
        else
        {
            lv_label_set_text_fmt(l_tBikeUi.pMapStatusLabel,
                                  "MAP %s Z%u  %u/%u  BROWSE",
                                  pCoordinateName,
                                  (unsigned int)l_tBikeUi.ucMapZoom,
                                  (unsigned int)ucLoadedCount,
                                  (unsigned int)BIKE_UI_MAP_TILE_COUNT);
        }
        return;
    }
    lv_img_set_angle(l_tBikeUi.pMapMarker,
                     (int16_t)pSnapshot->tGnss.usCourseDeg10);
    lv_obj_clear_flag(l_tBikeUi.pMapMarker, LV_OBJ_FLAG_HIDDEN);
    if (BIKE_RIDE_MODE_RUNNING == pSnapshot->tRide.eMode)
    {
        BikeUi_MapAppendTrack(&tPoint);
    }
    lv_label_set_text_fmt(l_tBikeUi.pMapStatusLabel,
                          "MAP %s Z%u  %lu/%lu  T%lu", pCoordinateName,
                          (unsigned int)l_tBikeUi.ucMapZoom,
                          (unsigned long)ucLoadedCount,
                          (unsigned long)BIKE_UI_MAP_TILE_COUNT,
                          (unsigned long)l_tBikeUi.ulMapTrackPointCount);

    return;
}

/* BikeUi_MapChangeZoom: 调整离线地图级别并按固定级别轨迹重新投影。
 * 参数：
 *   - cDelta: 缩放级别增量，仅支持 -1 或 1
 * 返回值：无
 */
static void BikeUi_MapChangeZoom(int8_t cDelta)
{
    int16_t sNewZoom;
    uint32_t ulPixelX;
    uint32_t ulPixelY;

    sNewZoom = (int16_t)l_tBikeUi.ucMapZoom + cDelta;
    if (((int16_t)l_tBikeUi.ucMapZoomMin <= sNewZoom) &&
        ((int16_t)l_tBikeUi.ucMapZoomMax >= sNewZoom))
    {
        if (l_tBikeUi.bMapViewValid &&
            BIKE_MAP_ConvertPixelLevel(
                l_tBikeUi.tMapViewPoint.ulPixelX,
                l_tBikeUi.tMapViewPoint.ulPixelY, l_tBikeUi.ucMapZoom,
                (uint8_t)sNewZoom, &ulPixelX, &ulPixelY))
        {
            l_tBikeUi.bMapViewValid = BikeUi_MapSetPointPixels(
                ulPixelX, ulPixelY, (uint8_t)sNewZoom,
                &l_tBikeUi.tMapViewPoint);
        }
        else
        {
            l_tBikeUi.bMapViewValid = false;
        }
        l_tBikeUi.ucMapZoom = (uint8_t)sNewZoom;
        l_tBikeUi.bMapTilesLoaded = false;
        l_tBikeUi.ucMapLoadedCount = 0U;
        BikeUi_Update();
    }

    return;
}

/* BikeUi_MapZoomInEvent: 处理地图放大按钮。
 * 参数：
 *   - pEvent: LVGL 事件
 * 返回值：无
 */
static void BikeUi_MapZoomInEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)))
    {
        BikeUi_MapChangeZoom(1);
    }

    return;
}

/* BikeUi_MapZoomOutEvent: 处理地图缩小按钮。
 * 参数：
 *   - pEvent: LVGL 事件
 * 返回值：无
 */
static void BikeUi_MapZoomOutEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)))
    {
        BikeUi_MapChangeZoom(-1);
    }

    return;
}

/* BikeUi_SetPanelStyle: 设置 X-TRACK 指标单元的无边框样式。
 * 参数：
 *   - pObject: LVGL 对象
 * 返回值：无
 */
static void BikeUi_SetPanelStyle(lv_obj_t *pObject)
{
    if (NULL != pObject)
    {
        lv_obj_set_style_bg_color(pObject, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pObject, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(pObject, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(pObject, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(pObject, 0, LV_PART_MAIN);
        lv_obj_clear_flag(pObject, LV_OBJ_FLAG_SCROLLABLE);
    }

    return;
}

/* BikeUi_CreateMetric: 创建 X-TRACK 风格的数值和标题指标单元。
 * 参数：
 *   - pParent: 父对象
 *   - pTitle: 指标标题
 *   - lX/lY/lWidth/lHeight: 卡片位置和尺寸
 * 返回值：数值标签，失败返回 NULL
 */
static lv_obj_t *BikeUi_CreateMetric(lv_obj_t *pParent, const char *pTitle,
                                    lv_coord_t lX, lv_coord_t lY,
                                    lv_coord_t lWidth, lv_coord_t lHeight)
{
    lv_obj_t *pPanel;
    lv_obj_t *pTitleLabel;
    lv_obj_t *pValueLabel;

    if ((NULL == pParent) || (NULL == pTitle))
    {
        return NULL;
    }

    pPanel = lv_obj_create(pParent);
    if (NULL == pPanel)
    {
        return NULL;
    }
    lv_obj_set_pos(pPanel, lX, lY);
    lv_obj_set_size(pPanel, lWidth, lHeight);
    BikeUi_SetPanelStyle(pPanel);

    pTitleLabel = lv_label_create(pPanel);
    pValueLabel = lv_label_create(pPanel);
    if ((NULL == pTitleLabel) || (NULL == pValueLabel))
    {
        return NULL;
    }

    lv_label_set_text(pTitleLabel, pTitle);
    lv_ext_set_local_font(pTitleLabel, FONT_SMALL, lv_color_hex(0xB3B3B3));
    lv_obj_align(pTitleLabel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_text(pValueLabel, "--");
    lv_ext_set_local_font(pValueLabel, FONT_TITLE, lv_color_white());
    lv_obj_set_width(pValueLabel, lWidth);
    lv_obj_set_style_text_align(pValueLabel, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_align(pValueLabel, LV_ALIGN_TOP_MID, 0, 0);

    return pValueLabel;
}

/* BikeUi_FormatTime: 更新移动时间标签。
 * 参数：
 *   - pLabel: LVGL 标签
 *   - ulTimeMs: 时间毫秒数
 * 返回值：无
 */
static void BikeUi_FormatTime(lv_obj_t *pLabel, uint32_t ulTimeMs)
{
    uint32_t ulTotalSeconds;
    uint32_t ulHours;
    uint32_t ulMinutes;
    uint32_t ulSeconds;

    if (NULL == pLabel)
    {
        return;
    }

    ulTotalSeconds = ulTimeMs / 1000U;
    ulHours = ulTotalSeconds / 3600U;
    ulMinutes = (ulTotalSeconds / 60U) % 60U;
    ulSeconds = ulTotalSeconds % 60U;
    lv_label_set_text_fmt(pLabel, "%02lu:%02lu:%02lu",
                          (unsigned long)ulHours,
                          (unsigned long)ulMinutes,
                          (unsigned long)ulSeconds);

    return;
}

/* BikeUi_FormatDuration: 将毫秒格式化为 HH:MM:SS。
 * 参数：
 *   - ulTimeMs: 时间毫秒数
 *   - pBuffer: 输出缓冲
 *   - ulBufferSize: 输出容量
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeUi_FormatDuration(uint32_t ulTimeMs, char *pBuffer, size_t ulBufferSize)
{
    uint32_t ulTotalSeconds;
    int lLength;

    if ((NULL == pBuffer) || (0U == ulBufferSize))
    {
        return false;
    }

    ulTotalSeconds = ulTimeMs / 1000U;
    lLength = snprintf(pBuffer, ulBufferSize, "%02lu:%02lu:%02lu",
                       (unsigned long)(ulTotalSeconds / 3600U),
                       (unsigned long)((ulTotalSeconds / 60U) % 60U),
                       (unsigned long)(ulTotalSeconds % 60U));

    return (0 < lLength) && ((size_t)lLength < ulBufferSize);
}

/* BikeUi_FormatCoordinate: 将 1e-7 度定点坐标格式化为十进制度。
 * 参数：
 *   - lCoordinateE7: 坐标
 *   - pBuffer: 输出缓冲
 *   - ulBufferSize: 输出容量
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeUi_FormatCoordinate(int32_t lCoordinateE7, char *pBuffer,
                                    size_t ulBufferSize)
{
    int64_t dAbsolute;
    const char *pSign;
    int lLength;

    if ((NULL == pBuffer) || (0U == ulBufferSize))
    {
        return false;
    }

    dAbsolute = lCoordinateE7;
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

/* BikeUi_FormatIntegerSensor: 格式化心率或踏频连接/数据状态。
 * 参数：
 *   - bConnected: 传感器链路是否已建立
 *   - bValid: 最近数据是否有效
 *   - usValue: 整数传感器值
 *   - pUnit: 单位文本
 *   - pBuffer: 输出缓冲
 *   - ulBufferSize: 输出容量
 * 返回值：无
 */
static void BikeUi_FormatIntegerSensor(bool bConnected, bool bValid,
                                       uint16_t usValue, const char *pUnit,
                                       char *pBuffer, size_t ulBufferSize)
{
    int lLength;

    if ((NULL == pUnit) || (NULL == pBuffer) || (0U == ulBufferSize))
    {
        return;
    }
    if (bValid)
    {
        lLength = snprintf(pBuffer, ulBufferSize, "%u %s",
                           (unsigned int)usValue, pUnit);
    }
    else
    {
        lLength = snprintf(pBuffer, ulBufferSize, "%s",
                           bConnected ? "WAIT" : "--");
    }
    if ((0 >= lLength) || ((size_t)lLength >= ulBufferSize))
    {
        pBuffer[0] = '\0';
    }

    return;
}

/* BikeUi_FormatPowerSensor: 格式化可为负值的瞬时功率。
 * 参数：
 *   - bConnected: 功率计链路是否已建立
 *   - bValid: 最近功率数据是否有效
 *   - sPowerWatts: 瞬时功率，单位 W
 *   - pBuffer/ulBufferSize: 输出缓冲及容量
 * 返回值：无
 */
static void BikeUi_FormatPowerSensor(bool bConnected, bool bValid,
                                     int16_t sPowerWatts, char *pBuffer,
                                     size_t ulBufferSize)
{
    int lLength;

    if ((NULL == pBuffer) || (0U == ulBufferSize))
    {
        return;
    }
    if (bValid)
    {
        lLength = snprintf(pBuffer, ulBufferSize, "%d W",
                           (int)sPowerWatts);
    }
    else
    {
        lLength = snprintf(pBuffer, ulBufferSize, "%s",
                           bConnected ? "WAIT" : "--");
    }
    if ((0 >= lLength) || ((size_t)lLength >= ulBufferSize))
    {
        pBuffer[0] = '\0';
    }

    return;
}

/* BikeUi_FormatSpeedSensor: 格式化 CSC 轮速连接/数据状态。
 * 参数：
 *   - bConnected: CSC 链路是否已建立
 *   - bValid: 最近轮速是否有效
 *   - usCentiKph: 轮速，单位 0.01 km/h
 *   - pBuffer: 输出缓冲
 *   - ulBufferSize: 输出容量
 * 返回值：无
 */
static void BikeUi_FormatSpeedSensor(bool bConnected, bool bValid,
                                     uint16_t usCentiKph, char *pBuffer,
                                     size_t ulBufferSize)
{
    int lLength;

    if ((NULL == pBuffer) || (0U == ulBufferSize))
    {
        return;
    }
    if (bValid)
    {
        lLength = snprintf(pBuffer, ulBufferSize, "%u.%02u km/h",
                           (unsigned int)(usCentiKph / 100U),
                           (unsigned int)(usCentiKph % 100U));
    }
    else
    {
        lLength = snprintf(pBuffer, ulBufferSize, "%s",
                           bConnected ? "WAIT" : "--");
    }
    if ((0 >= lLength) || ((size_t)lLength >= ulBufferSize))
    {
        pBuffer[0] = '\0';
    }

    return;
}

/* BikeUi_FormatBattery: 格式化传感器 BAS 电量或连接状态。
 * 参数：
 *   - bConnected: 传感器链路是否已建立
 *   - bValid: 已读取有效 BAS 电量
 *   - ucPercent: 电量百分比
 *   - pBuffer/ulBufferSize: 输出缓冲及容量
 * 返回值：无
 */
static void BikeUi_FormatBattery(bool bConnected, bool bValid,
                                 uint8_t ucPercent, char *pBuffer,
                                 size_t ulBufferSize)
{
    int lLength;

    if ((NULL == pBuffer) || (0U == ulBufferSize))
    {
        return;
    }
    if (bValid && (100U >= ucPercent))
    {
        lLength = snprintf(pBuffer, ulBufferSize, "%u%%",
                           (unsigned int)ucPercent);
    }
    else
    {
        lLength = snprintf(pBuffer, ulBufferSize, "%s",
                           bConnected ? "N/A" : "--");
    }
    if ((0 >= lLength) || ((size_t)lLength >= ulBufferSize))
    {
        pBuffer[0] = '\0';
    }

    return;
}

/* BikeUi_Update: 从线程安全快照刷新现有对象，不在每帧重建控件。
 * 返回值：无
 */
static void BikeUi_Update(void)
{
    BIKE_SERVICE_SNAPSHOT tSnapshot;
    uint32_t ulDistanceCentiKm;
    uint32_t ulAgeMs;
    uint32_t ulAltitudeAbsoluteCm;
    uint64_t udHistoryCentiKm;
    uint64_t udHistoryHours;
    char aLatitude[24];
    char aLongitude[24];
    char aMovingTime[16];
    char aElapsedTime[16];
    char aHeartRate[16];
    char aHeartRateBattery[12];
    char aCadence[16];
    char aCscBattery[12];
    char aCscSpeed[20];
    char aPower[16];
    char aPowerBattery[12];
    char aCompass[24];
    char aBattery[40];
    char aStorage[48];
    BIKE_STORAGE_INFO tStorage;
    uint64_t udStorageTotalMiB;
    uint64_t udStorageFreeMiB;
    const char *pAltitudeSign;
    const char *pGpsState;
    const char *pRecordState;
    const char *pRideState;
    const char *pStepState;
    const char *pPowerState;
    const char *pSpeedSource;
    const char *pFileName;
    const char *pStartText;
    const char *pStorageMedium;

    if (!BIKE_SERVICE_GetSnapshot(&tSnapshot))
    {
        return;
    }

    if (tSnapshot.bDemoMode)
    {
        pGpsState = "DEMO";
    }
    else if (BIKE_GNSS_PORT_ERROR == tSnapshot.ePortStatus)
    {
        pGpsState = "UART3 ERROR";
    }
    else if (tSnapshot.tGnss.bFixValid)
    {
        pGpsState = "GNSS FIX";
    }
    else
    {
        pGpsState = "SEARCHING";
    }

    switch (tSnapshot.tRecorder.eStatus)
    {
    case BIKE_RECORDER_STATUS_WAITING_FIX:
        pRecordState = "WAIT";
        break;

    case BIKE_RECORDER_STATUS_RECORDING:
        pRecordState = "REC";
        break;

    case BIKE_RECORDER_STATUS_PAUSED:
        pRecordState = "PAUSE";
        break;

    case BIKE_RECORDER_STATUS_SAVED:
        pRecordState = "SAVED";
        break;

    case BIKE_RECORDER_STATUS_ERROR:
        pRecordState = "REC ERR";
        break;

    case BIKE_RECORDER_STATUS_IDLE:
    default:
        pRecordState = "IDLE";
        break;
    }

    switch (tSnapshot.tRide.eMode)
    {
    case BIKE_RIDE_MODE_RUNNING:
        pRideState = "RIDING";
        break;

    case BIKE_RIDE_MODE_PAUSED:
        pRideState = tSnapshot.bAutoPaused ? "AUTO PAUSED" : "PAUSED";
        break;

    case BIKE_RIDE_MODE_STOPPED:
    default:
        pRideState = "STOPPED";
        break;
    }

    switch (tSnapshot.tRide.eSpeedSource)
    {
    case BIKE_SPEED_SOURCE_CSC:
        pSpeedSource = "CSC";
        break;

    case BIKE_SPEED_SOURCE_GNSS:
        pSpeedSource = "GPS";
        break;

    case BIKE_SPEED_SOURCE_NONE:
    default:
        pSpeedSource = "--";
        break;
    }

    switch (tSnapshot.tPedometer.eStatus)
    {
    case BIKE_PEDOMETER_STATUS_READY:
        pStepState = "READY";
        break;

    case BIKE_PEDOMETER_STATUS_SEARCHING:
        pStepState = "WAIT";
        break;

    case BIKE_PEDOMETER_STATUS_ERROR:
        pStepState = "ERROR";
        break;

    case BIKE_PEDOMETER_STATUS_DISABLED:
    default:
        pStepState = "OFF";
        break;
    }

    switch (tSnapshot.tCompass.eStatus)
    {
    case BIKE_COMPASS_STATUS_READY:
        (void)snprintf(aCompass, sizeof(aCompass), "%u.%01u deg",
                       (unsigned int)(tSnapshot.tCompass.usHeadingDeg10 / 10U),
                       (unsigned int)(tSnapshot.tCompass.usHeadingDeg10 % 10U));
        break;

    case BIKE_COMPASS_STATUS_CALIBRATING:
        (void)snprintf(aCompass, sizeof(aCompass), "CAL %u%%",
                       (unsigned int)tSnapshot.tCompass.ucCalibrationPercent);
        break;

    case BIKE_COMPASS_STATUS_SEARCHING:
        (void)snprintf(aCompass, sizeof(aCompass), "WAIT");
        break;

    case BIKE_COMPASS_STATUS_ERROR:
        (void)snprintf(aCompass, sizeof(aCompass), "ERROR");
        break;

    case BIKE_COMPASS_STATUS_DISABLED:
    default:
        (void)snprintf(aCompass, sizeof(aCompass), "OFF");
        break;
    }

    switch (tSnapshot.tPower.eStatus)
    {
    case BIKE_POWER_STATUS_READY:
        if (!tSnapshot.tPower.bChargeStatusValid)
        {
            pPowerState = "PWR?";
        }
        else if (tSnapshot.tPower.bFull)
        {
            pPowerState = "FULL";
        }
        else if (tSnapshot.tPower.bExternalPower)
        {
            pPowerState = "USB";
        }
        else
        {
            pPowerState = "BAT";
        }
        (void)snprintf(aBattery, sizeof(aBattery), "%s %u%% %lu.%03lu V",
                       pPowerState,
                       (unsigned int)tSnapshot.tPower.ucPercent,
                       (unsigned long)(
                           tSnapshot.tPower.ulVoltageDeciMv / 10000U),
                       (unsigned long)(
                           (tSnapshot.tPower.ulVoltageDeciMv % 10000U) /
                           10U));
        break;

    case BIKE_POWER_STATUS_SEARCHING:
        pPowerState = "PWR";
        (void)snprintf(aBattery, sizeof(aBattery), "PWR WAIT");
        break;

    case BIKE_POWER_STATUS_ERROR:
        pPowerState = "PWR";
        (void)snprintf(aBattery, sizeof(aBattery), "PWR ERROR");
        break;

    case BIKE_POWER_STATUS_DISABLED:
    default:
        pPowerState = "PWR";
        (void)snprintf(aBattery, sizeof(aBattery), "PWR OFF");
        break;
    }

    (void)memset(&tStorage, 0, sizeof(tStorage));
    pStorageMedium = "INT";
    if (BIKE_STORAGE_GetInfo(&tStorage))
    {
        if (BIKE_STORAGE_MEDIUM_TF == tStorage.eMedium)
        {
            pStorageMedium = "TF";
        }
        if (tStorage.bAvailable)
        {
            udStorageTotalMiB = tStorage.udTotalBytes / (1024ULL * 1024ULL);
            udStorageFreeMiB = tStorage.udFreeBytes / (1024ULL * 1024ULL);
            (void)snprintf(aStorage, sizeof(aStorage), "%s %llu/%llu MiB",
                           pStorageMedium,
                           (unsigned long long)udStorageFreeMiB,
                           (unsigned long long)udStorageTotalMiB);
        }
        else
        {
            (void)snprintf(aStorage, sizeof(aStorage), "%s ERR %lu",
                           pStorageMedium,
                           (unsigned long)tStorage.ulQueryErrorCount);
        }
    }
    else
    {
        (void)snprintf(aStorage, sizeof(aStorage), "STORAGE WAIT");
    }

    ulAgeMs = 0U;
    if (0U != tSnapshot.ulLastUpdateMs)
    {
        ulAgeMs = (uint32_t)rt_tick_get_millisecond() - tSnapshot.ulLastUpdateMs;
    }
    lv_label_set_text_fmt(l_tBikeUi.pGpsLabel, "%s S%u A%lus",
                          pGpsState,
                          (unsigned int)tSnapshot.tGnss.ucSatellites,
                          (unsigned long)(ulAgeMs / 1000U));
    if (BIKE_POWER_STATUS_READY == tSnapshot.tPower.eStatus)
    {
        lv_label_set_text_fmt(l_tBikeUi.pPowerLabel, "%s %lu | %s %u%%",
                              pRecordState,
                              (unsigned long)tSnapshot.tRecorder.ulPointCount,
                              pPowerState,
                              (unsigned int)tSnapshot.tPower.ucPercent);
    }
    else
    {
        lv_label_set_text_fmt(l_tBikeUi.pPowerLabel, "%s %lu | %s --",
                              pRecordState,
                              (unsigned long)tSnapshot.tRecorder.ulPointCount,
                              pPowerState);
    }

    lv_label_set_text_fmt(l_tBikeUi.pSpeedLabel, "%02u",
                          (unsigned int)(
                              (tSnapshot.tRide.usSpeedCentiKph + 50U) / 100U));
    lv_label_set_text_fmt(l_tBikeUi.pSpeedUnit, "km/h %s", pSpeedSource);

    ulDistanceCentiKm = tSnapshot.tRide.ulDistanceMm / 10000U;
    lv_label_set_text_fmt(l_tBikeUi.pDistanceLabel, "%lu.%02lu km",
                          (unsigned long)(ulDistanceCentiKm / 100U),
                          (unsigned long)(ulDistanceCentiKm % 100U));
    lv_label_set_text_fmt(l_tBikeUi.pAverageLabel, "%u.%02u km/h",
                          (unsigned int)(tSnapshot.tRide.usAverageSpeedCentiKph / 100U),
                          (unsigned int)(tSnapshot.tRide.usAverageSpeedCentiKph % 100U));
    BikeUi_FormatTime(l_tBikeUi.pTimeLabel, tSnapshot.tRide.ulMovingTimeMs);
    lv_label_set_text_fmt(l_tBikeUi.pCaloriesLabel, "%lu.%01lu kcal",
                          (unsigned long)(
                              tSnapshot.tRide.ulCaloriesMilliKcal / 1000U),
                          (unsigned long)(
                              (tSnapshot.tRide.ulCaloriesMilliKcal % 1000U) /
                              100U));
    pAltitudeSign = (0 > tSnapshot.tRide.lAltitudeCm) ? "-" : "";
    ulAltitudeAbsoluteCm = (uint32_t)((0 > tSnapshot.tRide.lAltitudeCm) ?
                                      -(int64_t)tSnapshot.tRide.lAltitudeCm :
                                      (int64_t)tSnapshot.tRide.lAltitudeCm);

    if (BIKE_RIDE_MODE_RUNNING == tSnapshot.tRide.eMode)
    {
        pStartText = LV_SYMBOL_PAUSE;
    }
    else if (BIKE_RIDE_MODE_PAUSED == tSnapshot.tRide.eMode)
    {
        pStartText = LV_SYMBOL_PLAY;
    }
    else
    {
        pStartText = LV_SYMBOL_PLAY;
    }
    lv_label_set_text(l_tBikeUi.pStartLabel, pStartText);

    if (NULL != l_tBikeUi.pMapSpeedLabel)
    {
        lv_label_set_text_fmt(l_tBikeUi.pMapSpeedLabel, "%02u",
                              (unsigned int)(
                                  (tSnapshot.tRide.usSpeedCentiKph + 50U) /
                                  100U));
    }
    if (NULL != l_tBikeUi.pMapDistanceLabel)
    {
        lv_label_set_text_fmt(l_tBikeUi.pMapDistanceLabel, "%lu.%02lu km",
                              (unsigned long)(ulDistanceCentiKm / 100U),
                              (unsigned long)(ulDistanceCentiKm % 100U));
    }
    BikeUi_FormatTime(l_tBikeUi.pMapTimeLabel,
                      tSnapshot.tRide.ulMovingTimeMs);

    if ((!tSnapshot.tGnss.bFixValid) ||
        (!BikeUi_FormatCoordinate(tSnapshot.tGnss.lLatitudeE7,
                                  aLatitude, sizeof(aLatitude))))
    {
        (void)snprintf(aLatitude, sizeof(aLatitude), "--");
    }
    if ((!tSnapshot.tGnss.bFixValid) ||
        (!BikeUi_FormatCoordinate(tSnapshot.tGnss.lLongitudeE7,
                                  aLongitude, sizeof(aLongitude))))
    {
        (void)snprintf(aLongitude, sizeof(aLongitude), "--");
    }
    udHistoryCentiKm = tSnapshot.tHistory.tRecord.udDistanceMm / 10000ULL;
    udHistoryHours = tSnapshot.tHistory.tRecord.udMovingTimeMs / 3600000ULL;
    lv_label_set_text_fmt(l_tBikeUi.pLocationLabel,
                          "LAT  %s\nLON  %s\nALT  %s%lu.%02lu m\n"
                          "COURSE  %u.%01u deg\nUTC  %04u-%02u-%02u %02u:%02u:%02u\n"
                          "SAT  %u   FIX  %u   RTC  %s\nNMEA  %lu   CRC ERR  %lu   OVF  %lu\n"
                          "STEPS  %lu   IMU  %s   I2C ERR  %lu\n"
                          "MAG  %s   ERR  %lu\nXYZ  %ld  %ld  %ld mG\n"
                          "PWR  %s\nSTORAGE  %s\nADC ERR  %lu   CHG ERR  %lu\n"
                          "LIFE  %llu.%02llu km   %llu h\nRIDES  %lu   MAX  %u.%02u km/h",
                          aLatitude, aLongitude, pAltitudeSign,
                          (unsigned long)(ulAltitudeAbsoluteCm / 100U),
                          (unsigned long)(ulAltitudeAbsoluteCm % 100U),
                          (unsigned int)(tSnapshot.tGnss.usCourseDeg10 / 10U),
                          (unsigned int)(tSnapshot.tGnss.usCourseDeg10 % 10U),
                          tSnapshot.tGnss.usYear, tSnapshot.tGnss.ucMonth,
                          tSnapshot.tGnss.ucDay, tSnapshot.tGnss.ucHour,
                          tSnapshot.tGnss.ucMinute, tSnapshot.tGnss.ucSecond,
                          tSnapshot.tGnss.ucSatellites, tSnapshot.tGnss.ucFixQuality,
                          tSnapshot.bRtcSynchronized ? "SYNC" : "WAIT",
                          (unsigned long)tSnapshot.ulAcceptedCount,
                          (unsigned long)tSnapshot.ulChecksumErrorCount,
                          (unsigned long)tSnapshot.ulOverflowCount,
                          (unsigned long)tSnapshot.tPedometer.ulStepCount,
                          pStepState,
                          (unsigned long)tSnapshot.tPedometer.ulReadErrorCount,
                          aCompass,
                          (unsigned long)tSnapshot.tCompass.ulReadErrorCount,
                          (long)tSnapshot.tCompass.lXMilliGauss,
                          (long)tSnapshot.tCompass.lYMilliGauss,
                          (long)tSnapshot.tCompass.lZMilliGauss,
                          aBattery,
                          aStorage,
                          (unsigned long)tSnapshot.tPower.ulAdcErrorCount,
                          (unsigned long)tSnapshot.tPower.ulChargeErrorCount,
                          (unsigned long long)(udHistoryCentiKm / 100ULL),
                          (unsigned long long)(udHistoryCentiKm % 100ULL),
                          (unsigned long long)udHistoryHours,
                          (unsigned long)tSnapshot.tHistory.tRecord.ulRideCount,
                          (unsigned int)(
                              tSnapshot.tHistory.tRecord.
                              usMaximumSpeedCentiKph / 100U),
                          (unsigned int)(
                              tSnapshot.tHistory.tRecord.
                              usMaximumSpeedCentiKph % 100U));

    (void)BikeUi_FormatDuration(tSnapshot.tRide.ulMovingTimeMs,
                                aMovingTime, sizeof(aMovingTime));
    (void)BikeUi_FormatDuration(tSnapshot.tRide.ulElapsedTimeMs,
                                aElapsedTime, sizeof(aElapsedTime));
    pFileName = strrchr(tSnapshot.tRecorder.aFilePath, '/');
    if (NULL != pFileName)
    {
        pFileName++;
    }
    else if ('\0' != tSnapshot.tRecorder.aFilePath[0])
    {
        pFileName = tSnapshot.tRecorder.aFilePath;
    }
    else
    {
        pFileName = "--";
    }
    BikeUi_FormatIntegerSensor(tSnapshot.tSensors.bHeartRateConnected,
                               tSnapshot.tSensors.bHeartRateValid,
                               tSnapshot.tSensors.usHeartRateBpm, "bpm",
                               aHeartRate, sizeof(aHeartRate));
    BikeUi_FormatIntegerSensor(tSnapshot.tSensors.bCscConnected,
                               tSnapshot.tSensors.bCadenceValid,
                               tSnapshot.tSensors.usCadenceRpm, "rpm",
                               aCadence, sizeof(aCadence));
    BikeUi_FormatSpeedSensor(tSnapshot.tSensors.bCscConnected,
                             tSnapshot.tSensors.bWheelSpeedValid,
                             tSnapshot.tSensors.usWheelSpeedCentiKph,
                             aCscSpeed, sizeof(aCscSpeed));
    BikeUi_FormatBattery(tSnapshot.tSensors.bHeartRateConnected,
                         tSnapshot.tSensors.bHeartRateBatteryValid,
                         tSnapshot.tSensors.ucHeartRateBatteryPercent,
                         aHeartRateBattery, sizeof(aHeartRateBattery));
    BikeUi_FormatBattery(tSnapshot.tSensors.bCscConnected,
                         tSnapshot.tSensors.bCscBatteryValid,
                         tSnapshot.tSensors.ucCscBatteryPercent,
                         aCscBattery, sizeof(aCscBattery));
    BikeUi_FormatPowerSensor(tSnapshot.tSensors.bPowerConnected,
                             tSnapshot.tSensors.bPowerValid,
                             tSnapshot.tSensors.sPowerWatts,
                             aPower, sizeof(aPower));
    BikeUi_FormatBattery(tSnapshot.tSensors.bPowerConnected,
                         tSnapshot.tSensors.bPowerBatteryValid,
                         tSnapshot.tSensors.ucPowerBatteryPercent,
                         aPowerBattery, sizeof(aPowerBattery));
    lv_label_set_text_fmt(l_tBikeUi.pSummaryLabel,
                          "STATE  %s\nDIST  %lu.%02lu km\nMOVING  %s\nELAPSED  %s\n"
                          "AVG  %u.%02u km/h\nMAX  %u.%02u km/h\n"
                           "CAL  %lu.%03lu kcal\nHR  %s  HB  %s\n"
                           "CAD  %s  CB  %s\nCSC  %s\nPWR  %s  PB  %s\n"
                           "STEP  %lu  IMU  %s\n"
                           "TRACK  %s / %lu pt\nFILE  %s",
                          pRideState,
                          (unsigned long)(ulDistanceCentiKm / 100U),
                          (unsigned long)(ulDistanceCentiKm % 100U),
                          aMovingTime, aElapsedTime,
                          (unsigned int)(tSnapshot.tRide.usAverageSpeedCentiKph / 100U),
                          (unsigned int)(tSnapshot.tRide.usAverageSpeedCentiKph % 100U),
                          (unsigned int)(tSnapshot.tRide.usMaximumSpeedCentiKph / 100U),
                          (unsigned int)(tSnapshot.tRide.usMaximumSpeedCentiKph % 100U),
                          (unsigned long)(tSnapshot.tRide.ulCaloriesMilliKcal / 1000U),
                          (unsigned long)(tSnapshot.tRide.ulCaloriesMilliKcal % 1000U),
                           aHeartRate, aHeartRateBattery,
                           aCadence, aCscBattery, aCscSpeed,
                           aPower, aPowerBattery,
                          (unsigned long)tSnapshot.tPedometer.ulStepCount,
                          pStepState,
                          pRecordState,
                          (unsigned long)tSnapshot.tRecorder.ulPointCount,
                          pFileName);
    BikeUi_MapUpdate(&tSnapshot);

    return;
}

/* BikeUi_TimerCallback: LVGL 定时刷新回调。
 * 参数：
 *   - pTimer: LVGL 定时器
 * 返回值：无
 */
static void BikeUi_TimerCallback(lv_timer_t *pTimer)
{
    (void)pTimer;
    BikeUi_Update();

    return;
}

/* BikeUi_StartEvent: 处理开始、继续和暂停操作。
 * 参数：
 *   - pEvent: LVGL 事件
 * 返回值：无
 */
static void BikeUi_StartEvent(lv_event_t *pEvent)
{
    BIKE_SERVICE_SNAPSHOT tSnapshot;
    lv_event_code_t eEventCode;

    if (NULL == pEvent)
    {
        return;
    }
    eEventCode = lv_event_get_code(pEvent);
    if ((LV_EVENT_CLICKED != eEventCode) &&
        (LV_EVENT_SHORT_CLICKED != eEventCode))
    {
        return;
    }

    if (BIKE_SERVICE_GetSnapshot(&tSnapshot))
    {
        if (BIKE_RIDE_MODE_RUNNING == tSnapshot.tRide.eMode)
        {
            (void)BIKE_SERVICE_PauseRide();
        }
        else
        {
            (void)BIKE_SERVICE_StartRide();
        }
        BikeUi_Update();
    }

    return;
}

/* BikeUi_StopEvent: 处理结束骑行操作。
 * 参数：
 *   - pEvent: LVGL 事件
 * 返回值：无
 */
static void BikeUi_StopEvent(lv_event_t *pEvent)
{
    lv_event_code_t eEventCode;

    if (NULL == pEvent)
    {
        return;
    }
    eEventCode = lv_event_get_code(pEvent);
    if ((LV_EVENT_CLICKED == eEventCode) ||
        (LV_EVENT_LONG_PRESSED == eEventCode))
    {
        (void)BIKE_SERVICE_StopRide();
        BikeUi_Update();
    }

    return;
}

/* BikeUi_DiscardEvent: 结束骑行并丢弃当前未发布轨迹。
 * 参数：
 *   - pEvent: LVGL 事件
 * 返回值：无
 */
static void BikeUi_DiscardEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)))
    {
        (void)BIKE_SERVICE_DiscardRide();
        BikeUi_Update();
    }

    return;
}

/* BikeUi_OpenMapEvent: 从主码表进入离线地图页。
 * 参数：
 *   - pEvent: LVGL 点击事件
 * 返回值：无
 */
static void BikeUi_OpenMapEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) &&
        (LV_EVENT_CLICKED == lv_event_get_code(pEvent)) &&
        (NULL != l_tBikeUi.pRoot))
    {
        lv_obj_set_tile_id(l_tBikeUi.pRoot, 2U, 0U, LV_ANIM_ON);
    }

    return;
}

/* BikeUi_OpenInfoEvent: 从主码表进入定位和系统信息页。
 * 参数：
 *   - pEvent: LVGL 点击事件
 * 返回值：无
 */
static void BikeUi_OpenInfoEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) &&
        (LV_EVENT_CLICKED == lv_event_get_code(pEvent)) &&
        (NULL != l_tBikeUi.pRoot))
    {
        lv_obj_set_tile_id(l_tBikeUi.pRoot, 1U, 0U, LV_ANIM_ON);
    }

    return;
}

/* BikeUi_CreateDialButton: 按 X-TRACK 占屏比例创建主码表操作按钮。
 * 参数：
 *   - pParent: 底部操作区
 *   - pText: 按钮符号或文字
 *   - sReferenceCenterX: 240 像素参考画布中的按钮中心 X
 *   - pCallback: 事件回调
 *   - eEventCode: 需要投递的事件类型
 * 返回值：按钮文字标签，失败返回 NULL
 */
static lv_obj_t *BikeUi_CreateDialButton(lv_obj_t *pParent,
                                         const char *pText,
                                         int16_t sReferenceCenterX,
                                         lv_event_cb_t pCallback,
                                         lv_event_code_t eEventCode)
{
    lv_obj_t *pButton;
    lv_obj_t *pLabel;
    lv_coord_t lButtonWidth;
    lv_coord_t lButtonHeight;
    lv_coord_t lControlHeight;

    if ((NULL == pParent) || (NULL == pText) || (NULL == pCallback))
    {
        return NULL;
    }

    lButtonWidth = BikeUi_ScaleX(40);
    lButtonHeight = BikeUi_ScaleY(31);
    lControlHeight = BikeUi_ScaleY(BIKE_UI_XTRACK_CONTROL_HEIGHT);
    pButton = lv_btn_create(pParent);
    if (NULL == pButton)
    {
        return NULL;
    }
    lv_obj_set_pos(pButton,
                   BikeUi_ScaleX(sReferenceCenterX) - (lButtonWidth / 2),
                   (lControlHeight - lButtonHeight) / 2);
    lv_obj_set_size(pButton, lButtonWidth, lButtonHeight);
    lv_obj_set_style_bg_color(pButton, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pButton, lv_color_hex(0xBBBBBB),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(pButton, lv_color_hex(0xFF931E),
                              LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(pButton, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(pButton, BikeUi_ScaleX(9), LV_PART_MAIN);
    lv_obj_set_style_pad_all(pButton, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(pButton, pCallback, eEventCode, NULL);

    pLabel = lv_label_create(pButton);
    if (NULL == pLabel)
    {
        return NULL;
    }
    lv_label_set_text(pLabel, pText);
    lv_obj_set_style_text_color(pLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(pLabel);

    return pLabel;
}

/* BikeUi_CreateButton: 创建底部大触控按钮。
 * 参数：
 *   - pParent: 父对象
 *   - pText: 按钮文字
 *   - lX: X 坐标
 *   - tColor: 背景色
 *   - pCallback: 点击回调
 * 返回值：按钮文字标签，失败返回 NULL
 */
static lv_obj_t *BikeUi_CreateButton(lv_obj_t *pParent, const char *pText, lv_coord_t lX,
                                    lv_color_t tColor, lv_event_cb_t pCallback)
{
    lv_obj_t *pButton;
    lv_obj_t *pLabel;

    if ((NULL == pParent) || (NULL == pText) || (NULL == pCallback))
    {
        return NULL;
    }

    pButton = lv_btn_create(pParent);
    if (NULL == pButton)
    {
        return NULL;
    }
    lv_obj_set_pos(pButton, lX, 368);
    lv_obj_set_size(pButton, 170, 58);
    lv_obj_set_style_bg_color(pButton, tColor, LV_PART_MAIN);
    lv_obj_set_style_radius(pButton, BIKE_UI_PANEL_RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(pButton, pCallback, LV_EVENT_CLICKED, NULL);

    pLabel = lv_label_create(pButton);
    if (NULL != pLabel)
    {
        lv_label_set_text(pLabel, pText);
        lv_obj_center(pLabel);
    }

    return pLabel;
}

/* BikeUi_CreateMapButton: 创建地图页右下角缩放按钮。
 * 参数：
 *   - pParent: 地图页对象
 *   - pText: 按钮文字
 *   - lX/lY: 按钮坐标
 *   - pCallback: 点击回调
 * 返回值：按钮对象，失败返回 NULL
 */
static lv_obj_t *BikeUi_CreateMapButton(lv_obj_t *pParent, const char *pText,
                                        lv_coord_t lX, lv_coord_t lY,
                                        lv_event_cb_t pCallback)
{
    lv_obj_t *pButton;
    lv_obj_t *pLabel;

    if ((NULL == pParent) || (NULL == pText) || (NULL == pCallback))
    {
        return NULL;
    }
    pButton = lv_btn_create(pParent);
    if (NULL == pButton)
    {
        return NULL;
    }
    lv_obj_set_pos(pButton, lX, lY);
    lv_obj_set_size(pButton, 52, 52);
    lv_obj_set_style_bg_color(pButton, lv_color_hex(0x17212B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pButton, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_radius(pButton, 26, LV_PART_MAIN);
    lv_obj_add_event_cb(pButton, pCallback, LV_EVENT_CLICKED, NULL);
    pLabel = lv_label_create(pButton);
    if (NULL != pLabel)
    {
        lv_label_set_text(pLabel, pText);
        lv_obj_center(pLabel);
    }

    return pButton;
}

/* BikeUi_SetPageStyle: 设置 tileview 子页面统一背景。
 * 参数：
 *   - pPage: tileview 页面
 * 返回值：无
 */
static void BikeUi_SetPageStyle(lv_obj_t *pPage)
{
    if (NULL != pPage)
    {
        lv_obj_set_style_bg_color(pPage, lv_color_hex(0x091017), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pPage, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(pPage, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(pPage, 0, LV_PART_MAIN);
        lv_obj_clear_flag(pPage, LV_OBJ_FLAG_SCROLLABLE);
    }

    return;
}

/* BikeUi_CreatePageTitle: 创建页面标题和滑动提示。
 * 参数：
 *   - pPage: 页面对象
 *   - pTitle: 标题文字
 *   - pHint: 底部提示，可为 NULL
 * 返回值：无
 */
static void BikeUi_CreatePageTitle(lv_obj_t *pPage, const char *pTitle, const char *pHint)
{
    lv_obj_t *pLabel;

    if ((NULL == pPage) || (NULL == pTitle))
    {
        return;
    }

    pLabel = lv_label_create(pPage);
    RT_ASSERT(NULL != pLabel);
    lv_label_set_text(pLabel, pTitle);
    lv_obj_set_style_text_color(pLabel, lv_color_hex(0xF5F7FA), LV_PART_MAIN);
    lv_obj_align(pLabel, LV_ALIGN_TOP_MID, 0, 16);

    if (NULL != pHint)
    {
        pLabel = lv_label_create(pPage);
        RT_ASSERT(NULL != pLabel);
        lv_label_set_text(pLabel, pHint);
        lv_obj_set_style_text_color(pLabel, lv_color_hex(0x60758A), LV_PART_MAIN);
        lv_obj_align(pLabel, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    return;
}

/* BikeUi_OnStart: 创建 390 x 450 主数据、定位、地图和总结四页码表。
 * 返回值：无
 */
static void BikeUi_OnStart(void)
{
    BIKE_SETTINGS_SNAPSHOT tSettings;
    BIKE_MAP_ARROW_THEME eArrowTheme;
    lv_obj_t *pDashboardPage;
    lv_obj_t *pLocationPage;
    lv_obj_t *pMapPage;
    lv_obj_t *pMapAttribution;
    lv_obj_t *pSpeedPanel;
    lv_obj_t *pMetricPanel;
    lv_obj_t *pControlPanel;
    lv_obj_t *pRecordButton;
    lv_obj_t *pMapSportPanel;
    lv_obj_t *pMapSpeedUnit;
    lv_obj_t *pSummaryPage;
    lv_obj_t *pTile;
    uint8_t ucIndex;
    bool bMapSourceReady;

    (void)memset(&l_tBikeUi, 0, sizeof(l_tBikeUi));
    (void)BIKE_STORAGE_RetryTfMount();
    l_tBikeUi.ucMapZoom = BIKE_UI_MAP_ZOOM_DEFAULT;
    l_tBikeUi.eMapArrowTheme = BIKE_MAP_ARROW_THEME_DEFAULT;
    bMapSourceReady = false;
    if (RT_EOK == BIKE_SETTINGS_GetSnapshot(&tSettings))
    {
        if (BIKE_MAP_ParseArrowTheme(tSettings.aArrowTheme, &eArrowTheme))
        {
            l_tBikeUi.eMapArrowTheme = eArrowTheme;
        }
        if (BikeUi_MapApplySource(tSettings.aMapDirectory,
                                  tSettings.aMapExtension))
        {
            l_tBikeUi.bMapUseWgs84 = tSettings.bMapUseWgs84;
            bMapSourceReady = true;
        }
    }
    if (!bMapSourceReady)
    {
        l_tBikeUi.bMapUseWgs84 = BIKE_SETTINGS_DEFAULT_MAP_USE_WGS84;
        (void)BikeUi_MapApplySource(BIKE_SETTINGS_DEFAULT_MAP_DIRECTORY,
                                    BIKE_SETTINGS_DEFAULT_MAP_EXTENSION);
    }
    (void)BikeUi_MapResetDefaultView();
    l_tBikeUi.ePreviousRideMode = BIKE_RIDE_MODE_STOPPED;
    l_tBikeUi.pRoot = lv_tileview_create(lv_scr_act());
    RT_ASSERT(NULL != l_tBikeUi.pRoot);
    lv_obj_set_size(l_tBikeUi.pRoot, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(l_tBikeUi.pRoot, 0, 0);
    lv_obj_set_style_bg_color(l_tBikeUi.pRoot, lv_color_hex(0x091017), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(l_tBikeUi.pRoot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(l_tBikeUi.pRoot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(l_tBikeUi.pRoot, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(l_tBikeUi.pRoot, LV_SCROLLBAR_MODE_OFF);

    pDashboardPage = lv_tileview_add_tile(l_tBikeUi.pRoot, 0U, 0U, LV_DIR_RIGHT);
    pLocationPage = lv_tileview_add_tile(l_tBikeUi.pRoot, 1U, 0U,
                                         LV_DIR_LEFT | LV_DIR_RIGHT);
    pMapPage = lv_tileview_add_tile(l_tBikeUi.pRoot, 2U, 0U,
                                    LV_DIR_LEFT | LV_DIR_RIGHT);
    pSummaryPage = lv_tileview_add_tile(l_tBikeUi.pRoot, 3U, 0U, LV_DIR_LEFT);
    RT_ASSERT((NULL != pDashboardPage) && (NULL != pLocationPage) &&
              (NULL != pMapPage) && (NULL != pSummaryPage));
    BikeUi_SetPageStyle(pDashboardPage);
    BikeUi_SetPageStyle(pLocationPage);
    BikeUi_SetPageStyle(pMapPage);
    BikeUi_SetPageStyle(pSummaryPage);
    lv_obj_set_style_bg_color(pDashboardPage, lv_color_black(), LV_PART_MAIN);

    pSpeedPanel = lv_obj_create(pDashboardPage);
    RT_ASSERT(NULL != pSpeedPanel);
    lv_obj_remove_style_all(pSpeedPanel);
    lv_obj_set_pos(pSpeedPanel, 0, BikeUi_ScaleY(BIKE_UI_XTRACK_TOP_Y));
    lv_obj_set_size(pSpeedPanel, LV_HOR_RES,
                    BikeUi_ScaleY(BIKE_UI_XTRACK_TOP_HEIGHT));
    lv_obj_set_style_bg_color(pSpeedPanel, lv_color_hex(0x333333),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pSpeedPanel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(pSpeedPanel, BikeUi_ScaleX(27), LV_PART_MAIN);
    lv_obj_clear_flag(pSpeedPanel, LV_OBJ_FLAG_SCROLLABLE);

    l_tBikeUi.pSpeedLabel = lv_label_create(pSpeedPanel);
    RT_ASSERT(NULL != l_tBikeUi.pSpeedLabel);
    lv_label_set_text(l_tBikeUi.pSpeedLabel, "00");
    lv_ext_set_local_font(l_tBikeUi.pSpeedLabel, FONT_SUPER,
                          lv_color_white());
    lv_obj_align(l_tBikeUi.pSpeedLabel, LV_ALIGN_TOP_MID, 0,
                 BikeUi_ScaleY(63));

    l_tBikeUi.pSpeedUnit = lv_label_create(pSpeedPanel);
    RT_ASSERT(NULL != l_tBikeUi.pSpeedUnit);
    lv_label_set_text(l_tBikeUi.pSpeedUnit, "km/h --");
    lv_ext_set_local_font(l_tBikeUi.pSpeedUnit, FONT_NORMAL,
                          lv_color_white());
    lv_obj_align_to(l_tBikeUi.pSpeedUnit, l_tBikeUi.pSpeedLabel,
                    LV_ALIGN_OUT_BOTTOM_MID, 0, BikeUi_ScaleY(4));

    l_tBikeUi.pGpsLabel = lv_label_create(pDashboardPage);
    RT_ASSERT(NULL != l_tBikeUi.pGpsLabel);
    lv_label_set_text(l_tBikeUi.pGpsLabel, "SEARCHING S0 A0s");
    lv_obj_set_width(l_tBikeUi.pGpsLabel, BikeUi_ScaleX(108));
    lv_label_set_long_mode(l_tBikeUi.pGpsLabel, LV_LABEL_LONG_DOT);
    lv_ext_set_local_font(l_tBikeUi.pGpsLabel, FONT_SMALL,
                          lv_color_hex(0x45D483));
    lv_obj_align(l_tBikeUi.pGpsLabel, LV_ALIGN_TOP_LEFT,
                 BikeUi_ScaleX(10), BikeUi_ScaleY(5));

    l_tBikeUi.pPowerLabel = lv_label_create(pDashboardPage);
    RT_ASSERT(NULL != l_tBikeUi.pPowerLabel);
    lv_label_set_text(l_tBikeUi.pPowerLabel, "IDLE 0 | PWR --");
    lv_obj_set_width(l_tBikeUi.pPowerLabel, BikeUi_ScaleX(108));
    lv_label_set_long_mode(l_tBikeUi.pPowerLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l_tBikeUi.pPowerLabel, LV_TEXT_ALIGN_RIGHT,
                                LV_PART_MAIN);
    lv_ext_set_local_font(l_tBikeUi.pPowerLabel, FONT_SMALL,
                          lv_color_hex(0xB3B3B3));
    lv_obj_align(l_tBikeUi.pPowerLabel, LV_ALIGN_TOP_RIGHT,
                 -BikeUi_ScaleX(10), BikeUi_ScaleY(5));

    pMetricPanel = lv_obj_create(pDashboardPage);
    RT_ASSERT(NULL != pMetricPanel);
    lv_obj_remove_style_all(pMetricPanel);
    lv_obj_set_pos(pMetricPanel, 0, BikeUi_ScaleY(BIKE_UI_XTRACK_METRIC_Y));
    lv_obj_set_size(pMetricPanel, LV_HOR_RES,
                    BikeUi_ScaleY(BIKE_UI_XTRACK_METRIC_HEIGHT));
    lv_obj_set_style_bg_color(pMetricPanel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pMetricPanel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(pMetricPanel, LV_OBJ_FLAG_SCROLLABLE);

    l_tBikeUi.pAverageLabel = BikeUi_CreateMetric(
        pMetricPanel, "AVG", BikeUi_ScaleX(18), BikeUi_ScaleY(4),
        BikeUi_ScaleX(93), BikeUi_ScaleY(39));
    l_tBikeUi.pTimeLabel = BikeUi_CreateMetric(
        pMetricPanel, "TIME", BikeUi_ScaleX(129), BikeUi_ScaleY(4),
        BikeUi_ScaleX(93), BikeUi_ScaleY(39));
    l_tBikeUi.pDistanceLabel = BikeUi_CreateMetric(
        pMetricPanel, "TRIP", BikeUi_ScaleX(18), BikeUi_ScaleY(47),
        BikeUi_ScaleX(93), BikeUi_ScaleY(39));
    l_tBikeUi.pCaloriesLabel = BikeUi_CreateMetric(
        pMetricPanel, "CALORIE", BikeUi_ScaleX(129), BikeUi_ScaleY(47),
        BikeUi_ScaleX(93), BikeUi_ScaleY(39));
    RT_ASSERT((NULL != l_tBikeUi.pDistanceLabel) &&
              (NULL != l_tBikeUi.pAverageLabel) &&
              (NULL != l_tBikeUi.pTimeLabel) &&
              (NULL != l_tBikeUi.pCaloriesLabel));

    pControlPanel = lv_obj_create(pDashboardPage);
    RT_ASSERT(NULL != pControlPanel);
    lv_obj_remove_style_all(pControlPanel);
    lv_obj_set_pos(pControlPanel, 0,
                   BikeUi_ScaleY(BIKE_UI_XTRACK_CONTROL_Y));
    lv_obj_set_size(pControlPanel, LV_HOR_RES,
                    BikeUi_ScaleY(BIKE_UI_XTRACK_CONTROL_HEIGHT));
    lv_obj_set_style_bg_color(pControlPanel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pControlPanel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(pControlPanel, LV_OBJ_FLAG_SCROLLABLE);

    RT_ASSERT(NULL != BikeUi_CreateDialButton(
                          pControlPanel, LV_SYMBOL_GPS, 40,
                          BikeUi_OpenMapEvent, LV_EVENT_CLICKED));
    l_tBikeUi.pStartLabel = BikeUi_CreateDialButton(
        pControlPanel, LV_SYMBOL_PLAY, 120, BikeUi_StartEvent,
        LV_EVENT_SHORT_CLICKED);
    RT_ASSERT(NULL != l_tBikeUi.pStartLabel);
    pRecordButton = lv_obj_get_parent(l_tBikeUi.pStartLabel);
    RT_ASSERT(NULL != pRecordButton);
    lv_obj_add_event_cb(pRecordButton, BikeUi_StopEvent,
                        LV_EVENT_LONG_PRESSED, NULL);
    RT_ASSERT(NULL != BikeUi_CreateDialButton(
                          pControlPanel, LV_SYMBOL_LIST, 200,
                          BikeUi_OpenInfoEvent, LV_EVENT_CLICKED));

    BikeUi_CreatePageTitle(pLocationPage, "GNSS", "<  SWIPE  >");
    l_tBikeUi.pLocationLabel = lv_label_create(pLocationPage);
    RT_ASSERT(NULL != l_tBikeUi.pLocationLabel);
    lv_obj_set_pos(l_tBikeUi.pLocationLabel, 20, 66);
    lv_obj_set_width(l_tBikeUi.pLocationLabel, 350);
    lv_label_set_long_mode(l_tBikeUi.pLocationLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(l_tBikeUi.pLocationLabel, lv_color_hex(0xDCE6F0),
                                LV_PART_MAIN);
    lv_obj_set_style_text_line_space(l_tBikeUi.pLocationLabel, 4, LV_PART_MAIN);

    l_tBikeUi.pMapContainer = lv_obj_create(pMapPage);
    RT_ASSERT(NULL != l_tBikeUi.pMapContainer);
    lv_obj_set_size(l_tBikeUi.pMapContainer, 768, 768);
    lv_obj_set_style_bg_color(l_tBikeUi.pMapContainer,
                              lv_color_hex(0x101B24), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(l_tBikeUi.pMapContainer, LV_OPA_COVER,
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(l_tBikeUi.pMapContainer, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(l_tBikeUi.pMapContainer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(l_tBikeUi.pMapContainer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(l_tBikeUi.pMapContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(l_tBikeUi.pMapContainer, LV_OBJ_FLAG_CLICKABLE);
    for (ucIndex = 0U; ucIndex < BIKE_UI_MAP_TILE_COUNT; ucIndex++)
    {
        pTile = lv_img_create(l_tBikeUi.pMapContainer);
        RT_ASSERT(NULL != pTile);
        l_tBikeUi.apMapTiles[ucIndex] = pTile;
        lv_obj_add_flag(pTile, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pTile, LV_OBJ_FLAG_CLICKABLE);
    }
    l_tBikeUi.pMapTrackLine = lv_line_create(l_tBikeUi.pMapContainer);
    RT_ASSERT(NULL != l_tBikeUi.pMapTrackLine);
    lv_obj_set_style_line_color(l_tBikeUi.pMapTrackLine,
                                lv_color_hex(0x45D483), LV_PART_MAIN);
    lv_obj_set_style_line_width(l_tBikeUi.pMapTrackLine, 4, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(l_tBikeUi.pMapTrackLine, true,
                                  LV_PART_MAIN);
    lv_obj_add_flag(l_tBikeUi.pMapTrackLine, LV_OBJ_FLAG_HIDDEN);

    l_tBikeUi.pMapMarker = lv_img_create(pMapPage);
    RT_ASSERT(NULL != l_tBikeUi.pMapMarker);
    lv_img_set_src(l_tBikeUi.pMapMarker, &l_tBikeMapMarkerImage);
    lv_img_set_pivot(l_tBikeUi.pMapMarker,
                     BIKE_UI_MAP_MARKER_WIDTH_PX / 2U,
                     BIKE_UI_MAP_MARKER_HEIGHT_PX / 2U);
    lv_img_set_antialias(l_tBikeUi.pMapMarker, true);
    lv_obj_set_pos(l_tBikeUi.pMapMarker, BIKE_UI_MAP_MARKER_X,
                   BIKE_UI_MAP_MARKER_Y);
    (void)BikeUi_MapApplyArrowTheme(l_tBikeUi.eMapArrowTheme);
    lv_obj_add_flag(l_tBikeUi.pMapMarker, LV_OBJ_FLAG_HIDDEN);

    l_tBikeUi.pMapTouchArea = lv_obj_create(pMapPage);
    RT_ASSERT(NULL != l_tBikeUi.pMapTouchArea);
    lv_obj_set_size(l_tBikeUi.pMapTouchArea,
                    LV_HOR_RES - (2 * BIKE_UI_MAP_PAN_EDGE_WIDTH_PX),
                    LV_VER_RES);
    lv_obj_set_pos(l_tBikeUi.pMapTouchArea,
                   BIKE_UI_MAP_PAN_EDGE_WIDTH_PX, 0);
    lv_obj_set_style_bg_opa(l_tBikeUi.pMapTouchArea, LV_OPA_TRANSP,
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(l_tBikeUi.pMapTouchArea, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(l_tBikeUi.pMapTouchArea, 0, LV_PART_MAIN);
    lv_obj_add_flag(l_tBikeUi.pMapTouchArea,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(l_tBikeUi.pMapTouchArea,
                      LV_OBJ_FLAG_SCROLL_ELASTIC |
                      LV_OBJ_FLAG_SCROLL_MOMENTUM |
                      LV_OBJ_FLAG_SCROLL_CHAIN |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(l_tBikeUi.pMapTouchArea, LV_DIR_ALL);
    lv_obj_add_event_cb(l_tBikeUi.pMapTouchArea, BikeUi_MapPanEvent,
                        LV_EVENT_PRESSING, NULL);

    l_tBikeUi.pMapEmptyLabel = lv_label_create(pMapPage);
    RT_ASSERT(NULL != l_tBikeUi.pMapEmptyLabel);
    lv_label_set_text(l_tBikeUi.pMapEmptyLabel, "NO OFFLINE TILES");
    lv_obj_set_width(l_tBikeUi.pMapEmptyLabel, 340);
    lv_label_set_long_mode(l_tBikeUi.pMapEmptyLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l_tBikeUi.pMapEmptyLabel, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(l_tBikeUi.pMapEmptyLabel,
                                lv_color_hex(0xDCE6F0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(l_tBikeUi.pMapEmptyLabel,
                              lv_color_hex(0x223241), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(l_tBikeUi.pMapEmptyLabel, LV_OPA_80,
                            LV_PART_MAIN);
    lv_obj_set_style_border_color(l_tBikeUi.pMapEmptyLabel,
                                  lv_color_hex(0x526779), LV_PART_MAIN);
    lv_obj_set_style_border_width(l_tBikeUi.pMapEmptyLabel, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(l_tBikeUi.pMapEmptyLabel, 14, LV_PART_MAIN);
    lv_obj_set_style_radius(l_tBikeUi.pMapEmptyLabel, 10, LV_PART_MAIN);
    lv_obj_align(l_tBikeUi.pMapEmptyLabel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(l_tBikeUi.pMapEmptyLabel, LV_OBJ_FLAG_HIDDEN);

    l_tBikeUi.pMapStatusLabel = lv_label_create(pMapPage);
    RT_ASSERT(NULL != l_tBikeUi.pMapStatusLabel);
    lv_label_set_text_fmt(l_tBikeUi.pMapStatusLabel,
                          "MAP GCJ Z%u  BROWSE",
                          (unsigned int)l_tBikeUi.ucMapZoom);
    lv_obj_set_style_text_color(l_tBikeUi.pMapStatusLabel,
                                lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(l_tBikeUi.pMapStatusLabel,
                              lv_color_hex(0x091017), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(l_tBikeUi.pMapStatusLabel, LV_OPA_70,
                            LV_PART_MAIN);
    lv_obj_set_style_pad_all(l_tBikeUi.pMapStatusLabel, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(l_tBikeUi.pMapStatusLabel, 8, LV_PART_MAIN);
    lv_obj_align(l_tBikeUi.pMapStatusLabel, LV_ALIGN_TOP_MID, 0, 12);

    pMapSportPanel = lv_obj_create(pMapPage);
    RT_ASSERT(NULL != pMapSportPanel);
    lv_obj_remove_style_all(pMapSportPanel);
    lv_obj_set_size(pMapSportPanel, BikeUi_ScaleX(159),
                    BikeUi_ScaleY(66));
    lv_obj_align(pMapSportPanel, LV_ALIGN_BOTTOM_LEFT,
                 BikeUi_ScaleX(-10), BikeUi_ScaleY(10));
    lv_obj_set_style_bg_color(pMapSportPanel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pMapSportPanel, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(pMapSportPanel, BikeUi_ScaleX(10), LV_PART_MAIN);
    lv_obj_clear_flag(pMapSportPanel, LV_OBJ_FLAG_SCROLLABLE);

    l_tBikeUi.pMapSpeedLabel = lv_label_create(pMapSportPanel);
    RT_ASSERT(NULL != l_tBikeUi.pMapSpeedLabel);
    lv_label_set_text(l_tBikeUi.pMapSpeedLabel, "00");
    lv_ext_set_local_font(l_tBikeUi.pMapSpeedLabel, FONT_HUGE,
                          lv_color_white());
    lv_obj_align(l_tBikeUi.pMapSpeedLabel, LV_ALIGN_LEFT_MID,
                 BikeUi_ScaleX(20), BikeUi_ScaleY(-10));

    pMapSpeedUnit = lv_label_create(pMapSportPanel);
    RT_ASSERT(NULL != pMapSpeedUnit);
    lv_label_set_text(pMapSpeedUnit, "km/h");
    lv_ext_set_local_font(pMapSpeedUnit, FONT_SMALL, lv_color_white());
    lv_obj_align_to(pMapSpeedUnit, l_tBikeUi.pMapSpeedLabel,
                    LV_ALIGN_OUT_BOTTOM_MID, 0, BikeUi_ScaleY(2));

    l_tBikeUi.pMapDistanceLabel = lv_label_create(pMapSportPanel);
    RT_ASSERT(NULL != l_tBikeUi.pMapDistanceLabel);
    lv_label_set_text(l_tBikeUi.pMapDistanceLabel, "0.00 km");
    lv_ext_set_local_font(l_tBikeUi.pMapDistanceLabel, FONT_NORMAL,
                          lv_color_white());
    lv_obj_align(l_tBikeUi.pMapDistanceLabel, LV_ALIGN_TOP_RIGHT,
                 -BikeUi_ScaleX(8), BikeUi_ScaleY(10));

    l_tBikeUi.pMapTimeLabel = lv_label_create(pMapSportPanel);
    RT_ASSERT(NULL != l_tBikeUi.pMapTimeLabel);
    lv_label_set_text(l_tBikeUi.pMapTimeLabel, "00:00:00");
    lv_ext_set_local_font(l_tBikeUi.pMapTimeLabel, FONT_NORMAL,
                          lv_color_white());
    lv_obj_align(l_tBikeUi.pMapTimeLabel, LV_ALIGN_TOP_RIGHT,
                 -BikeUi_ScaleX(8), BikeUi_ScaleY(34));

    pMapAttribution = lv_label_create(pMapPage);
    RT_ASSERT(NULL != pMapAttribution);
    lv_label_set_text(pMapAttribution, "Map data: OpenStreetMap");
    lv_obj_set_style_text_color(pMapAttribution,
                                lv_color_hex(0xDCE6F0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pMapAttribution,
                              lv_color_hex(0x091017), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pMapAttribution, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pMapAttribution, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(pMapAttribution, 4, LV_PART_MAIN);
    lv_obj_align(pMapAttribution, LV_ALIGN_BOTTOM_MID, 0, -8);

    RT_ASSERT(NULL != BikeUi_CreateMapButton(pMapPage, "-", 322, 122,
                                              BikeUi_MapZoomOutEvent));
    RT_ASSERT(NULL != BikeUi_CreateMapButton(pMapPage, "+", 322, 188,
                                              BikeUi_MapZoomInEvent));

    BikeUi_CreatePageTitle(pSummaryPage, "RIDE SUMMARY", NULL);
    l_tBikeUi.pSummaryLabel = lv_label_create(pSummaryPage);
    RT_ASSERT(NULL != l_tBikeUi.pSummaryLabel);
    lv_obj_set_pos(l_tBikeUi.pSummaryLabel, 20, 60);
    lv_obj_set_width(l_tBikeUi.pSummaryLabel, 350);
    lv_label_set_long_mode(l_tBikeUi.pSummaryLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(l_tBikeUi.pSummaryLabel, lv_color_hex(0xDCE6F0),
                                LV_PART_MAIN);
    lv_obj_set_style_text_line_space(l_tBikeUi.pSummaryLabel, 2, LV_PART_MAIN);
    RT_ASSERT(NULL != BikeUi_CreateButton(pSummaryPage, "SAVE", 16,
                                          lv_color_hex(0x168B4D),
                                          BikeUi_StopEvent));
    RT_ASSERT(NULL != BikeUi_CreateButton(pSummaryPage, "DISCARD", 204,
                                          lv_color_hex(0xA9323A),
                                          BikeUi_DiscardEvent));

    l_tBikeUi.pTimer = lv_timer_create(BikeUi_TimerCallback, BIKE_UI_REFRESH_PERIOD_MS, NULL);
    RT_ASSERT(NULL != l_tBikeUi.pTimer);
    BikeUi_Update();
    (void)BIKE_SOUND_Request(BIKE_SOUND_EVENT_STARTUP);

    return;
}

/* BikeUi_OnStop: 删除 UI 自有定时器并清理页面引用。
 * 返回值：无
 */
static void BikeUi_OnStop(void)
{
    if (NULL != l_tBikeUi.pTimer)
    {
        lv_timer_del(l_tBikeUi.pTimer);
        l_tBikeUi.pTimer = NULL;
    }
    if (NULL != l_tBikeUi.pRoot)
    {
        lv_obj_del(l_tBikeUi.pRoot);
    }
    (void)memset(&l_tBikeUi, 0, sizeof(l_tBikeUi));

    return;
}

/* BikeUi_MessageHandler: 处理 GUI app 生命周期消息。
 * 参数：
 *   - eMessage: 生命周期消息
 *   - pParameter: 未使用
 * 返回值：无
 */
static void BikeUi_MessageHandler(gui_app_msg_type_t eMessage, void *pParameter)
{
    (void)pParameter;
    switch (eMessage)
    {
    case GUI_APP_MSG_ONSTART:
        BikeUi_OnStart();
        break;

    case GUI_APP_MSG_ONRESUME:
        BikeUi_Update();
        break;

    case GUI_APP_MSG_ONPAUSE:
        break;

    case GUI_APP_MSG_ONSTOP:
        BikeUi_OnStop();
        break;

    default:
        break;
    }

    return;
}

/* BikeUi_AppMain: 注册码表应用消息处理器。
 * 参数：
 *   - tIntent: GUI app 启动参数
 * 返回值：成功返回 0
 */
static int BikeUi_AppMain(intent_t tIntent)
{
    (void)tIntent;
    gui_app_regist_msg_handler(APP_ID, BikeUi_MessageHandler);

    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(bike_computer),
                   LV_EXT_IMG_GET(img_bike_computer), APP_ID,
                   BikeUi_AppMain);
