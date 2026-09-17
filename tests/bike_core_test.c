#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bike_gpx.h"
#include "bike_auto_pause.h"
#include "bike_ble_advertising.h"
#include "bike_ble_measurement.h"
#include "bike_csc.h"
#include "bike_nmea.h"
#include "bike_ride_model.h"
#include "bike_speed_source.h"
#include "bike_time.h"

#define TEST_GPX_DIRECTORY "/tmp/sf32_bike_gpx_test"

/* Test_CountText: 统计文本中固定子串的出现次数。
 * 参数：
 *   - pText: 输入文本
 *   - pPattern: 待统计子串
 * 返回值：出现次数
 */
static uint32_t Test_CountText(const char *pText, const char *pPattern)
{
    const char *pCursor;
    uint32_t ulCount;
    size_t ulPatternLength;

    assert(NULL != pText);
    assert(NULL != pPattern);
    ulCount = 0U;
    ulPatternLength = strlen(pPattern);
    pCursor = pText;
    while (NULL != (pCursor = strstr(pCursor, pPattern)))
    {
        ulCount++;
        pCursor += ulPatternLength;
    }

    return ulCount;
}

/* Test_ReadFile: 将测试文件读取到固定容量缓冲。
 * 参数：
 *   - pPath: 文件路径
 *   - pBuffer: 输出缓冲
 *   - ulBufferSize: 缓冲容量
 * 返回值：读取字节数
 */
static size_t Test_ReadFile(const char *pPath, char *pBuffer, size_t ulBufferSize)
{
    int lFileDescriptor;
    ssize_t dReadLength;

    assert(NULL != pPath);
    assert(NULL != pBuffer);
    assert(1U < ulBufferSize);
    lFileDescriptor = open(pPath, O_RDONLY);
    assert(0 <= lFileDescriptor);
    dReadLength = read(lFileDescriptor, pBuffer, ulBufferSize - 1U);
    assert(0 <= dReadLength);
    assert(0 == close(lFileDescriptor));
    pBuffer[dReadLength] = '\0';

    return (size_t)dReadLength;
}

/* Test_FeedSentence: 向固定容量解析器输入一条完整 NMEA 语句。
 * 参数：
 *   - pParser: 解析器实例
 *   - pSentence: 测试语句
 * 返回值：最后一个非空解析结果
 */
static BIKE_NMEA_RESULT Test_FeedSentence(BIKE_NMEA_PARSER *pParser, const char *pSentence)
{
    BIKE_NMEA_RESULT eResult;
    BIKE_NMEA_RESULT eLastResult;
    size_t ulIndex;

    assert(NULL != pParser);
    assert(NULL != pSentence);
    eLastResult = BIKE_NMEA_RESULT_NONE;
    for (ulIndex = 0U; ulIndex < strlen(pSentence); ulIndex++)
    {
        eResult = BIKE_NMEA_Feed(pParser, (uint8_t)pSentence[ulIndex]);
        if (BIKE_NMEA_RESULT_NONE != eResult)
        {
            eLastResult = eResult;
        }
    }

    return eLastResult;
}

/* Test_NmeaParser: 覆盖 GGA/RMC/VTG、校验错误和字段换算。
 * 返回值：无
 */
