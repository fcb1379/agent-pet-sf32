#include "bike_settings.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <rtthread.h>

#ifdef BSP_SHARE_PREFS
#include "share_prefs.h"
#endif

#define LOG_TAG "bike.settings"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#ifndef CONFIG_BIKE_TIMEZONE_MINUTES
#define CONFIG_BIKE_TIMEZONE_MINUTES (480)
#endif

#define BIKE_SETTINGS_TIMEZONE_MIN (-720)
#define BIKE_SETTINGS_TIMEZONE_MAX (840)
#define BIKE_SETTINGS_WHEEL_MIN_MM (500)
#define BIKE_SETTINGS_WHEEL_MAX_MM (4000)
#define BIKE_SETTINGS_RIDER_WEIGHT_MIN_KG (30)
#define BIKE_SETTINGS_RIDER_WEIGHT_MAX_KG (250)
#define BIKE_SETTINGS_AUTO_PAUSE_MIN_CENTI_KPH (50)
#define BIKE_SETTINGS_AUTO_PAUSE_MAX_CENTI_KPH (2000)
#define BIKE_SETTINGS_BRIGHTNESS_MIN_PERCENT (1)
#define BIKE_SETTINGS_BRIGHTNESS_MAX_PERCENT (100)
#define BIKE_SETTINGS_SCREEN_TIMEOUT_MIN_SECONDS (0)
#define BIKE_SETTINGS_SCREEN_TIMEOUT_MAX_SECONDS (3600)

#define BIKE_SETTINGS_KEY_TIMEZONE "timezone"
#define BIKE_SETTINGS_KEY_WHEEL "wheel_mm"
#define BIKE_SETTINGS_KEY_RIDER_WEIGHT "weight_kg"
#define BIKE_SETTINGS_KEY_AUTO_PAUSE "auto_pause"
#define BIKE_SETTINGS_KEY_AUTO_THRESHOLD "auto_kph"
#define BIKE_SETTINGS_KEY_BRIGHTNESS "brightness"
#define BIKE_SETTINGS_KEY_SCREEN_TIMEOUT "screen_sec"
#define BIKE_SETTINGS_KEY_MAP_WGS84 "map_wgs84"
#define BIKE_SETTINGS_KEY_MAP_DIRECTORY "map_dir"
#define BIKE_SETTINGS_KEY_MAP_EXTENSION "map_ext"
#define BIKE_SETTINGS_KEY_SOUND_ENABLED "sound_en"

/* l_aBikeSettingsPrefName: FlashDB 命名空间，数组固定 32 字节以规避 SDK
 * share_prefs_open 固定读取 31 字节时越过短字符串结尾。
 */
static const char l_aBikeSettingsPrefName[32] = "bike_computer_runtime_pref_v1";

/* l_tBikeSettings: 当前码表设置，所有读写均受 l_tBikeSettingsMutex 保护。 */
static BIKE_SETTINGS_SNAPSHOT l_tBikeSettings;

/* l_tBikeSettingsMutex: 设置快照和持久化写入的互斥锁。 */
static struct rt_mutex l_tBikeSettingsMutex;

/* l_bBikeSettingsReady: 设置模块已完成互斥锁和默认值初始化。 */
static bool l_bBikeSettingsReady;

#ifdef BSP_SHARE_PREFS
/* l_pBikeSettingsPrefs: 码表 FlashDB 偏好句柄，打开失败时为 NULL。 */
static share_prefs_t *l_pBikeSettingsPrefs;
#endif

/* BikeSettings_Clamp: 将输入约束到指定闭区间。
 * 参数：
 *   - lValue: 输入值
 *   - lMinimum: 最小值
 *   - lMaximum: 最大值
 * 返回值：约束后的值
 */
static int32_t BikeSettings_Clamp(int32_t lValue, int32_t lMinimum, int32_t lMaximum)
{
    if (lMinimum > lValue)
    {
        return lMinimum;
    }
    if (lMaximum < lValue)
    {
        return lMaximum;
    }

    return lValue;
}

/* BikeSettings_Unlock: 释放设置互斥锁并记录异常。
 * 返回值：无
 */
