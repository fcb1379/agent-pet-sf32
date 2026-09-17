#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bike_nmea.h"
#include "bike_ride_model.h"

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

int main(void)
{
    Test_NmeaParser();
    Test_RideModel();
    (void)printf("bike_core_test: PASS\n");

    return 0;
}