static void Test_NmeaParser(void)
{
    BIKE_NMEA_PARSER tParser;
    const BIKE_GNSS_DATA *pData;
    BIKE_NMEA_RESULT eResult;

    BIKE_NMEA_Init(&tParser);
    eResult = Test_FeedSentence(&tParser,
                                "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
    assert(BIKE_NMEA_RESULT_GGA == eResult);
    pData = BIKE_NMEA_GetData(&tParser);
    assert(NULL != pData);
    assert(pData->bFixValid);
    assert(8U == pData->ucSatellites);
    assert(54540 == pData->lAltitudeCm);
    assert((481172900 <= pData->lLatitudeE7) && (481173100 >= pData->lLatitudeE7));

    eResult = Test_FeedSentence(&tParser,
                                "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n");
    assert(BIKE_NMEA_RESULT_RMC == eResult);
    pData = BIKE_NMEA_GetData(&tParser);
    assert(1994U == pData->usYear);
    assert(1152U == pData->ulSpeedCmPerSec);
    assert(844U == pData->usCourseDeg10);

    eResult = Test_FeedSentence(&tParser,
                                "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48\r\n");
    assert(BIKE_NMEA_RESULT_VTG == eResult);
    pData = BIKE_NMEA_GetData(&tParser);
    assert(283U == pData->ulSpeedCmPerSec);
    assert(547U == pData->usCourseDeg10);

    eResult = Test_FeedSentence(&tParser,
                                "$GPVTG,054.7,T,034.4,M,005.5,N,,K*65\r\n");
    assert(BIKE_NMEA_RESULT_VTG == eResult);
    pData = BIKE_NMEA_GetData(&tParser);
    assert(283U == pData->ulSpeedCmPerSec);

    eResult = Test_FeedSentence(&tParser,
                                "$GNVTG,,T,,M,,N,,K,N*32\r\n");
    assert(BIKE_NMEA_RESULT_VTG == eResult);
    pData = BIKE_NMEA_GetData(&tParser);
    assert(0U == pData->ulSpeedCmPerSec);

    eResult = Test_FeedSentence(&tParser,
                                "$GPVTG,400.0,T,034.4,M,005.5,N,010.2,K*4A\r\n");
    assert(BIKE_NMEA_RESULT_ERROR == eResult);
    pData = BIKE_NMEA_GetData(&tParser);
    assert(547U == pData->usCourseDeg10);

    eResult = Test_FeedSentence(&tParser,
                                "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00\r\n");
    assert(BIKE_NMEA_RESULT_ERROR == eResult);
    assert(1U == tParser.ulChecksumErrorCount);

    return;
}

/* Test_RideModel: 覆盖开始、里程、速度、暂停和跳点过滤。
 * 返回值：无
 */
static void Test_RideModel(void)
{
    BIKE_RIDE_STATE tState;
    BIKE_GNSS_DATA tGnss;
    uint32_t ulDistanceBeforePause;

    (void)memset(&tGnss, 0, sizeof(tGnss));
    tGnss.bFixValid = true;
    tGnss.ucSatellites = 8U;
    tGnss.ulSpeedCmPerSec = 500U;

    BIKE_RIDE_Init(&tState, 70U);
    BIKE_RIDE_Start(&tState, 0U);
    BIKE_RIDE_Update(&tState, &tGnss, 1000U);
    tGnss.lLongitudeE7 = 898;
    BIKE_RIDE_Update(&tState, &tGnss, 2000U);

    assert(BIKE_RIDE_MODE_RUNNING == tState.eMode);
    assert((9000U <= tState.ulDistanceMm) && (11000U >= tState.ulDistanceMm));
    assert(1800U == tState.usSpeedCentiKph);
    assert(2000U == tState.ulMovingTimeMs);
    assert((1700U <= tState.usAverageSpeedCentiKph) && (1900U >= tState.usAverageSpeedCentiKph));

    ulDistanceBeforePause = tState.ulDistanceMm;
    BIKE_RIDE_Pause(&tState);
    tGnss.lLongitudeE7 = 1796;
    BIKE_RIDE_Update(&tState, &tGnss, 3000U);
    assert(ulDistanceBeforePause == tState.ulDistanceMm);
    assert(1800U == tState.usSpeedCentiKph);

    BIKE_RIDE_Start(&tState, 4000U);
    tGnss.lLongitudeE7 = 1000000;
    BIKE_RIDE_Update(&tState, &tGnss, 5000U);
    tGnss.lLongitudeE7 = 2000000;
    BIKE_RIDE_Update(&tState, &tGnss, 6000U);
    assert(ulDistanceBeforePause == tState.ulDistanceMm);

    return;
}

/* Test_SpeedSource: 覆盖 CSC 优先、GNSS 回退、来源切换和 CSC 里程。
 * 返回值：无
 */
static void Test_SpeedSource(void)
{
    BIKE_SPEED_INPUT tInput;
    BIKE_SPEED_SELECTION tSelection;
    BIKE_RIDE_STATE tState;
    BIKE_GNSS_DATA tGnss;

    (void)memset(&tInput, 0, sizeof(tInput));
    tInput.bGnssValid = true;
    tInput.ulGnssSpeedCmPerSec = 500U;
    BIKE_SPEED_Select(&tInput, &tSelection);
    assert(tSelection.bValid);
    assert(BIKE_SPEED_SOURCE_GNSS == tSelection.eSource);
    assert(1800U == tSelection.usSpeedCentiKph);

    tInput.bCscValid = true;
    tInput.usCscSpeedCentiKph = 3600U;
    BIKE_SPEED_Select(&tInput, &tSelection);
    assert(tSelection.bValid);
    assert(BIKE_SPEED_SOURCE_CSC == tSelection.eSource);
    assert(3600U == tSelection.usSpeedCentiKph);

    (void)memset(&tGnss, 0, sizeof(tGnss));
    tGnss.bFixValid = true;
    BIKE_RIDE_Init(&tState, 70U);
    BIKE_RIDE_Start(&tState, 0U);
    BIKE_RIDE_UpdateWithSpeed(&tState, &tGnss, &tSelection, 1000U);
    assert(BIKE_SPEED_SOURCE_CSC == tState.eSpeedSource);
    assert(10000U == tState.ulDistanceMm);
    assert(1000U == tState.ulMovingTimeMs);

    tInput.bCscValid = false;
    BIKE_SPEED_Select(&tInput, &tSelection);
    BIKE_RIDE_UpdateWithSpeed(&tState, &tGnss, &tSelection, 2000U);
    assert(BIKE_SPEED_SOURCE_GNSS == tState.eSpeedSource);
    assert(10000U == tState.ulDistanceMm);
    tGnss.lLongitudeE7 = 898;
    BIKE_RIDE_UpdateWithSpeed(&tState, &tGnss, &tSelection, 3000U);
    assert((19000U <= tState.ulDistanceMm) &&
           (21000U >= tState.ulDistanceMm));

    BIKE_RIDE_InvalidateFix(&tState);
    assert(BIKE_SPEED_SOURCE_NONE == tState.eSpeedSource);
    assert(0U == tState.usSpeedCentiKph);

    tInput.bGnssValid = false;
    tInput.bCscValid = true;
    tInput.usCscSpeedCentiKph = 3600U;
    BIKE_SPEED_Select(&tInput, &tSelection);
    tGnss.bFixValid = false;
    BIKE_RIDE_UpdateWithSpeed(&tState, &tGnss, &tSelection, 4000U);
    BIKE_RIDE_InvalidateFix(&tState);
    assert(BIKE_SPEED_SOURCE_CSC == tState.eSpeedSource);
    assert(3600U == tState.usSpeedCentiKph);

    tInput.bGnssValid = true;
    tInput.ulGnssSpeedCmPerSec = 500U;
    tInput.usCscSpeedCentiKph = 20001U;
    BIKE_SPEED_Select(&tInput, &tSelection);
    assert(tSelection.bValid);
    assert(BIKE_SPEED_SOURCE_GNSS == tSelection.eSource);
    assert(1800U == tSelection.usSpeedCentiKph);
    tInput.bGnssValid = false;
    BIKE_SPEED_Select(&tInput, &tSelection);
    assert(!tSelection.bValid);
    assert(BIKE_SPEED_SOURCE_NONE == tSelection.eSource);

    BIKE_RIDE_Init(&tState, 70U);
    BIKE_RIDE_Start(&tState, UINT32_MAX - 499U);
    tInput.bCscValid = true;
    tInput.usCscSpeedCentiKph = 3600U;
    BIKE_SPEED_Select(&tInput, &tSelection);
    BIKE_RIDE_UpdateWithSpeed(&tState, NULL, &tSelection, 500U);
    assert(10000U == tState.ulDistanceMm);
    assert(1000U == tState.ulMovingTimeMs);

    BIKE_SPEED_Select(NULL, &tSelection);
    assert(!tSelection.bValid);
    assert(BIKE_SPEED_SOURCE_NONE == tSelection.eSource);
    BIKE_SPEED_Select(&tInput, NULL);

    return;
}

/* Test_AutoPause: 覆盖低速防抖、恢复回差、无效定位和手动暂停隔离。
 * 返回值：无
 */
static void Test_AutoPause(void)
{
    BIKE_AUTO_PAUSE_CONTROLLER tController;
    BIKE_AUTO_PAUSE_ACTION eAction;

    BIKE_AUTO_PAUSE_Init(&tController);
    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, false,
                                     BIKE_RIDE_MODE_RUNNING, true,
                                     250U, 300U, 1000U);
    assert(BIKE_AUTO_PAUSE_ACTION_NONE == eAction);
    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, false,
                                     BIKE_RIDE_MODE_RUNNING, true,
                                     250U, 300U, 5999U);
    assert(BIKE_AUTO_PAUSE_ACTION_NONE == eAction);
    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, false,
                                     BIKE_RIDE_MODE_RUNNING, true,
                                     250U, 300U, 6000U);
    assert(BIKE_AUTO_PAUSE_ACTION_PAUSE == eAction);

    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, true,
                                     BIKE_RIDE_MODE_PAUSED, true,
                                     350U, 300U, 7000U);
    assert(BIKE_AUTO_PAUSE_ACTION_NONE == eAction);
    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, true,
                                     BIKE_RIDE_MODE_PAUSED, true,
                                     450U, 300U, 8000U);
    assert(BIKE_AUTO_PAUSE_ACTION_NONE == eAction);
    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, true,
                                     BIKE_RIDE_MODE_PAUSED, true,
                                     450U, 300U, 9999U);
    assert(BIKE_AUTO_PAUSE_ACTION_NONE == eAction);
    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, true,
                                     BIKE_RIDE_MODE_PAUSED, true,
                                     450U, 300U, 10000U);
    assert(BIKE_AUTO_PAUSE_ACTION_RESUME == eAction);

    eAction = BIKE_AUTO_PAUSE_Update(&tController, false, true,
                                     BIKE_RIDE_MODE_PAUSED, false,
                                     0U, 300U, 11000U);
    assert(BIKE_AUTO_PAUSE_ACTION_RESUME == eAction);
    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, false,
                                     BIKE_RIDE_MODE_PAUSED, true,
                                     1000U, 300U, 12000U);
    assert(BIKE_AUTO_PAUSE_ACTION_NONE == eAction);

    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, false,
                                     BIKE_RIDE_MODE_RUNNING, true,
                                     0U, 300U, UINT32_MAX - 2000U);
    assert(BIKE_AUTO_PAUSE_ACTION_NONE == eAction);
    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, false,
                                     BIKE_RIDE_MODE_RUNNING, false,
                                     0U, 300U, UINT32_MAX - 1000U);
    assert(BIKE_AUTO_PAUSE_ACTION_NONE == eAction);
    eAction = BIKE_AUTO_PAUSE_Update(&tController, true, false,
                                     BIKE_RIDE_MODE_RUNNING, true,
                                     0U, 300U, 1000U);
    assert(BIKE_AUTO_PAUSE_ACTION_NONE == eAction);

    return;
}