static void BikeSettings_Unlock(void)
{
    rt_err_t eResult;

    eResult = rt_mutex_release(&l_tBikeSettingsMutex);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex release failed: %d", eResult);
    }

    return;
}

/* BikeSettings_SaveLocked: 在持锁状态下写入一个整数参数。
 * 参数：
 *   - pKey: FlashDB 键名
 *   - lValue: 待保存数值
 * 返回值：成功返回 RT_EOK，存储不可用或写失败时返回错误码
 */
static rt_err_t BikeSettings_SaveLocked(const char *pKey, int32_t lValue)
{
    if (NULL == pKey)
    {
        return -RT_EINVAL;
    }

#ifdef BSP_SHARE_PREFS
    if (NULL != l_pBikeSettingsPrefs)
    {
        return share_prefs_set_int(l_pBikeSettingsPrefs, pKey, lValue);
    }
#else
    (void)lValue;
#endif

    return -RT_ERROR;
}

/* BikeSettings_ParseInt32: 安全解析 shell 十进制参数。
 * 参数：
 *   - pText: 输入字符串
 *   - pValue: 输出整数
 * 返回值：完整且未溢出时返回 true，否则返回 false
 */
static bool BikeSettings_ParseInt32(const char *pText, int32_t *pValue)
{
    char *pEnd;
    long lParsed;

    if ((NULL == pText) || (NULL == pValue) || ('\0' == pText[0]))
    {
        return false;
    }

    pEnd = NULL;
    lParsed = strtol(pText, &pEnd, 10);
    if ((pText == pEnd) || ('\0' != *pEnd) || (INT32_MIN > lParsed) ||
        (INT32_MAX < lParsed))
    {
        return false;
    }

    *pValue = (int32_t)lParsed;
    return true;
}

/* BIKE_SETTINGS_Init: 初始化默认参数并从产品 prefdb 恢复持久化值。
 * 返回值：运行参数可用返回 RT_EOK，互斥锁初始化失败返回错误码
 */
