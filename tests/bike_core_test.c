#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bike_gpx.h"
#include "bike_history.h"
#include "bike_map.h"
#include "bike_auto_pause.h"
#include "bike_ble_advertising.h"
#include "bike_ble_measurement.h"
#include "bike_csc.h"
#include "bike_compass.h"
#include "bike_nmea.h"
#include "bike_pedometer.h"
#include "bike_power.h"
#include "bike_ride_model.h"
#include "bike_speed_source.h"
#include "bike_storage.h"
#include "bike_time.h"

#define TEST_GPX_DIRECTORY "/tmp/sf32_bike_gpx_test"

/* Test_PowerFilter: 覆盖 X-TRACK 电压百分比边界、低通、2% 回差、
 * 满量程边界、非法电压和空指针保护。
 * 返回值：无
 */
static void Test_PowerFilter(void)
{
    BIKE_POWER_FILTER tFilter;
    uint32_t ulFilteredVoltageDeciMv;
    uint8_t ucPercent;

    assert(0U == BIKE_POWER_VoltageToPercent(32000U));
    assert(0U == BIKE_POWER_VoltageToPercent(33000U));
    assert(50U == BIKE_POWER_VoltageToPercent(37000U));
    assert(99U == BIKE_POWER_VoltageToPercent(40999U));
    assert(100U == BIKE_POWER_VoltageToPercent(41000U));
    assert(100U == BIKE_POWER_VoltageToPercent(45000U));

    BIKE_POWER_ResetFilter(&tFilter);
    assert(!tFilter.bInitialized);
    assert(BIKE_POWER_UpdateFilter(&tFilter, 37000U,
                                   &ulFilteredVoltageDeciMv, &ucPercent));
    assert(37000U == ulFilteredVoltageDeciMv);
    assert(50U == ucPercent);
    assert(BIKE_POWER_UpdateFilter(&tFilter, 37080U,
                                   &ulFilteredVoltageDeciMv, &ucPercent));
    assert(37020U == ulFilteredVoltageDeciMv);
    assert(50U == ucPercent);
    assert(BIKE_POWER_UpdateFilter(&tFilter, 38600U,
                                   &ulFilteredVoltageDeciMv, &ucPercent));
    assert(37415U == ulFilteredVoltageDeciMv);
    assert(55U == ucPercent);

    tFilter.ulFilteredVoltageDeciMv = 40996U;
    tFilter.ucDisplayedPercent = 99U;
    assert(BIKE_POWER_UpdateFilter(&tFilter, 41012U,
                                   &ulFilteredVoltageDeciMv, &ucPercent));
    assert(41000U == ulFilteredVoltageDeciMv);
    assert(100U == ucPercent);
    assert(!BIKE_POWER_UpdateFilter(&tFilter, 0U,
                                    &ulFilteredVoltageDeciMv, &ucPercent));
    assert(41000U == tFilter.ulFilteredVoltageDeciMv);
    assert(!BIKE_POWER_UpdateFilter(NULL, 37000U,
                                    &ulFilteredVoltageDeciMv, &ucPercent));
    assert(!BIKE_POWER_UpdateFilter(&tFilter, 37000U, NULL, &ucPercent));
    assert(!BIKE_POWER_UpdateFilter(&tFilter, 37000U,
                                    &ulFilteredVoltageDeciMv, NULL));
    BIKE_POWER_ResetFilter(NULL);

    return;
}

/* Test_CompassCalibration: 覆盖磁力计水平面校准进度、中心偏移、
 * 四象限航向、无效参数和样本计数饱和。
 * 返回值：无
 */