/* Test_CscCalculation: 覆盖轮速、踏频及 16/32 位累计值回绕。
 * 返回值：无
 */
static void Test_CscCalculation(void)
{
    BIKE_CSC_MEASUREMENT tMeasurement;
    BIKE_CSC_STATE tState;

    (void)memset(&tMeasurement, 0, sizeof(tMeasurement));
    tMeasurement.ucFlags = BIKE_CSC_WHEEL_DATA_PRESENT |
                           BIKE_CSC_CRANK_DATA_PRESENT;
    tMeasurement.ulCumulativeWheelRevolutions = 100U;
    tMeasurement.usLastWheelEventTime = 1000U;
    tMeasurement.usCumulativeCrankRevolutions = 200U;
    tMeasurement.usLastCrankEventTime = 1000U;
    BIKE_CSC_Init(&tState);
    BIKE_CSC_Update(&tState, &tMeasurement, 2105U);
    assert(!tState.bWheelSpeedValid);
    assert(!tState.bCadenceValid);

    tMeasurement.ulCumulativeWheelRevolutions = 101U;
    tMeasurement.usLastWheelEventTime = 2024U;
    tMeasurement.usCumulativeCrankRevolutions = 201U;
    tMeasurement.usLastCrankEventTime = 2024U;
    BIKE_CSC_Update(&tState, &tMeasurement, 2105U);
    assert(tState.bWheelSpeedValid);
    assert(757U == tState.usWheelSpeedCentiKph);
    assert(tState.bCadenceValid);
    assert(60U == tState.usCadenceRpm);

    BIKE_CSC_Init(&tState);
    tMeasurement.ulCumulativeWheelRevolutions = UINT32_MAX;
    tMeasurement.usLastWheelEventTime = UINT16_MAX - 512U;
    tMeasurement.usCumulativeCrankRevolutions = UINT16_MAX;
    tMeasurement.usLastCrankEventTime = UINT16_MAX - 512U;
    BIKE_CSC_Update(&tState, &tMeasurement, 2105U);
    tMeasurement.ulCumulativeWheelRevolutions = 0U;
    tMeasurement.usLastWheelEventTime = 511U;
    tMeasurement.usCumulativeCrankRevolutions = 0U;
    tMeasurement.usLastCrankEventTime = 511U;
    BIKE_CSC_Update(&tState, &tMeasurement, 2105U);
    assert(tState.bWheelSpeedValid);
    assert(757U == tState.usWheelSpeedCentiKph);
    assert(tState.bCadenceValid);
    assert(60U == tState.usCadenceRpm);

    return;
}