rt_err_t BIKE_SETTINGS_Init(void)
{
#ifdef BSP_SHARE_PREFS
    char aStoredMapDirectory[BIKE_STORAGE_MAP_DIRECTORY_MAX];
    char aStoredMapExtension[BIKE_MAP_EXTENSION_MAX];
    int32_t lMapDirectoryLength;
    int32_t lMapExtensionLength;
#endif
    rt_err_t eResult;

    if (l_bBikeSettingsReady)
    {
        return RT_EOK;
    }

    eResult = rt_mutex_init(&l_tBikeSettingsMutex, "bikeset", RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex init failed: %d", eResult);
        return eResult;
    }

    (void)memset(&l_tBikeSettings, 0, sizeof(l_tBikeSettings));
    l_tBikeSettings.sTimeZoneMinutes = (int16_t)BikeSettings_Clamp(
        CONFIG_BIKE_TIMEZONE_MINUTES, BIKE_SETTINGS_TIMEZONE_MIN,
        BIKE_SETTINGS_TIMEZONE_MAX);
    l_tBikeSettings.usWheelCircumferenceMm = BIKE_SETTINGS_DEFAULT_WHEEL_MM;
    l_tBikeSettings.ucRiderWeightKg = BIKE_SETTINGS_DEFAULT_RIDER_WEIGHT_KG;
    l_tBikeSettings.usAutoPauseCentiKph = BIKE_SETTINGS_DEFAULT_AUTO_PAUSE_CENTI_KPH;
    l_tBikeSettings.usScreenTimeoutSeconds = BIKE_SETTINGS_DEFAULT_SCREEN_TIMEOUT_SECONDS;
    l_tBikeSettings.ucBrightnessPercent = BIKE_SETTINGS_DEFAULT_BRIGHTNESS_PERCENT;
    l_tBikeSettings.bAutoPauseEnabled = false;
    l_tBikeSettings.bMapUseWgs84 = BIKE_SETTINGS_DEFAULT_MAP_USE_WGS84;
    l_tBikeSettings.bSoundEnabled = BIKE_SETTINGS_DEFAULT_SOUND_ENABLED;
    (void)memcpy(l_tBikeSettings.aMapDirectory,
                 BIKE_SETTINGS_DEFAULT_MAP_DIRECTORY,
                 sizeof(BIKE_SETTINGS_DEFAULT_MAP_DIRECTORY));
    (void)memcpy(l_tBikeSettings.aMapExtension,
                 BIKE_SETTINGS_DEFAULT_MAP_EXTENSION,
                 sizeof(BIKE_SETTINGS_DEFAULT_MAP_EXTENSION));

#ifdef BSP_SHARE_PREFS
    l_pBikeSettingsPrefs = share_prefs_open(l_aBikeSettingsPrefName,
                                            SHAREPREFS_MODE_PRIVATE);
    if (NULL != l_pBikeSettingsPrefs)
    {
        l_tBikeSettings.bStorageReady = true;
        l_tBikeSettings.sTimeZoneMinutes = (int16_t)BikeSettings_Clamp(
            share_prefs_get_int(l_pBikeSettingsPrefs, BIKE_SETTINGS_KEY_TIMEZONE,
                                l_tBikeSettings.sTimeZoneMinutes),
            BIKE_SETTINGS_TIMEZONE_MIN, BIKE_SETTINGS_TIMEZONE_MAX);
        l_tBikeSettings.usWheelCircumferenceMm = (uint16_t)BikeSettings_Clamp(
            share_prefs_get_int(l_pBikeSettingsPrefs, BIKE_SETTINGS_KEY_WHEEL,
                                l_tBikeSettings.usWheelCircumferenceMm),
            BIKE_SETTINGS_WHEEL_MIN_MM, BIKE_SETTINGS_WHEEL_MAX_MM);
        l_tBikeSettings.ucRiderWeightKg = (uint8_t)BikeSettings_Clamp(
            share_prefs_get_int(l_pBikeSettingsPrefs,
                                BIKE_SETTINGS_KEY_RIDER_WEIGHT,
                                l_tBikeSettings.ucRiderWeightKg),
            BIKE_SETTINGS_RIDER_WEIGHT_MIN_KG,
            BIKE_SETTINGS_RIDER_WEIGHT_MAX_KG);
        l_tBikeSettings.bAutoPauseEnabled = (0 != share_prefs_get_int(
            l_pBikeSettingsPrefs, BIKE_SETTINGS_KEY_AUTO_PAUSE, 0));
        l_tBikeSettings.usAutoPauseCentiKph = (uint16_t)BikeSettings_Clamp(
            share_prefs_get_int(l_pBikeSettingsPrefs,
                                BIKE_SETTINGS_KEY_AUTO_THRESHOLD,
                                l_tBikeSettings.usAutoPauseCentiKph),
            BIKE_SETTINGS_AUTO_PAUSE_MIN_CENTI_KPH,
            BIKE_SETTINGS_AUTO_PAUSE_MAX_CENTI_KPH);
        l_tBikeSettings.ucBrightnessPercent = (uint8_t)BikeSettings_Clamp(
            share_prefs_get_int(l_pBikeSettingsPrefs, BIKE_SETTINGS_KEY_BRIGHTNESS,
                                l_tBikeSettings.ucBrightnessPercent),
            BIKE_SETTINGS_BRIGHTNESS_MIN_PERCENT,
            BIKE_SETTINGS_BRIGHTNESS_MAX_PERCENT);
        l_tBikeSettings.usScreenTimeoutSeconds = (uint16_t)BikeSettings_Clamp(
            share_prefs_get_int(l_pBikeSettingsPrefs,
                                BIKE_SETTINGS_KEY_SCREEN_TIMEOUT,
                                l_tBikeSettings.usScreenTimeoutSeconds),
            BIKE_SETTINGS_SCREEN_TIMEOUT_MIN_SECONDS,
            BIKE_SETTINGS_SCREEN_TIMEOUT_MAX_SECONDS);
        l_tBikeSettings.bMapUseWgs84 = (0 != share_prefs_get_int(
            l_pBikeSettingsPrefs, BIKE_SETTINGS_KEY_MAP_WGS84,
            l_tBikeSettings.bMapUseWgs84 ? 1 : 0));
        l_tBikeSettings.bSoundEnabled = (0 != share_prefs_get_int(
            l_pBikeSettingsPrefs, BIKE_SETTINGS_KEY_SOUND_ENABLED,
            l_tBikeSettings.bSoundEnabled ? 1 : 0));
        lMapDirectoryLength = share_prefs_get_string(
            l_pBikeSettingsPrefs, BIKE_SETTINGS_KEY_MAP_DIRECTORY,
            aStoredMapDirectory, sizeof(aStoredMapDirectory) - 1U);
        if ((0 < lMapDirectoryLength) &&
            ((int32_t)sizeof(aStoredMapDirectory) > lMapDirectoryLength))
        {
            aStoredMapDirectory[lMapDirectoryLength] = '\0';
            if (BIKE_STORAGE_IsMapDirectoryValid(aStoredMapDirectory))
            {
                (void)memset(l_tBikeSettings.aMapDirectory, 0,
                             sizeof(l_tBikeSettings.aMapDirectory));
                (void)memcpy(l_tBikeSettings.aMapDirectory,
                             aStoredMapDirectory,
                             (size_t)lMapDirectoryLength + 1U);
            }
        }
        lMapExtensionLength = share_prefs_get_string(
            l_pBikeSettingsPrefs, BIKE_SETTINGS_KEY_MAP_EXTENSION,
            aStoredMapExtension, sizeof(aStoredMapExtension) - 1U);
        if ((0 < lMapExtensionLength) &&
            ((int32_t)sizeof(aStoredMapExtension) > lMapExtensionLength))
        {
            aStoredMapExtension[lMapExtensionLength] = '\0';
            if (BIKE_MAP_IsExtensionValid(aStoredMapExtension))
            {
                (void)memset(l_tBikeSettings.aMapExtension, 0,
                             sizeof(l_tBikeSettings.aMapExtension));
                (void)memcpy(l_tBikeSettings.aMapExtension,
                             aStoredMapExtension,
                             (size_t)lMapExtensionLength + 1U);
            }
        }
    }
#endif

    l_bBikeSettingsReady = true;
    LOG_I("ready storage=%u timezone=%d wheel=%u weight=%u auto=%u/%u display=%u/%u sound=%u map=%u/%s/*.%s",
          l_tBikeSettings.bStorageReady, l_tBikeSettings.sTimeZoneMinutes,
          l_tBikeSettings.usWheelCircumferenceMm,
          l_tBikeSettings.ucRiderWeightKg,
          l_tBikeSettings.bAutoPauseEnabled,
          l_tBikeSettings.usAutoPauseCentiKph,
          l_tBikeSettings.ucBrightnessPercent,
          l_tBikeSettings.usScreenTimeoutSeconds,
          l_tBikeSettings.bSoundEnabled,
          l_tBikeSettings.bMapUseWgs84,
          l_tBikeSettings.aMapDirectory,
          l_tBikeSettings.aMapExtension);

    return RT_EOK;
}

