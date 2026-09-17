#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bike_gpx.h"
#include "bike_nmea.h"
#include "bike_ride_model.h"
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

/* Test_NmeaParser: 覆盖 GGA/RMC、校验错误和字段换算。
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

    pFirstFile = TEST_GPX_DIRECTORY "/TRK_20240923_123519.gpx";
    pSecondFile = TEST_GPX_DIRECTORY "/TRK_20240923_123529.gpx";
    (void)unlink(TEST_GPX_DIRECTORY "/.active");
    (void)unlink(TEST_GPX_DIRECTORY "/.active.tmp");
    (void)unlink(TEST_GPX_DIRECTORY "/TRK_20240923_123519.gpx.part");
    (void)unlink(TEST_GPX_DIRECTORY "/TRK_20240923_123529.gpx.part");
    (void)unlink(pFirstFile);
    (void)unlink(pSecondFile);
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
    Test_TimeConversion();
    Test_GpxWriter();
    (void)printf("bike_core_test: PASS\n");

    return 0;
}