/* Test_BleAdvertising: 覆盖 HR/CSC UUID 列表、服务数据和畸形 AD 结构。
 * 返回值：无
 */
static void Test_BleAdvertising(void)
{
    static const uint8_t aHeartRate[] = {3U, 0x03U, 0x0DU, 0x18U};
    static const uint8_t aCombined[] = {
        2U, 0x01U, 0x06U,
        5U, 0x02U, 0x0DU, 0x18U, 0x16U, 0x18U
    };
    static const uint8_t aCscServiceData[] = {
        5U, 0x16U, 0x16U, 0x18U, 0x01U, 0x02U
    };
    static const uint8_t aMalformed[] = {5U, 0x03U, 0x0DU, 0x18U};

    assert(BIKE_BLE_SERVICE_HEART_RATE ==
           BIKE_BLE_ADV_GetServiceMask(aHeartRate, sizeof(aHeartRate)));
    assert((BIKE_BLE_SERVICE_HEART_RATE | BIKE_BLE_SERVICE_CSC) ==
           BIKE_BLE_ADV_GetServiceMask(aCombined, sizeof(aCombined)));
    assert(BIKE_BLE_SERVICE_CSC ==
           BIKE_BLE_ADV_GetServiceMask(aCscServiceData,
                                       sizeof(aCscServiceData)));
    assert(0U == BIKE_BLE_ADV_GetServiceMask(aMalformed, sizeof(aMalformed)));
    assert(0U == BIKE_BLE_ADV_GetServiceMask(NULL, 0U));

    return;
}