/* BIKE_SETTINGS_GetSnapshot: 获取线程安全的运行参数快照。
 * 参数：
 *   - pSnapshot: 输出快照
 * 返回值：成功返回 RT_EOK，否则返回错误码
 */
rt_err_t BIKE_SETTINGS_GetSnapshot(BIKE_SETTINGS_SNAPSHOT *pSnapshot)
{
    rt_err_t eResult;

    if (NULL == pSnapshot)
    {
        return -RT_EINVAL;
    }
    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeSettingsMutex, RT_WAITING_NO);
    if (RT_EOK == eResult)
    {
        *pSnapshot = l_tBikeSettings;
        BikeSettings_Unlock();
    }

    return eResult;
}

/* BIKE_SETTINGS_SetTimeZoneMinutes: 更新并保存 UTC 时区偏移。
 * 参数：
 *   - sMinutes: 时区偏移，范围 -720~840 分钟
 * 返回值：保存成功返回 RT_EOK，参数或存储错误返回错误码
 */
rt_err_t BIKE_SETTINGS_SetTimeZoneMinutes(int16_t sMinutes)
{
    rt_err_t eResult;

    if ((BIKE_SETTINGS_TIMEZONE_MIN > sMinutes) ||
        (BIKE_SETTINGS_TIMEZONE_MAX < sMinutes))
    {
        return -RT_EINVAL;
    }
    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeSettingsMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
        eResult = BikeSettings_SaveLocked(BIKE_SETTINGS_KEY_TIMEZONE, sMinutes);
        if (RT_EOK == eResult)
        {
            l_tBikeSettings.sTimeZoneMinutes = sMinutes;
        }
        BikeSettings_Unlock();
    }

    return eResult;
}