static void Test_CompassCalibration(void)
{
    BIKE_COMPASS_CALIBRATION tCalibration;
    int32_t lCorrectedX;
    int32_t lCorrectedY;
    uint16_t usHeadingDeg10;

    BIKE_COMPASS_ResetCalibration(&tCalibration);
    assert(!tCalibration.bInitialized);
    assert(!BIKE_COMPASS_UpdateCalibration(&tCalibration, 1200, -800,
                                            4000U, 4U,
                                            &lCorrectedX, &lCorrectedY));
    assert(0U == BIKE_COMPASS_GetCalibrationPercent(&tCalibration, 4000U));
    assert(!BIKE_COMPASS_UpdateCalibration(&tCalibration, 5200, -800,
                                            4000U, 4U,
                                            &lCorrectedX, &lCorrectedY));
    assert(!BIKE_COMPASS_UpdateCalibration(&tCalibration, 1200, 3200,
                                            4000U, 4U,
                                            &lCorrectedX, &lCorrectedY));
    assert(BIKE_COMPASS_UpdateCalibration(&tCalibration, 5200, 3200,
                                           4000U, 4U,
                                           &lCorrectedX, &lCorrectedY));
    assert(2000 == lCorrectedX);
    assert(2000 == lCorrectedY);
    assert(100U == BIKE_COMPASS_GetCalibrationPercent(&tCalibration, 4000U));

    assert(BIKE_COMPASS_CalculateHeadingDeg10(100, 0, &usHeadingDeg10));
    assert(0U == usHeadingDeg10);
    assert(BIKE_COMPASS_CalculateHeadingDeg10(0, 100, &usHeadingDeg10));
    assert(900U == usHeadingDeg10);
    assert(BIKE_COMPASS_CalculateHeadingDeg10(-100, 0, &usHeadingDeg10));
    assert(1800U == usHeadingDeg10);
    assert(BIKE_COMPASS_CalculateHeadingDeg10(0, -100, &usHeadingDeg10));
    assert(2700U == usHeadingDeg10);
    assert(!BIKE_COMPASS_CalculateHeadingDeg10(0, 0, &usHeadingDeg10));
    assert(!BIKE_COMPASS_CalculateHeadingDeg10(1, 1, NULL));

    tCalibration.ulSampleCount = UINT32_MAX;
    assert(BIKE_COMPASS_UpdateCalibration(&tCalibration, 3200, 1200,
                                           4000U, 4U,
                                           &lCorrectedX, &lCorrectedY));
    assert(UINT32_MAX == tCalibration.ulSampleCount);
    assert(!BIKE_COMPASS_UpdateCalibration(NULL, 0, 0, 1U, 1U,
                                            &lCorrectedX, &lCorrectedY));
    BIKE_COMPASS_ResetCalibration(NULL);

    return;
}

/* Test_PedometerAccumulator: 覆盖初始值、正常递增、异常跳变、硬件复位、
 * 16 位回绕和 32 位饱和保护。
 * 返回值：无
 */
static void Test_PedometerAccumulator(void)
{
    BIKE_PEDOMETER_ACCUMULATOR tAccumulator;

    BIKE_PEDOMETER_ResetAccumulator(&tAccumulator);
    assert(!tAccumulator.bInitialized);
    assert(BIKE_PEDOMETER_UpdateAccumulator(&tAccumulator, 120U, 100U));
    assert(120U == tAccumulator.ulTotalSteps);
    assert(BIKE_PEDOMETER_UpdateAccumulator(&tAccumulator, 125U, 100U));
    assert(125U == tAccumulator.ulTotalSteps);
    assert(!BIKE_PEDOMETER_UpdateAccumulator(&tAccumulator, 1000U, 100U));
    assert(125U == tAccumulator.ulTotalSteps);
    assert(BIKE_PEDOMETER_UpdateAccumulator(&tAccumulator, 1005U, 100U));
    assert(130U == tAccumulator.ulTotalSteps);
    assert(BIKE_PEDOMETER_UpdateAccumulator(&tAccumulator, 2U, 100U));
    assert(132U == tAccumulator.ulTotalSteps);

    BIKE_PEDOMETER_ResetAccumulator(&tAccumulator);
    assert(BIKE_PEDOMETER_UpdateAccumulator(&tAccumulator, 65534U, 100U));
    assert(BIKE_PEDOMETER_UpdateAccumulator(&tAccumulator, 2U, 100U));
    assert(65538U == tAccumulator.ulTotalSteps);

    tAccumulator.bInitialized = true;
    tAccumulator.usPreviousRaw = 10U;
    tAccumulator.ulTotalSteps = UINT32_MAX - 1U;
    assert(BIKE_PEDOMETER_UpdateAccumulator(&tAccumulator, 20U, 100U));
    assert(UINT32_MAX == tAccumulator.ulTotalSteps);
    assert(!BIKE_PEDOMETER_UpdateAccumulator(NULL, 0U, 100U));
    BIKE_PEDOMETER_ResetAccumulator(NULL);

    return;
}