/* Test_BleMeasurement: 覆盖标准 HR/CSC 测量解析和畸形长度拒绝。
 * 返回值：无
 */
static void Test_BleMeasurement(void)
{
    static const uint8_t aHeartRate8[] = {0x00U, 72U};
    static const uint8_t aHeartRate16[] = {0x01U, 0x2CU, 0x01U};
    static const uint8_t aHeartRateEnergyRr[] = {
        0x18U, 80U, 0x34U, 0x12U, 0x00U, 0x04U
    };
    static const uint8_t aHeartRateTruncated[] = {0x01U, 0x2CU};
    static const uint8_t aHeartRateOddRr[] = {0x10U, 80U, 0x01U};
    static const uint8_t aHeartRateTrailing[] = {0x00U, 80U, 0x01U};
    static const uint8_t aCscCombined[] = {
        0x03U, 0x78U, 0x56U, 0x34U, 0x12U, 0xBCU, 0x9AU,
        0x57U, 0x13U, 0x68U, 0x24U
    };
    static const uint8_t aCscTruncated[] = {0x01U, 0x01U, 0x00U};
    static const uint8_t aCscReservedFlag[] = {0x04U};
    static const uint8_t aCscTrailing[] = {0x00U, 0x01U};
    static const uint8_t aBatteryValid[] = {85U};
    static const uint8_t aBatteryInvalid[] = {101U};
    static const uint8_t aBatteryTrailing[] = {85U, 0U};
    BIKE_CSC_MEASUREMENT tMeasurement;
    uint8_t ucBatteryPercent;
    uint16_t usHeartRateBpm;

    usHeartRateBpm = 0U;
    assert(BIKE_BLE_MEAS_ParseHeartRate(aHeartRate8,
                                        sizeof(aHeartRate8),
                                        &usHeartRateBpm));
    assert(72U == usHeartRateBpm);
    assert(BIKE_BLE_MEAS_ParseHeartRate(aHeartRate16,
                                        sizeof(aHeartRate16),
                                        &usHeartRateBpm));
    assert(300U == usHeartRateBpm);
    assert(BIKE_BLE_MEAS_ParseHeartRate(aHeartRateEnergyRr,
                                        sizeof(aHeartRateEnergyRr),
                                        &usHeartRateBpm));
    assert(80U == usHeartRateBpm);
    assert(!BIKE_BLE_MEAS_ParseHeartRate(aHeartRateTruncated,
                                         sizeof(aHeartRateTruncated),
                                         &usHeartRateBpm));
    assert(!BIKE_BLE_MEAS_ParseHeartRate(aHeartRateOddRr,
                                         sizeof(aHeartRateOddRr),
                                         &usHeartRateBpm));
    assert(!BIKE_BLE_MEAS_ParseHeartRate(aHeartRateTrailing,
                                         sizeof(aHeartRateTrailing),
                                         &usHeartRateBpm));
    assert(!BIKE_BLE_MEAS_ParseHeartRate(NULL, 0U, &usHeartRateBpm));
    assert(!BIKE_BLE_MEAS_ParseHeartRate(aHeartRate8,
                                         sizeof(aHeartRate8), NULL));

    (void)memset(&tMeasurement, 0, sizeof(tMeasurement));
    assert(BIKE_BLE_MEAS_ParseCsc(aCscCombined, sizeof(aCscCombined),
                                  &tMeasurement));
    assert((BIKE_CSC_WHEEL_DATA_PRESENT | BIKE_CSC_CRANK_DATA_PRESENT) ==
           tMeasurement.ucFlags);
    assert(0x12345678U == tMeasurement.ulCumulativeWheelRevolutions);
    assert(0x9ABCU == tMeasurement.usLastWheelEventTime);
    assert(0x1357U == tMeasurement.usCumulativeCrankRevolutions);
    assert(0x2468U == tMeasurement.usLastCrankEventTime);
    assert(!BIKE_BLE_MEAS_ParseCsc(aCscTruncated, sizeof(aCscTruncated),
                                   &tMeasurement));
    assert(!BIKE_BLE_MEAS_ParseCsc(aCscReservedFlag,
                                   sizeof(aCscReservedFlag), &tMeasurement));
    assert(!BIKE_BLE_MEAS_ParseCsc(aCscTrailing, sizeof(aCscTrailing),
                                   &tMeasurement));
    assert(!BIKE_BLE_MEAS_ParseCsc(NULL, 0U, &tMeasurement));
    assert(!BIKE_BLE_MEAS_ParseCsc(aCscCombined, sizeof(aCscCombined), NULL));

    ucBatteryPercent = 0U;
    assert(BIKE_BLE_MEAS_ParseBatteryLevel(aBatteryValid,
                                           sizeof(aBatteryValid),
                                           &ucBatteryPercent));
    assert(85U == ucBatteryPercent);
    assert(!BIKE_BLE_MEAS_ParseBatteryLevel(aBatteryInvalid,
                                             sizeof(aBatteryInvalid),
                                             &ucBatteryPercent));
    assert(!BIKE_BLE_MEAS_ParseBatteryLevel(aBatteryTrailing,
                                             sizeof(aBatteryTrailing),
                                             &ucBatteryPercent));
    assert(!BIKE_BLE_MEAS_ParseBatteryLevel(NULL, 0U,
                                             &ucBatteryPercent));
    assert(!BIKE_BLE_MEAS_ParseBatteryLevel(aBatteryValid,
                                             sizeof(aBatteryValid), NULL));

    return;
}