/* BIKE_SETTINGS_SetWheelCircumference: 更新并保存车轮周长预留参数。
 * 参数：
 *   - usMillimeters: 周长，范围 500~4000 毫米
 * 返回值：保存成功返回 RT_EOK，参数或存储错误返回错误码
 */
rt_err_t BIKE_SETTINGS_SetWheelCircumference(uint16_t usMillimeters)
{
    rt_err_t eResult;

    if ((BIKE_SETTINGS_WHEEL_MIN_MM > usMillimeters) ||
        (BIKE_SETTINGS_WHEEL_MAX_MM < usMillimeters))
    {
        return -RT_EINVAL;
    }
    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeSettingsMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
        eResult = BikeSettings_SaveLocked(BIKE_SETTINGS_KEY_WHEEL,
                                          usMillimeters);
        if (RT_EOK == eResult)
        {
            l_tBikeSettings.usWheelCircumferenceMm = usMillimeters;
        }
        BikeSettings_Unlock();
    }

    return eResult;
}

/* BIKE_SETTINGS_SetRiderWeight: 更新并保存卡路里估算使用的骑手体重。
 * 参数：
 *   - ucWeightKg: 骑手体重，范围 30~250 kg
 * 返回值：保存成功返回 RT_EOK，参数或存储错误返回错误码
 */
rt_err_t BIKE_SETTINGS_SetRiderWeight(uint8_t ucWeightKg)
{
    rt_err_t eResult;

    if ((BIKE_SETTINGS_RIDER_WEIGHT_MIN_KG > ucWeightKg) ||
        (BIKE_SETTINGS_RIDER_WEIGHT_MAX_KG < ucWeightKg))
    {
        return -RT_EINVAL;
    }
    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeSettingsMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
        eResult = BikeSettings_SaveLocked(BIKE_SETTINGS_KEY_RIDER_WEIGHT,
                                          ucWeightKg);
        if (RT_EOK == eResult)
        {
            l_tBikeSettings.ucRiderWeightKg = ucWeightKg;
        }
        BikeSettings_Unlock();
    }

    return eResult;
}

/* BIKE_SETTINGS_SetAutoPause: 更新并保存自动暂停策略。
 * 参数：
 *   - bEnabled: 是否启用
 *   - usThresholdCentiKph: 速度阈值，范围 50~2000，单位 0.01 km/h
 * 返回值：两项均保存成功返回 RT_EOK，否则返回错误码
 */
rt_err_t BIKE_SETTINGS_SetAutoPause(bool bEnabled, uint16_t usThresholdCentiKph)
{
    rt_err_t eResult;
    rt_err_t eRollbackResult;
    bool bPreviousEnabled;

    if ((BIKE_SETTINGS_AUTO_PAUSE_MIN_CENTI_KPH > usThresholdCentiKph) ||
        (BIKE_SETTINGS_AUTO_PAUSE_MAX_CENTI_KPH < usThresholdCentiKph))
    {
        return -RT_EINVAL;
    }
    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeSettingsMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
        bPreviousEnabled = l_tBikeSettings.bAutoPauseEnabled;
        eResult = BikeSettings_SaveLocked(BIKE_SETTINGS_KEY_AUTO_PAUSE,
                                          bEnabled ? 1 : 0);
        if (RT_EOK == eResult)
        {
            eResult = BikeSettings_SaveLocked(BIKE_SETTINGS_KEY_AUTO_THRESHOLD,
                                              usThresholdCentiKph);
            if (RT_EOK != eResult)
            {
                eRollbackResult = BikeSettings_SaveLocked(
                    BIKE_SETTINGS_KEY_AUTO_PAUSE,
                    bPreviousEnabled ? 1 : 0);
                if (RT_EOK != eRollbackResult)
                {
                    LOG_E("autopause rollback failed: %d", eRollbackResult);
                }
            }
        }
        if (RT_EOK == eResult)
        {
            l_tBikeSettings.bAutoPauseEnabled = bEnabled;
            l_tBikeSettings.usAutoPauseCentiKph = usThresholdCentiKph;
        }
        BikeSettings_Unlock();
    }

    return eResult;
}