/* Test_StoragePaths: 覆盖 TF 优先与内部文件系统回退路径。
 * 返回值：无
 */
static void Test_StoragePaths(void)
{
    assert(0 == strcmp("/tracks",
                       BIKE_STORAGE_SelectTrackDirectory(false)));
    assert(0 == strcmp("/sd/tracks",
                       BIKE_STORAGE_SelectTrackDirectory(true)));
    assert(0 == strcmp("/MAP", BIKE_STORAGE_SelectMapRoot(false)));
    assert(0 == strcmp("/sd/MAP", BIKE_STORAGE_SelectMapRoot(true)));

    assert(BIKE_STORAGE_Init());
    assert(!BIKE_STORAGE_IsTfMounted());
    assert(0 == strcmp("/tracks", BIKE_STORAGE_GetTrackDirectory()));
    assert(0 == strcmp("/MAP", BIKE_STORAGE_GetMapRoot()));

    return;
}

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

/* Test_WriteFile: 将固定文本完整写入测试文件。
 * 参数：
 *   - pPath: 文件路径
 *   - pText: 待写入文本
 * 返回值：无
 */
static void Test_WriteFile(const char *pPath, const char *pText)
{
    size_t ulLength;
    size_t ulOffset;
    ssize_t dWriteLength;
    int lFileDescriptor;

    assert(NULL != pPath);
    assert(NULL != pText);
    lFileDescriptor = open(pPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    assert(0 <= lFileDescriptor);
    ulLength = strlen(pText);
    ulOffset = 0U;
    while (ulOffset < ulLength)
    {
        dWriteLength = write(lFileDescriptor, &pText[ulOffset],
                             ulLength - ulOffset);
        assert(0 < dWriteLength);
        ulOffset += (size_t)dWriteLength;
    }
    assert(0 == fsync(lFileDescriptor));
    assert(0 == close(lFileDescriptor));

    return;
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

/* Test_FeedPayload: 为 NMEA payload 生成校验和后输入解析器。
 * 参数：
 *   - pParser: 解析器实例
 *   - pPayload: 不含 '$'、'*XX' 和换行的 payload
 * 返回值：解析结果
 */
static BIKE_NMEA_RESULT Test_FeedPayload(BIKE_NMEA_PARSER *pParser,
                                         const char *pPayload)
{
    char aSentence[BIKE_NMEA_SENTENCE_MAX];
    uint8_t ucChecksum;
    size_t ulIndex;
    int lLength;

    assert(NULL != pParser);
    assert(NULL != pPayload);
    ucChecksum = 0U;
    for (ulIndex = 0U; ulIndex < strlen(pPayload); ulIndex++)
    {
        ucChecksum ^= (uint8_t)pPayload[ulIndex];
    }
    lLength = snprintf(aSentence, sizeof(aSentence), "$%s*%02X\r\n",
                       pPayload, ucChecksum);
    assert(0 < lLength);
    assert((size_t)lLength < sizeof(aSentence));

    return Test_FeedSentence(pParser, aSentence);
}

/* Test_NmeaParser: 覆盖 GGA/RMC/VTG、校验错误和字段换算。
 * 返回值：无
 */
static void Test_NmeaParser(void)
{
    BIKE_NMEA_PARSER tParser;
    BIKE_GNSS_DATA tDataBeforeError;
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
    tDataBeforeError = *pData;

    eResult = Test_FeedSentence(&tParser,
                                "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00\r\n");
    assert(BIKE_NMEA_RESULT_ERROR == eResult);
    assert(1U == tParser.ulChecksumErrorCount);

    eResult = Test_FeedPayload(
        &tParser,
        "GPRMC,246060,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
    assert(BIKE_NMEA_RESULT_ERROR == eResult);
    eResult = Test_FeedPayload(
        &tParser,
        "GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,300224,003.1,W");
    assert(BIKE_NMEA_RESULT_ERROR == eResult);
    eResult = Test_FeedPayload(
        &tParser,
        "GPRMC,123519,X,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
    assert(BIKE_NMEA_RESULT_ERROR == eResult);
    eResult = Test_FeedPayload(
        &tParser,
        "GPRMC,123519,A,4899.000,N,01131.000,E,022.4,084.4,230394,003.1,W");
    assert(BIKE_NMEA_RESULT_ERROR == eResult);
    eResult = Test_FeedPayload(
        &tParser,
        "GPRMC,123519,A,4807.038,N,18100.000,E,022.4,084.4,230394,003.1,W");
    assert(BIKE_NMEA_RESULT_ERROR == eResult);
    eResult = Test_FeedPayload(
        &tParser,
        "GPRMC,123519,A,4807.038,N,01131.000,E,9223372036854775.807,084.4,230394,003.1,W");
    assert(BIKE_NMEA_RESULT_ERROR == eResult);
    pData = BIKE_NMEA_GetData(&tParser);
    assert(tDataBeforeError.bFixValid == pData->bFixValid);
    assert(tDataBeforeError.ucHour == pData->ucHour);
    assert(tDataBeforeError.ucMinute == pData->ucMinute);
    assert(tDataBeforeError.ucSecond == pData->ucSecond);
    assert(tDataBeforeError.ucDay == pData->ucDay);
    assert(tDataBeforeError.ucMonth == pData->ucMonth);
    assert(tDataBeforeError.usYear == pData->usYear);
    assert(tDataBeforeError.lLatitudeE7 == pData->lLatitudeE7);
    assert(tDataBeforeError.lLongitudeE7 == pData->lLongitudeE7);
    assert(tDataBeforeError.ulSpeedCmPerSec == pData->ulSpeedCmPerSec);
    assert(tDataBeforeError.usCourseDeg10 == pData->usCourseDeg10);

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

    assert(0U == BIKE_RIDE_CalculateDistanceMm(0, -1800000000,
                                                0, 1800000000));

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

    BIKE_CSC_Init(&tState);
    tMeasurement.ulCumulativeWheelRevolutions = 1U;
    tMeasurement.usLastWheelEventTime = 1000U;
    BIKE_CSC_Update(&tState, &tMeasurement, 4000U);
    tMeasurement.ulCumulativeWheelRevolutions = 0U;
    tMeasurement.usLastWheelEventTime = 2024U;
    BIKE_CSC_Update(&tState, &tMeasurement, 4000U);
    assert(!tState.bWheelSpeedValid);

    return;
}

/* Test_BleAdvertising: 覆盖 HR/CSC/Power UUID 列表、服务数据和畸形 AD 结构。
 * 返回值：无
 */
static void Test_BleAdvertising(void)
{
    static const uint8_t aHeartRate[] = {3U, 0x03U, 0x0DU, 0x18U};
    static const uint8_t aCombined[] = {
        2U, 0x01U, 0x06U,
        7U, 0x02U, 0x0DU, 0x18U, 0x16U, 0x18U, 0x18U, 0x18U
    };
    static const uint8_t aCscServiceData[] = {
        5U, 0x16U, 0x16U, 0x18U, 0x01U, 0x02U
    };
    static const uint8_t aMalformed[] = {5U, 0x03U, 0x0DU, 0x18U};
    static const uint8_t aOddUuidList[] = {
        4U, 0x03U, 0x0DU, 0x18U, 0x16U
    };

    assert(BIKE_BLE_SERVICE_HEART_RATE ==
           BIKE_BLE_ADV_GetServiceMask(aHeartRate, sizeof(aHeartRate)));
    assert((BIKE_BLE_SERVICE_HEART_RATE | BIKE_BLE_SERVICE_CSC |
            BIKE_BLE_SERVICE_POWER) ==
           BIKE_BLE_ADV_GetServiceMask(aCombined, sizeof(aCombined)));
    assert(BIKE_BLE_SERVICE_CSC ==
           BIKE_BLE_ADV_GetServiceMask(aCscServiceData,
                                       sizeof(aCscServiceData)));
    assert(0U == BIKE_BLE_ADV_GetServiceMask(aMalformed, sizeof(aMalformed)));
    assert(0U == BIKE_BLE_ADV_GetServiceMask(aOddUuidList,
                                              sizeof(aOddUuidList)));
    assert(0U == BIKE_BLE_ADV_GetServiceMask(NULL, 0U));

    return;
}

/* Test_BleMeasurement: 覆盖标准 HR/CSC/Power 测量解析和畸形长度拒绝。
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
    static const uint8_t aHeartRateReserved[] = {0x20U, 80U};
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
    static const uint8_t aPowerPositive[] = {0x00U, 0x00U, 0xFAU, 0x00U};
    static const uint8_t aPowerNegative[] = {0x00U, 0x00U, 0xD4U, 0xFEU};
    static const uint8_t aPowerReserved[] = {0x00U, 0x20U, 0x00U, 0x00U};
    static const uint8_t aPowerTrailing[] = {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    uint8_t aPowerAllFields[34];
    BIKE_CSC_MEASUREMENT tMeasurement;
    int16_t sPowerWatts;
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
    assert(!BIKE_BLE_MEAS_ParseHeartRate(aHeartRateReserved,
                                         sizeof(aHeartRateReserved),
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

    sPowerWatts = 0;
    assert(BIKE_BLE_MEAS_ParseCyclingPower(aPowerPositive,
                                           sizeof(aPowerPositive),
                                           &sPowerWatts));
    assert(250 == sPowerWatts);
    assert(BIKE_BLE_MEAS_ParseCyclingPower(aPowerNegative,
                                           sizeof(aPowerNegative),
                                           &sPowerWatts));
    assert(-300 == sPowerWatts);
    (void)memset(aPowerAllFields, 0, sizeof(aPowerAllFields));
    aPowerAllFields[0] = 0xF5U;
    aPowerAllFields[1] = 0x0FU;
    assert(BIKE_BLE_MEAS_ParseCyclingPower(aPowerAllFields,
                                           sizeof(aPowerAllFields),
                                           &sPowerWatts));
    assert(!BIKE_BLE_MEAS_ParseCyclingPower(aPowerAllFields,
                                            sizeof(aPowerAllFields) - 1U,
                                            &sPowerWatts));
    assert(!BIKE_BLE_MEAS_ParseCyclingPower(aPowerReserved,
                                            sizeof(aPowerReserved),
                                            &sPowerWatts));
    assert(!BIKE_BLE_MEAS_ParseCyclingPower(aPowerTrailing,
                                            sizeof(aPowerTrailing),
                                            &sPowerWatts));
    assert(!BIKE_BLE_MEAS_ParseCyclingPower(NULL, 0U, &sPowerWatts));
    assert(!BIKE_BLE_MEAS_ParseCyclingPower(aPowerPositive,
                                            sizeof(aPowerPositive), NULL));

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

    tGnss.ucMonth = 2U;
    tGnss.ucDay = 30U;
    BIKE_GPX_Init(&tWriter);
    assert(!BIKE_GPX_Start(&tWriter, TEST_GPX_DIRECTORY, &tGnss));
    tGnss.ucMonth = 9U;
    tGnss.ucDay = 23U;

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

    /* 模拟恢复 rename 已完成，但删除 .part/.active 前再次掉电。 */
    Test_WriteFile(TEST_GPX_DIRECTORY "/TRK_20240923_123529.gpx.part",
                   "stale recovery source\n");
    Test_WriteFile(TEST_GPX_DIRECTORY "/.active",
                   TEST_GPX_DIRECTORY "/TRK_20240923_123529.gpx.part\n");
    aRecoveredPath[0] = '\0';
    eRecovery = BIKE_GPX_Recover(TEST_GPX_DIRECTORY, aRecoveredPath,
                                 sizeof(aRecoveredPath));
    assert(BIKE_GPX_RECOVERY_DONE == eRecovery);
    assert(0 == strcmp(pSecondFile, aRecoveredPath));
    assert(0 != access(TEST_GPX_DIRECTORY "/.active", F_OK));
    assert(0 != access(TEST_GPX_DIRECTORY "/TRK_20240923_123529.gpx.part",
                       F_OK));
    assert(0U < Test_ReadFile(pSecondFile, aFileData, sizeof(aFileData)));
    assert(NULL != strstr(aFileData, "</trkseg></trk></gpx>"));

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

/* Test_MapProjection: 验证 Web Mercator 边界、瓦片坐标和路径安全性。
 * 返回值：无
 */
static void Test_MapProjection(void)
{
    BIKE_MAP_POINT tPoint;
    BIKE_MAP_POINT tWgsPoint;
    BIKE_MAP_POINT tGcjPoint;
    int32_t lMapLatitudeE7;
    int32_t lMapLongitudeE7;
    uint32_t ulConvertedPixelX;
    uint32_t ulConvertedPixelY;
    char aPath[64];

    assert(BIKE_MAP_Project(0, 0, 0U, &tPoint));
    assert(128U == tPoint.ulPixelX);
    assert(128U == tPoint.ulPixelY);
    assert(0U == tPoint.ulTileX);
    assert(0U == tPoint.ulTileY);
    assert(128U == tPoint.usOffsetX);
    assert(128U == tPoint.usOffsetY);

    assert(BIKE_MAP_Project(399074150, 1163913320, 16U, &tPoint));
    assert(16U == tPoint.ucZoom);
    assert(tPoint.ulTileX < (1UL << 16U));
    assert(tPoint.ulTileY < (1UL << 16U));
    assert(256U > tPoint.usOffsetX);
    assert(256U > tPoint.usOffsetY);

    assert(BIKE_MAP_ConvertCoordinate(399074150, 1163913320,
                                      BIKE_MAP_COORDINATE_WGS84,
                                      &lMapLatitudeE7,
                                      &lMapLongitudeE7));
    assert(399074150 == lMapLatitudeE7);
    assert(1163913320 == lMapLongitudeE7);
    assert(BIKE_MAP_ConvertCoordinate(399074150, 1163913320,
                                      BIKE_MAP_COORDINATE_GCJ02,
                                      &lMapLatitudeE7,
                                      &lMapLongitudeE7));
    assert((399074150 + 10000) < lMapLatitudeE7);
    assert((399074150 + 20000) > lMapLatitudeE7);
    assert((1163913320 + 50000) < lMapLongitudeE7);
    assert((1163913320 + 70000) > lMapLongitudeE7);
    assert(BIKE_MAP_ProjectCoordinate(399074150, 1163913320, 16U,
                                      BIKE_MAP_COORDINATE_WGS84,
                                      &tWgsPoint));
    assert(BIKE_MAP_ProjectCoordinate(399074150, 1163913320, 16U,
                                      BIKE_MAP_COORDINATE_GCJ02,
                                      &tGcjPoint));
    assert((tWgsPoint.ulPixelX != tGcjPoint.ulPixelX) ||
           (tWgsPoint.ulPixelY != tGcjPoint.ulPixelY));
    assert(BIKE_MAP_ConvertPixelLevel(
        tWgsPoint.ulPixelX, tWgsPoint.ulPixelY, 16U, 19U,
        &ulConvertedPixelX, &ulConvertedPixelY));
    assert((tWgsPoint.ulPixelX << 3U) == ulConvertedPixelX);
    assert((tWgsPoint.ulPixelY << 3U) == ulConvertedPixelY);
    assert(BIKE_MAP_ConvertPixelLevel(
        ulConvertedPixelX, ulConvertedPixelY, 19U, 16U,
        &ulConvertedPixelX, &ulConvertedPixelY));
    assert(tWgsPoint.ulPixelX == ulConvertedPixelX);
    assert(tWgsPoint.ulPixelY == ulConvertedPixelY);
    assert(!BIKE_MAP_ConvertPixelLevel(
        BIKE_MAP_TILE_SIZE_PX << 16U, tWgsPoint.ulPixelY, 16U, 19U,
        &ulConvertedPixelX, &ulConvertedPixelY));
    assert(!BIKE_MAP_ConvertPixelLevel(
        tWgsPoint.ulPixelX, tWgsPoint.ulPixelY, 20U, 16U,
        &ulConvertedPixelX, &ulConvertedPixelY));
    assert(!BIKE_MAP_ConvertPixelLevel(
        tWgsPoint.ulPixelX, tWgsPoint.ulPixelY, 16U, 19U,
        NULL, &ulConvertedPixelY));

    assert(BIKE_MAP_ConvertCoordinate(488566000, 23522000,
                                      BIKE_MAP_COORDINATE_GCJ02,
                                      &lMapLatitudeE7,
                                      &lMapLongitudeE7));
    assert(488566000 == lMapLatitudeE7);
    assert(23522000 == lMapLongitudeE7);
    assert(!BIKE_MAP_ConvertCoordinate(
        399074150, 1163913320, (BIKE_MAP_COORDINATE_SYSTEM)2,
        &lMapLatitudeE7, &lMapLongitudeE7));
    assert(!BIKE_MAP_ConvertCoordinate(399074150, 1163913320,
                                       BIKE_MAP_COORDINATE_GCJ02, NULL,
                                       &lMapLongitudeE7));
    assert(!BIKE_MAP_ProjectCoordinate(399074150, 1163913320, 16U,
                                       BIKE_MAP_COORDINATE_GCJ02, NULL));
    assert(BIKE_MAP_FormatTilePath("/MAP", tPoint.ucZoom,
                                   tPoint.ulTileX, tPoint.ulTileY, "bin",
                                   aPath, sizeof(aPath)));
    assert(NULL != strstr(aPath, "/MAP/16/"));
    assert(NULL != strstr(aPath, ".bin"));

    assert(BIKE_MAP_Project(900000000, 1800000000, 19U, &tPoint));
    assert((1UL << 19U) > tPoint.ulTileX);
    assert((1UL << 19U) > tPoint.ulTileY);
    assert(!BIKE_MAP_Project(0, 1800000001, 16U, &tPoint));
    assert(!BIKE_MAP_Project(0, 0, 20U, &tPoint));
    assert(!BIKE_MAP_Project(0, 0, 16U, NULL));

    assert(!BIKE_MAP_FormatTilePath("MAP", 16U, 0U, 0U, "bin",
                                    aPath, sizeof(aPath)));
    assert(!BIKE_MAP_FormatTilePath("/MAP/../BAD", 16U, 0U, 0U, "bin",
                                    aPath, sizeof(aPath)));
    assert(!BIKE_MAP_FormatTilePath("/MAP", 16U, (1UL << 16U), 0U,
                                    "bin", aPath, sizeof(aPath)));
    assert(!BIKE_MAP_FormatTilePath("/MAP", 16U, 0U, 0U, "b/in",
                                    aPath, sizeof(aPath)));
    assert(!BIKE_MAP_FormatTilePath("/MAP", 16U, 0U, 0U, "bin",
                                    aPath, 8U));

    return;
}

/* Test_HistoryRecord: 验证累计合并、校验损坏和 A/B 槽新旧选择。
 * 返回值：无
 */
static void Test_HistoryRecord(void)
{
    BIKE_HISTORY_RECORD tFirst;
    BIKE_HISTORY_RECORD tSecond;
    BIKE_HISTORY_RECORD tSelected;
    BIKE_RIDE_STATE tRide;

    BIKE_HISTORY_InitRecord(&tFirst);
    assert(BIKE_HISTORY_IsRecordValid(&tFirst));
    assert(0U == tFirst.ulRideCount);
    assert(!BIKE_HISTORY_MergeRide(&tFirst, NULL));

    BIKE_RIDE_Init(&tRide, 75U);
    tRide.ulDistanceMm = 1234567U;
    tRide.ulMovingTimeMs = 600000U;
    tRide.ulElapsedTimeMs = 720000U;
    tRide.ulCaloriesMilliKcal = 12345U;
    tRide.usMaximumSpeedCentiKph = 4567U;
    assert(BIKE_HISTORY_MergeRide(&tFirst, &tRide));
    assert(BIKE_HISTORY_IsRecordValid(&tFirst));
    assert(1U == tFirst.ulRideCount);
    assert(1234567ULL == tFirst.udDistanceMm);
    assert(600000ULL == tFirst.udMovingTimeMs);
    assert(720000ULL == tFirst.udElapsedTimeMs);
    assert(12345ULL == tFirst.udCaloriesMilliKcal);
    assert(4567U == tFirst.usMaximumSpeedCentiKph);

    tSecond = tFirst;
    tRide.ulDistanceMm = 7654321U;
    tRide.ulMovingTimeMs = 900000U;
    tRide.ulElapsedTimeMs = 960000U;
    tRide.ulCaloriesMilliKcal = 54321U;
    tRide.usMaximumSpeedCentiKph = 4000U;
    assert(BIKE_HISTORY_MergeRide(&tSecond, &tRide));
    assert(BIKE_HISTORY_SelectRecord(&tFirst, &tSecond, &tSelected));
    assert(2U == tSelected.ulRideCount);
    assert(8888888ULL == tSelected.udDistanceMm);
    assert(4567U == tSelected.usMaximumSpeedCentiKph);

    tSecond.ulChecksum ^= 1U;
    assert(!BIKE_HISTORY_IsRecordValid(&tSecond));
    assert(BIKE_HISTORY_SelectRecord(&tFirst, &tSecond, &tSelected));
    assert(1U == tSelected.ulRideCount);
    tFirst.ulChecksum ^= 1U;
    assert(!BIKE_HISTORY_SelectRecord(&tFirst, &tSecond, &tSelected));
    assert(BIKE_HISTORY_IsRecordValid(&tSelected));
    assert(0U == tSelected.ulRideCount);

    return;
}

int main(void)
{
    Test_PowerFilter();
    Test_CompassCalibration();
    Test_PedometerAccumulator();
    Test_StoragePaths();
    Test_NmeaParser();
    Test_RideModel();
    Test_SpeedSource();
    Test_AutoPause();
    Test_BleAdvertising();
    Test_BleMeasurement();
    Test_CscCalculation();
    Test_TimeConversion();
    Test_GpxWriter();
    Test_MapProjection();
    Test_HistoryRecord();
    (void)printf("bike_core_test: PASS\n");

    return 0;
}