/* Test_TimeConversion: 覆盖正负时区、跨年和闰日转换。
 * 返回值：无
 */
static void Test_TimeConversion(void)
{
    BIKE_GNSS_DATA tGnss;
    BIKE_LOCAL_TIME tLocalTime;

    (void)memset(&tGnss, 0, sizeof(tGnss));
    tGnss.usYear = 2024U;
    tGnss.ucMonth = 12U;
    tGnss.ucDay = 31U;
    tGnss.ucHour = 18U;
    tGnss.ucMinute = 30U;
    tGnss.ucSecond = 15U;
    assert(BIKE_TIME_ConvertUtc(&tGnss, 480, &tLocalTime));
    assert(2025U == tLocalTime.usYear);
    assert(1U == tLocalTime.ucMonth);
    assert(1U == tLocalTime.ucDay);
    assert(2U == tLocalTime.ucHour);
    assert(30U == tLocalTime.ucMinute);

    tGnss.usYear = 2024U;
    tGnss.ucMonth = 3U;
    tGnss.ucDay = 1U;
    tGnss.ucHour = 0U;
    tGnss.ucMinute = 30U;
    assert(BIKE_TIME_ConvertUtc(&tGnss, -60, &tLocalTime));
    assert(2024U == tLocalTime.usYear);
    assert(2U == tLocalTime.ucMonth);
    assert(29U == tLocalTime.ucDay);
    assert(23U == tLocalTime.ucHour);
    assert(30U == tLocalTime.ucMinute);

    tGnss.ucMonth = 2U;
    tGnss.ucDay = 30U;
    assert(!BIKE_TIME_ConvertUtc(&tGnss, 0, &tLocalTime));
    assert(!BIKE_TIME_ConvertUtc(&tGnss, 900, &tLocalTime));

    return;
}