/* BIKE_SETTINGS_SetDisplay: 更新并保存显示策略。
 * 参数：
 *   - ucBrightnessPercent: 亮度，范围 1~100%
 *   - usTimeoutSeconds: 自动熄屏时间，范围 0~3600 秒
 * 返回值：两项均保存成功返回 RT_EOK，否则返回错误码
 */
rt_err_t BIKE_SETTINGS_SetDisplay(uint8_t ucBrightnessPercent,
                                  uint16_t usTimeoutSeconds)
{
    rt_err_t eResult;
    rt_err_t eRollbackResult;
    uint8_t ucPreviousBrightness;

    if ((BIKE_SETTINGS_BRIGHTNESS_MIN_PERCENT > ucBrightnessPercent) ||
        (BIKE_SETTINGS_BRIGHTNESS_MAX_PERCENT < ucBrightnessPercent) ||
        (BIKE_SETTINGS_SCREEN_TIMEOUT_MAX_SECONDS < usTimeoutSeconds))
    {
        return -RT_EINVAL;
    }
    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeSettingsMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
        ucPreviousBrightness = l_tBikeSettings.ucBrightnessPercent;
        eResult = BikeSettings_SaveLocked(BIKE_SETTINGS_KEY_BRIGHTNESS,
                                          ucBrightnessPercent);
        if (RT_EOK == eResult)
        {
            eResult = BikeSettings_SaveLocked(BIKE_SETTINGS_KEY_SCREEN_TIMEOUT,
                                              usTimeoutSeconds);
            if (RT_EOK != eResult)
            {
                eRollbackResult = BikeSettings_SaveLocked(
                    BIKE_SETTINGS_KEY_BRIGHTNESS, ucPreviousBrightness);
                if (RT_EOK != eRollbackResult)
                {
                    LOG_E("display rollback failed: %d", eRollbackResult);
                }
            }
        }
        if (RT_EOK == eResult)
        {
            l_tBikeSettings.ucBrightnessPercent = ucBrightnessPercent;
            l_tBikeSettings.usScreenTimeoutSeconds = usTimeoutSeconds;
        }
        BikeSettings_Unlock();
    }

    return eResult;
}

/* BIKE_SETTINGS_SetMapUseWgs84: 更新并保存离线瓦片坐标系。
 * 参数：
 *   - bUseWgs84: true 使用 WGS-84，false 使用 GCJ-02
 * 返回值：保存成功返回 RT_EOK，存储错误返回错误码
 */
rt_err_t BIKE_SETTINGS_SetMapUseWgs84(bool bUseWgs84)
{
    rt_err_t eResult;

    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeSettingsMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
        eResult = BikeSettings_SaveLocked(BIKE_SETTINGS_KEY_MAP_WGS84,
                                          bUseWgs84 ? 1 : 0);
        if (RT_EOK == eResult)
        {
            l_tBikeSettings.bMapUseWgs84 = bUseWgs84;
        }
        BikeSettings_Unlock();
    }

    return eResult;
}

/* BIKE_SETTINGS_SetMapDirectory: 更新并保存当前介质内的地图逻辑路径。
 * 参数：
 *   - pDirectory: 以斜杠开头的安全逻辑绝对路径
 * 返回值：保存成功返回 RT_EOK，参数或存储错误返回错误码
 */
rt_err_t BIKE_SETTINGS_SetMapDirectory(const char *pDirectory)
{
    size_t ulLength;
    rt_err_t eResult;

    if (!BIKE_STORAGE_IsMapDirectoryValid(pDirectory))
    {
        return -RT_EINVAL;
    }
    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    ulLength = strlen(pDirectory);
    eResult = rt_mutex_take(&l_tBikeSettingsMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
#ifdef BSP_SHARE_PREFS
        if (NULL != l_pBikeSettingsPrefs)
        {
            eResult = share_prefs_set_string(l_pBikeSettingsPrefs,
                                             BIKE_SETTINGS_KEY_MAP_DIRECTORY,
                                             pDirectory);
        }
        else
#endif
        {
            eResult = -RT_ERROR;
        }
        if (RT_EOK == eResult)
        {
            (void)memset(l_tBikeSettings.aMapDirectory, 0,
                         sizeof(l_tBikeSettings.aMapDirectory));
            (void)memcpy(l_tBikeSettings.aMapDirectory, pDirectory,
                         ulLength + 1U);
        }
        BikeSettings_Unlock();
    }

    return eResult;
}

/* BIKE_SETTINGS_SetMapExtension: 更新并保存瓦片文件扩展名。
 * 参数：
 *   - pExtension: 不含点号的 ASCII 字母或数字扩展名
 * 返回值：保存成功返回 RT_EOK，参数或存储错误返回错误码
 */
rt_err_t BIKE_SETTINGS_SetMapExtension(const char *pExtension)
{
    size_t ulLength;
    rt_err_t eResult;

    if (!BIKE_MAP_IsExtensionValid(pExtension))
    {
        return -RT_EINVAL;
    }
    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    ulLength = strlen(pExtension);
    eResult = rt_mutex_take(&l_tBikeSettingsMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
#ifdef BSP_SHARE_PREFS
        if (NULL != l_pBikeSettingsPrefs)
        {
            eResult = share_prefs_set_string(l_pBikeSettingsPrefs,
                                             BIKE_SETTINGS_KEY_MAP_EXTENSION,
                                             pExtension);
        }
        else
#endif
        {
            eResult = -RT_ERROR;
        }
        if (RT_EOK == eResult)
        {
            (void)memset(l_tBikeSettings.aMapExtension, 0,
                         sizeof(l_tBikeSettings.aMapExtension));
            (void)memcpy(l_tBikeSettings.aMapExtension, pExtension,
                         ulLength + 1U);
        }
        BikeSettings_Unlock();
    }

    return eResult;
}

/* BIKE_SETTINGS_SetSoundEnabled: 更新并保存事件提示音开关。
 * 参数：
 *   - bEnabled: true 开启提示音，false 静音
 * 返回值：保存成功返回 RT_EOK，存储错误返回错误码
 */
rt_err_t BIKE_SETTINGS_SetSoundEnabled(bool bEnabled)
{
    rt_err_t eResult;

    eResult = BIKE_SETTINGS_Init();
    if (RT_EOK != eResult)
    {
        return eResult;
    }
    eResult = rt_mutex_take(&l_tBikeSettingsMutex, RT_WAITING_FOREVER);
    if (RT_EOK == eResult)
    {
        eResult = BikeSettings_SaveLocked(BIKE_SETTINGS_KEY_SOUND_ENABLED,
                                          bEnabled ? 1 : 0);
        if (RT_EOK == eResult)
        {
            l_tBikeSettings.bSoundEnabled = bEnabled;
        }
        BikeSettings_Unlock();
    }

    return eResult;
}

/* BikeSettings_Command: 查询或修改码表持久化设置。
 * 参数：
 *   - lArgumentCount: shell 参数数量
 *   - pArguments: shell 参数数组
 * 返回值：无
 */