/* Test_GpxWriter: 覆盖流式写点、暂停、闭合和断电恢复。
 * 返回值：无
 */
static void Test_GpxWriter(void)
{
    BIKE_GPX_WRITER tWriter;
    BIKE_GNSS_DATA tGnss;
    BIKE_GPX_RECOVERY_RESULT eRecovery;
    char aFileData[2048];
    char aRecoveredPath[BIKE_GPX_PATH_MAX];
    const char *pFirstFile;
    const char *pSecondFile;
    const char *pDiscardFile;

    pFirstFile = TEST_GPX_DIRECTORY "/TRK_20240923_123519.gpx";
    pSecondFile = TEST_GPX_DIRECTORY "/TRK_20240923_123529.gpx";
    pDiscardFile = TEST_GPX_DIRECTORY "/TRK_20240923_123539.gpx";
    (void)unlink(TEST_GPX_DIRECTORY "/.active");
    (void)unlink(TEST_GPX_DIRECTORY "/.active.tmp");
    (void)unlink(TEST_GPX_DIRECTORY "/TRK_20240923_123519.gpx.part");
    (void)unlink(TEST_GPX_DIRECTORY "/TRK_20240923_123529.gpx.part");
    (void)unlink(TEST_GPX_DIRECTORY "/TRK_20240923_123539.gpx.part");
    (void)unlink(pFirstFile);
    (void)unlink(pSecondFile);
    (void)unlink(pDiscardFile);
    (void)rmdir(TEST_GPX_DIRECTORY);

    (void)memset(&tGnss, 0, sizeof(tGnss));
    tGnss.bFixValid = true;
    tGnss.usYear = 2024U;
    tGnss.ucMonth = 9U;
    tGnss.ucDay = 23U;
    tGnss.ucHour = 12U;
    tGnss.ucMinute = 35U;
    tGnss.ucSecond = 19U;
    tGnss.lAltitudeCm = 12345;

    BIKE_GPX_Init(&tWriter);
    assert(BIKE_GPX_Start(&tWriter, TEST_GPX_DIRECTORY, &tGnss));
    tGnss.ucSecond = 20U;
    tGnss.lLongitudeE7 = 898;
    assert(BIKE_GPX_Append(&tWriter, &tGnss));
    assert(BIKE_GPX_Pause(&tWriter));
    assert(BIKE_GPX_Resume(&tWriter));
    tGnss.ucSecond = 21U;
    tGnss.lLongitudeE7 = 10000000;
    assert(BIKE_GPX_Append(&tWriter, &tGnss));
    tGnss.ucSecond = 22U;
    tGnss.lLongitudeE7 = 10000898;
    assert(BIKE_GPX_Append(&tWriter, &tGnss));
    assert(BIKE_GPX_Stop(&tWriter));
    assert(3U == tWriter.ulPointCount);
    assert(0U < Test_ReadFile(pFirstFile, aFileData, sizeof(aFileData)));
    assert(3U == Test_CountText(aFileData, "<trkpt"));
    assert(NULL != strstr(aFileData, "<ele>123.45</ele>"));
    assert(NULL != strstr(aFileData, "2024-09-23T12:35:22Z"));
    assert(2U == Test_CountText(aFileData, "<trkseg>"));
    assert(NULL != strstr(aFileData, "</trkseg></trk></gpx>"));

    tGnss.ucSecond = 29U;
    tGnss.lLongitudeE7 = 2694;
    BIKE_GPX_Init(&tWriter);
    assert(BIKE_GPX_Start(&tWriter, TEST_GPX_DIRECTORY, &tGnss));
    tGnss.ucSecond = 30U;
    tGnss.lLongitudeE7 = 20000000;
    assert(BIKE_GPX_Append(&tWriter, &tGnss));
    tGnss.ucSecond = 31U;
    tGnss.lLongitudeE7 = 20000898;
    assert(BIKE_GPX_Append(&tWriter, &tGnss));
    assert(0 == close((int)tWriter.lFileDescriptor));
    tWriter.lFileDescriptor = -1;
    aRecoveredPath[0] = '\0';
    eRecovery = BIKE_GPX_Recover(TEST_GPX_DIRECTORY, aRecoveredPath,
                                 sizeof(aRecoveredPath));
    assert(BIKE_GPX_RECOVERY_DONE == eRecovery);
    assert(0 == strcmp(pSecondFile, aRecoveredPath));
    assert(0U < Test_ReadFile(pSecondFile, aFileData, sizeof(aFileData)));
    assert(2U == Test_CountText(aFileData, "<trkpt"));
    assert(2U == Test_CountText(aFileData, "<trkseg>"));
    assert(NULL != strstr(aFileData, "</trkseg></trk></gpx>"));
    assert(0 != access(TEST_GPX_DIRECTORY "/.active", F_OK));

    tGnss.ucSecond = 39U;
    tGnss.lLongitudeE7 = 3592;
    BIKE_GPX_Init(&tWriter);
    assert(BIKE_GPX_Start(&tWriter, TEST_GPX_DIRECTORY, &tGnss));
    assert(BIKE_GPX_Discard(&tWriter));
    assert(BIKE_GPX_STATE_IDLE == tWriter.eState);
    assert(0 != access(TEST_GPX_DIRECTORY "/.active", F_OK));
    assert(0 != access(TEST_GPX_DIRECTORY "/TRK_20240923_123539.gpx.part",
                       F_OK));
    assert(0 != access(pDiscardFile, F_OK));
    assert(!BIKE_GPX_Discard(&tWriter));

#ifndef BIKE_GPX_TEST_KEEP_FILES
    assert(0 == unlink(pFirstFile));
    assert(0 == unlink(pSecondFile));
    assert(0 == rmdir(TEST_GPX_DIRECTORY));
#endif

    return;
}

int main(void)
{
    Test_NmeaParser();
    Test_RideModel();
    Test_SpeedSource();
    Test_AutoPause();
    Test_BleAdvertising();
    Test_BleMeasurement();
    Test_CscCalculation();
    Test_TimeConversion();
    Test_GpxWriter();
    (void)printf("bike_core_test: PASS\n");

    return 0;
}