static void BikeSettings_Command(int lArgumentCount, char **pArguments)
{
    BIKE_SETTINGS_SNAPSHOT tSnapshot;
    int32_t lFirstValue;
    int32_t lSecondValue;
    rt_err_t eResult;

    if ((2 > lArgumentCount) || (0 == strcmp(pArguments[1], "status")))
    {
        (void)memset(&tSnapshot, 0, sizeof(tSnapshot));
        eResult = BIKE_SETTINGS_GetSnapshot(&tSnapshot);
        rt_kprintf("bike settings ret=%d storage=%u timezone=%d wheel=%u weight=%u "
                   "auto=%u threshold=%u brightness=%u screen=%u sound=%u map_wgs84=%u "
                   "map_dir=%s map_ext=%s\n",
                   eResult, tSnapshot.bStorageReady, tSnapshot.sTimeZoneMinutes,
                   tSnapshot.usWheelCircumferenceMm,
                   tSnapshot.ucRiderWeightKg, tSnapshot.bAutoPauseEnabled,
                   tSnapshot.usAutoPauseCentiKph, tSnapshot.ucBrightnessPercent,
                   tSnapshot.usScreenTimeoutSeconds, tSnapshot.bSoundEnabled,
                   tSnapshot.bMapUseWgs84,
                   tSnapshot.aMapDirectory, tSnapshot.aMapExtension);
        rt_kprintf("usage: bikeset timezone <min> | wheel <mm> | weight <kg> | "
                   "sound <0|1> | map <0|1> | mapdir </path> | mapext <ext> | "
                   "autopause <0|1> <centi-kph> | "
                   "display <1-100> <sec>\n");
        return;
    }

    lFirstValue = 0;
    lSecondValue = 0;
    eResult = -RT_EINVAL;
    if ((3 == lArgumentCount) && BikeSettings_ParseInt32(pArguments[2], &lFirstValue))
    {
        if ((0 == strcmp(pArguments[1], "timezone")) &&
            (INT16_MIN <= lFirstValue) && (INT16_MAX >= lFirstValue))
        {
            eResult = BIKE_SETTINGS_SetTimeZoneMinutes((int16_t)lFirstValue);
        }
        else if ((0 == strcmp(pArguments[1], "wheel")) &&
                 (0 <= lFirstValue) && (UINT16_MAX >= lFirstValue))
        {
            eResult = BIKE_SETTINGS_SetWheelCircumference((uint16_t)lFirstValue);
        }
        else if ((0 == strcmp(pArguments[1], "weight")) &&
                 (0 <= lFirstValue) && (UINT8_MAX >= lFirstValue))
        {
            eResult = BIKE_SETTINGS_SetRiderWeight((uint8_t)lFirstValue);
        }
        else if ((0 == strcmp(pArguments[1], "map")) &&
                 ((0 == lFirstValue) || (1 == lFirstValue)))
        {
            eResult = BIKE_SETTINGS_SetMapUseWgs84(1 == lFirstValue);
        }
        else if ((0 == strcmp(pArguments[1], "sound")) &&
                 ((0 == lFirstValue) || (1 == lFirstValue)))
        {
            eResult = BIKE_SETTINGS_SetSoundEnabled(1 == lFirstValue);
        }
    }
    else if ((3 == lArgumentCount) &&
             (0 == strcmp(pArguments[1], "mapdir")))
    {
        eResult = BIKE_SETTINGS_SetMapDirectory(pArguments[2]);
    }
    else if ((3 == lArgumentCount) &&
             (0 == strcmp(pArguments[1], "mapext")))
    {
        eResult = BIKE_SETTINGS_SetMapExtension(pArguments[2]);
    }
    else if ((4 == lArgumentCount) &&
             BikeSettings_ParseInt32(pArguments[2], &lFirstValue) &&
             BikeSettings_ParseInt32(pArguments[3], &lSecondValue))
    {
        if ((0 == strcmp(pArguments[1], "autopause")) &&
            ((0 == lFirstValue) || (1 == lFirstValue)) &&
            (0 <= lSecondValue) && (UINT16_MAX >= lSecondValue))
        {
            eResult = BIKE_SETTINGS_SetAutoPause((1 == lFirstValue),
                                                 (uint16_t)lSecondValue);
        }
        else if ((0 == strcmp(pArguments[1], "display")) &&
                 (0 <= lFirstValue) && (UINT8_MAX >= lFirstValue) &&
                 (0 <= lSecondValue) && (UINT16_MAX >= lSecondValue))
        {
            eResult = BIKE_SETTINGS_SetDisplay((uint8_t)lFirstValue,
                                               (uint16_t)lSecondValue);
        }
    }

    rt_kprintf("bike settings update ret=%d\n", eResult);
    return;
}
MSH_CMD_EXPORT_ALIAS(BikeSettings_Command, bikeset, bike persistent settings);
