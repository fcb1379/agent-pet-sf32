#include "bike_nmea.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define BIKE_NMEA_TOKEN_MAX (20U)
#define BIKE_NMEA_COORD_SCALE (100000LL)
#define BIKE_NMEA_DEGREE_E7 (10000000LL)

/* BikeNmea_HexValue: 将 ASCII 十六进制字符转换为数值。
 * 参数：
 *   - cValue: 待转换字符
 *   - pValue: 输出数值
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeNmea_HexValue(char cValue, uint8_t *pValue)
{
    bool bResult;

    bResult = true;
    if (NULL == pValue)
    {
        bResult = false;
    }
    else if (('0' <= cValue) && ('9' >= cValue))
    {
        *pValue = (uint8_t)(cValue - '0');
    }
    else if (('A' <= cValue) && ('F' >= cValue))
    {
        *pValue = (uint8_t)(cValue - 'A' + 10);
    }
    else if (('a' <= cValue) && ('f' >= cValue))
    {
        *pValue = (uint8_t)(cValue - 'a' + 10);
    }
    else
    {
        bResult = false;
    }

    return bResult;
}

/* BikeNmea_VerifyChecksum: 验证 NMEA 语句异或校验。
 * 参数：
 *   - pSentence: 以 '$' 开头、以 NUL 结尾的语句
 * 返回值：校验正确返回 true，否则返回 false
 */
static bool BikeNmea_VerifyChecksum(const char *pSentence)
{
    const char *pCursor;
    const char *pStar;
    uint8_t ucExpectedHigh;
    uint8_t ucExpectedLow;
    uint8_t ucChecksum;
    bool bResult;

    bResult = false;
    if ((NULL == pSentence) || ('$' != pSentence[0]))
    {
        return bResult;
    }

    pStar = strchr(pSentence, '*');
    if ((NULL == pStar) || ('\0' == pStar[1]) || ('\0' == pStar[2]) || ('\0' != pStar[3]))
    {
        return bResult;
    }

    if ((!BikeNmea_HexValue(pStar[1], &ucExpectedHigh)) ||
            (!BikeNmea_HexValue(pStar[2], &ucExpectedLow)))
    {
        return bResult;
    }

    ucChecksum = 0U;
    pCursor = &pSentence[1];
    while (pCursor < pStar)
    {
        ucChecksum ^= (uint8_t)(*pCursor);
        pCursor++;
    }

    if (ucChecksum == (uint8_t)((ucExpectedHigh << 4U) | ucExpectedLow))
    {
        bResult = true;
    }

    return bResult;
}

/* BikeNmea_ParseUnsigned: 解析无符号十进制整数。
 * 参数：
 *   - pText: 输入字段
 *   - pValue: 输出数值
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeNmea_ParseUnsigned(const char *pText, uint32_t *pValue)
{
    uint64_t udValue;
    const char *pCursor;
    bool bResult;

    bResult = false;
    if ((NULL == pText) || (NULL == pValue) || ('\0' == pText[0]))
    {
        return bResult;
    }

    udValue = 0ULL;
    pCursor = pText;
    while ('\0' != *pCursor)
    {
        if (('0' > *pCursor) || ('9' < *pCursor))
        {
            return bResult;
        }

        udValue = (udValue * 10ULL) + (uint64_t)(*pCursor - '0');
        if (UINT32_MAX < udValue)
        {
            return bResult;
        }
        pCursor++;
    }

    *pValue = (uint32_t)udValue;
    bResult = true;

    return bResult;
}

/* BikeNmea_ParseScaled: 将十进制字段解析为指定倍率的有符号整数。
 * 参数：
 *   - pText: 输入字段
 *   - ulScale: 小数倍率，必须为 10 的整数次幂
 *   - pValue: 输出数值
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeNmea_ParseScaled(const char *pText, uint32_t ulScale, int64_t *pValue)
{
    const char *pCursor;
    uint64_t udInteger;
    uint64_t udFraction;
    uint32_t ulFractionScale;
    bool bNegative;
    bool bHasDigit;
    bool bResult;

    bResult = false;
    if ((NULL == pText) || (NULL == pValue) || (0U == ulScale) || ('\0' == pText[0]))
    {
        return bResult;
    }

    pCursor = pText;
    bNegative = false;
    if ('-' == *pCursor)
    {
        bNegative = true;
        pCursor++;
    }
    else if ('+' == *pCursor)
    {
        pCursor++;
    }

    udInteger = 0ULL;
    bHasDigit = false;
    while (('0' <= *pCursor) && ('9' >= *pCursor))
    {
        bHasDigit = true;
        udInteger = (udInteger * 10ULL) + (uint64_t)(*pCursor - '0');
        if (INT64_MAX / (uint64_t)ulScale < udInteger)
        {
            return bResult;
        }
        pCursor++;
    }

    udFraction = 0ULL;
    ulFractionScale = ulScale;
    if ('.' == *pCursor)
    {
        pCursor++;
        while (('0' <= *pCursor) && ('9' >= *pCursor))
        {
            if (1U < ulFractionScale)
            {
                ulFractionScale /= 10U;
                udFraction += (uint64_t)(*pCursor - '0') * (uint64_t)ulFractionScale;
            }
            pCursor++;
        }
    }

    if ((!bHasDigit) || ('\0' != *pCursor))
    {
        return bResult;
    }

    *pValue = (int64_t)((udInteger * (uint64_t)ulScale) + udFraction);
    if (bNegative)
    {
        *pValue = -*pValue;
    }
    bResult = true;

    return bResult;
}

/* BikeNmea_ParseCoordinate: 将 NMEA ddmm.mmmmm 坐标转换为 1e-7 度。
 * 参数：
 *   - pText: 坐标字段
 *   - cHemisphere: N/S/E/W 半球字符
 *   - pCoordinateE7: 输出坐标
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeNmea_ParseCoordinate(const char *pText, char cHemisphere, int32_t *pCoordinateE7)
{
    int64_t dRaw;
    int64_t dDegrees;
    int64_t dMinutes;
    int64_t dCoordinate;
    bool bResult;

    bResult = false;
    if ((NULL == pText) || (NULL == pCoordinateE7) ||
            (!BikeNmea_ParseScaled(pText, (uint32_t)BIKE_NMEA_COORD_SCALE, &dRaw)) ||
            (0LL > dRaw))
    {
        return bResult;
    }

    dDegrees = dRaw / (100LL * BIKE_NMEA_COORD_SCALE);
    dMinutes = dRaw % (100LL * BIKE_NMEA_COORD_SCALE);
    dCoordinate = (dDegrees * BIKE_NMEA_DEGREE_E7) +
                  ((dMinutes * BIKE_NMEA_DEGREE_E7) / (60LL * BIKE_NMEA_COORD_SCALE));

    if (('S' == cHemisphere) || ('W' == cHemisphere))
    {
        dCoordinate = -dCoordinate;
    }
    else if (('N' != cHemisphere) && ('E' != cHemisphere))
    {
        return bResult;
    }

    if ((INT32_MIN > dCoordinate) || (INT32_MAX < dCoordinate))
    {
        return bResult;
    }

    *pCoordinateE7 = (int32_t)dCoordinate;
    bResult = true;

    return bResult;
}

/* BikeNmea_ParseTime: 解析 hhmmss.sss UTC 时间。
 * 参数：
 *   - pText: 时间字段
 *   - pData: 定位快照
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeNmea_ParseTime(const char *pText, BIKE_GNSS_DATA *pData)
{
    bool bResult;

    bResult = false;
    if ((NULL == pText) || (NULL == pData) || (6U > strlen(pText)))
    {
        return bResult;
    }

    if (('0' <= pText[0]) && ('9' >= pText[0]) &&
            ('0' <= pText[1]) && ('9' >= pText[1]) &&
            ('0' <= pText[2]) && ('9' >= pText[2]) &&
            ('0' <= pText[3]) && ('9' >= pText[3]) &&
            ('0' <= pText[4]) && ('9' >= pText[4]) &&
            ('0' <= pText[5]) && ('9' >= pText[5]))
    {
        pData->ucHour = (uint8_t)(((pText[0] - '0') * 10) + (pText[1] - '0'));
        pData->ucMinute = (uint8_t)(((pText[2] - '0') * 10) + (pText[3] - '0'));
        pData->ucSecond = (uint8_t)(((pText[4] - '0') * 10) + (pText[5] - '0'));
        if ((24U > pData->ucHour) && (60U > pData->ucMinute) && (60U > pData->ucSecond))
        {
            bResult = true;
        }
    }

    return bResult;
}

/* BikeNmea_ParseDate: 解析 ddmmyy UTC 日期。
 * 参数：
 *   - pText: 日期字段
 *   - pData: 定位快照
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeNmea_ParseDate(const char *pText, BIKE_GNSS_DATA *pData)
{
    bool bResult;

    bResult = false;
    if ((NULL == pText) || (NULL == pData) || (6U != strlen(pText)))
    {
        return bResult;
    }

    if (('0' <= pText[0]) && ('9' >= pText[0]) &&
            ('0' <= pText[1]) && ('9' >= pText[1]) &&
            ('0' <= pText[2]) && ('9' >= pText[2]) &&
            ('0' <= pText[3]) && ('9' >= pText[3]) &&
            ('0' <= pText[4]) && ('9' >= pText[4]) &&
            ('0' <= pText[5]) && ('9' >= pText[5]))
    {
        pData->ucDay = (uint8_t)(((pText[0] - '0') * 10) + (pText[1] - '0'));
        pData->ucMonth = (uint8_t)(((pText[2] - '0') * 10) + (pText[3] - '0'));
        pData->usYear = (uint16_t)(((uint16_t)(pText[4] - '0') * 10U) +
                                  (uint16_t)(pText[5] - '0'));
        pData->usYear = (uint16_t)(pData->usYear + ((80U <= pData->usYear) ? 1900U : 2000U));
        if ((1U <= pData->ucDay) && (31U >= pData->ucDay) &&
                (1U <= pData->ucMonth) && (12U >= pData->ucMonth))
        {
            bResult = true;
        }
    }

    return bResult;
}

/* BikeNmea_Tokenize: 原地分割 NMEA 字段。
 * 参数：
 *   - pSentence: 已通过校验的可写语句
 *   - apToken: 输出字段指针数组
 *   - ucCapacity: 指针数组容量
 * 返回值：实际字段数量，格式错误返回 0
 */
static uint8_t BikeNmea_Tokenize(char *pSentence, char **apToken, uint8_t ucCapacity)
{
    char *pCursor;
    uint8_t ucCount;

    if ((NULL == pSentence) || (NULL == apToken) || (0U == ucCapacity) || ('$' != pSentence[0]))
    {
        return 0U;
    }

    pCursor = &pSentence[1];
    apToken[0] = pCursor;
    ucCount = 1U;
    while ('\0' != *pCursor)
    {
        if ((',' == *pCursor) || ('*' == *pCursor))
        {
            *pCursor = '\0';
            if (ucCount < ucCapacity)
            {
                apToken[ucCount] = &pCursor[1];
                ucCount++;
            }
            else
            {
                return 0U;
            }
        }
        pCursor++;
    }

    return ucCount;
}

/* BikeNmea_ParseGga: 解析 GGA 定位质量、卫星数和海拔。
 * 参数：
 *   - apToken: 字段数组
 *   - ucCount: 字段数量
 *   - pData: 定位快照
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeNmea_ParseGga(char **apToken, uint8_t ucCount, BIKE_GNSS_DATA *pData)
{
    uint32_t ulFixQuality;
    uint32_t ulSatellites;
    int64_t dAltitudeCm;
    int32_t lLatitudeE7;
    int32_t lLongitudeE7;
    bool bResult;

    bResult = false;
    if ((NULL == apToken) || (NULL == pData) || (10U >= ucCount))
    {
        return bResult;
    }

    if ((!BikeNmea_ParseUnsigned(apToken[6], &ulFixQuality)) ||
            (!BikeNmea_ParseUnsigned(apToken[7], &ulSatellites)) ||
            (UINT8_MAX < ulFixQuality) || (UINT8_MAX < ulSatellites))
    {
        return bResult;
    }

    (void)BikeNmea_ParseTime(apToken[1], pData);
    pData->ucFixQuality = (uint8_t)ulFixQuality;
    pData->ucSatellites = (uint8_t)ulSatellites;
    pData->bFixValid = (0U != pData->ucFixQuality);

    if (pData->bFixValid)
    {
        if (BikeNmea_ParseCoordinate(apToken[2], apToken[3][0], &lLatitudeE7) &&
                BikeNmea_ParseCoordinate(apToken[4], apToken[5][0], &lLongitudeE7))
        {
            pData->lLatitudeE7 = lLatitudeE7;
            pData->lLongitudeE7 = lLongitudeE7;
        }
        else
        {
            pData->bFixValid = false;
        }
    }

    if (BikeNmea_ParseScaled(apToken[9], 100U, &dAltitudeCm) &&
            (INT32_MIN <= dAltitudeCm) && (INT32_MAX >= dAltitudeCm))
    {
        pData->lAltitudeCm = (int32_t)dAltitudeCm;
    }

    bResult = true;

    return bResult;
}

/* BikeNmea_ParseRmc: 解析 RMC 有效性、坐标、速度、航向和日期。
 * 参数：
 *   - apToken: 字段数组
 *   - ucCount: 字段数量
 *   - pData: 定位快照
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeNmea_ParseRmc(char **apToken, uint8_t ucCount, BIKE_GNSS_DATA *pData)
{
    int64_t dSpeedMilliKnots;
    int64_t dCourseDeg10;
    int32_t lLatitudeE7;
    int32_t lLongitudeE7;
    bool bResult;

    bResult = false;
    if ((NULL == apToken) || (NULL == pData) || (10U >= ucCount))
    {
        return bResult;
    }

    (void)BikeNmea_ParseTime(apToken[1], pData);
    (void)BikeNmea_ParseDate(apToken[9], pData);
    pData->bFixValid = ('A' == apToken[2][0]);
    if (!pData->bFixValid)
    {
        pData->ulSpeedCmPerSec = 0U;
        bResult = true;
        return bResult;
    }

    if ((!BikeNmea_ParseCoordinate(apToken[3], apToken[4][0], &lLatitudeE7)) ||
            (!BikeNmea_ParseCoordinate(apToken[5], apToken[6][0], &lLongitudeE7)))
    {
        return bResult;
    }

    pData->lLatitudeE7 = lLatitudeE7;
    pData->lLongitudeE7 = lLongitudeE7;

    if (BikeNmea_ParseScaled(apToken[7], 1000U, &dSpeedMilliKnots) && (0LL <= dSpeedMilliKnots))
    {
        pData->ulSpeedCmPerSec = (uint32_t)((dSpeedMilliKnots * 51444LL + 500000LL) / 1000000LL);
    }
    else
    {
        pData->ulSpeedCmPerSec = 0U;
    }

    if (BikeNmea_ParseScaled(apToken[8], 10U, &dCourseDeg10) &&
            (0LL <= dCourseDeg10) && (3600LL >= dCourseDeg10))
    {
        pData->usCourseDeg10 = (uint16_t)dCourseDeg10;
    }

    bResult = true;

    return bResult;
}

/* BikeNmea_ParseVtg: 解析 VTG 对地航向和速度。
 * 参数：
 *   - apToken: 字段数组
 *   - ucCount: 字段数量
 *   - pData: 定位快照
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeNmea_ParseVtg(char **apToken, uint8_t ucCount,
                              BIKE_GNSS_DATA *pData)
{
    int64_t dCourseDeg10;
    int64_t dSpeedMilliKph;
    int64_t dSpeedMilliKnots;
    uint64_t udSpeedCmPerSec;
    uint16_t usCourseDeg10;
    bool bCourseValid;
    bool bSpeedValid;

    if ((NULL == apToken) || (NULL == pData) || (10U > ucCount) ||
        (0 != strcmp(apToken[2], "T")) ||
        (0 != strcmp(apToken[6], "N")) ||
        (0 != strcmp(apToken[8], "K")))
    {
        return false;
    }

    /* NMEA 2.3 的 mode=N 表示数据无效；旧格式没有 mode 字段。 */
    if ((11U <= ucCount) && ('N' == apToken[9][0]) &&
        ('\0' == apToken[9][1]))
    {
        pData->ulSpeedCmPerSec = 0U;
        return true;
    }

    bCourseValid = BikeNmea_ParseScaled(apToken[1], 10U, &dCourseDeg10) &&
                   (0LL <= dCourseDeg10) && (3600LL >= dCourseDeg10);
    if (bCourseValid)
    {
        usCourseDeg10 = (uint16_t)dCourseDeg10;
    }
    else if ('\0' != apToken[1][0])
    {
        return false;
    }
    else
    {
        usCourseDeg10 = pData->usCourseDeg10;
    }

    bSpeedValid = BikeNmea_ParseScaled(apToken[7], 1000U,
                                       &dSpeedMilliKph) &&
                  (0LL <= dSpeedMilliKph);
    if (bSpeedValid)
    {
        udSpeedCmPerSec = ((uint64_t)dSpeedMilliKph + 18ULL) / 36ULL;
    }
    else
    {
        bSpeedValid = BikeNmea_ParseScaled(apToken[5], 1000U,
                                           &dSpeedMilliKnots) &&
                      (0LL <= dSpeedMilliKnots);
        if (!bSpeedValid)
        {
            return false;
        }
        if ((UINT64_MAX - 500000ULL) / 51444ULL <
            (uint64_t)dSpeedMilliKnots)
        {
            return false;
        }
        udSpeedCmPerSec = ((uint64_t)dSpeedMilliKnots * 51444ULL +
                           500000ULL) / 1000000ULL;
    }
    if (UINT32_MAX < udSpeedCmPerSec)
    {
        return false;
    }
    pData->usCourseDeg10 = usCourseDeg10;
    pData->ulSpeedCmPerSec = (uint32_t)udSpeedCmPerSec;

    return true;
}

/* BikeNmea_ParseSentence: 解析一条完整且已去除换行的 NMEA 语句。
 * 参数：
 *   - pParser: 解析器实例
 * 返回值：语句解析结果
 */
static BIKE_NMEA_RESULT BikeNmea_ParseSentence(BIKE_NMEA_PARSER *pParser)
{
    char *apToken[BIKE_NMEA_TOKEN_MAX];
    const char *pType;
    size_t ulIdentifierLength;
    uint8_t ucCount;
    BIKE_NMEA_RESULT eResult;

    eResult = BIKE_NMEA_RESULT_ERROR;
    if ((NULL == pParser) || (!BikeNmea_VerifyChecksum(pParser->aSentence)))
    {
        if (NULL != pParser)
        {
            pParser->ulChecksumErrorCount++;
        }
        return eResult;
    }

    ucCount = BikeNmea_Tokenize(pParser->aSentence, apToken, BIKE_NMEA_TOKEN_MAX);
    if (0U == ucCount)
    {
        return eResult;
    }

    ulIdentifierLength = strlen(apToken[0]);
    if (3U > ulIdentifierLength)
    {
        return eResult;
    }
    pType = &apToken[0][ulIdentifierLength - 3U];

    if (0 == strcmp(pType, "GGA"))
    {
        if (BikeNmea_ParseGga(apToken, ucCount, &pParser->tData))
        {
            eResult = BIKE_NMEA_RESULT_GGA;
        }
    }
    else if (0 == strcmp(pType, "RMC"))
    {
        if (BikeNmea_ParseRmc(apToken, ucCount, &pParser->tData))
        {
            eResult = BIKE_NMEA_RESULT_RMC;
        }
    }
    else if (0 == strcmp(pType, "VTG"))
    {
        if (BikeNmea_ParseVtg(apToken, ucCount, &pParser->tData))
        {
            eResult = BIKE_NMEA_RESULT_VTG;
        }
    }
    else
    {
        eResult = BIKE_NMEA_RESULT_NONE;
    }

    if ((BIKE_NMEA_RESULT_GGA == eResult) ||
        (BIKE_NMEA_RESULT_RMC == eResult) ||
        (BIKE_NMEA_RESULT_VTG == eResult))
    {
        pParser->ulAcceptedCount++;
    }

    return eResult;
}

/* BIKE_NMEA_Init: 初始化固定容量 NMEA 解析器。
 * 参数：
 *   - pParser: 解析器实例
 * 返回值：无
 */
void BIKE_NMEA_Init(BIKE_NMEA_PARSER *pParser)
{
    if (NULL != pParser)
    {
        (void)memset(pParser, 0, sizeof(*pParser));
    }

    return;
}

/* BIKE_NMEA_Feed: 输入一个串口字节并在完整语句到达时解析。
 * 参数：
 *   - pParser: 解析器实例
 *   - ucByte: 串口字节
 * 返回值：本字节触发的解析结果
 */
BIKE_NMEA_RESULT BIKE_NMEA_Feed(BIKE_NMEA_PARSER *pParser, uint8_t ucByte)
{
    BIKE_NMEA_RESULT eResult;

    eResult = BIKE_NMEA_RESULT_NONE;
    if (NULL == pParser)
    {
        return BIKE_NMEA_RESULT_ERROR;
    }

    if ('$' == (char)ucByte)
    {
        pParser->usLength = 0U;
        pParser->bCollecting = true;
        pParser->aSentence[pParser->usLength] = (char)ucByte;
        pParser->usLength++;
    }
    else if (pParser->bCollecting)
    {
        if (('\r' == (char)ucByte) || ('\n' == (char)ucByte))
        {
            if (0U < pParser->usLength)
            {
                pParser->aSentence[pParser->usLength] = '\0';
                eResult = BikeNmea_ParseSentence(pParser);
            }
            pParser->usLength = 0U;
            pParser->bCollecting = false;
        }
        else if (pParser->usLength < (BIKE_NMEA_SENTENCE_MAX - 1U))
        {
            pParser->aSentence[pParser->usLength] = (char)ucByte;
            pParser->usLength++;
        }
        else
        {
            pParser->usLength = 0U;
            pParser->bCollecting = false;
            pParser->ulOverflowCount++;
            eResult = BIKE_NMEA_RESULT_ERROR;
        }
    }

    return eResult;
}

/* BIKE_NMEA_GetData: 获取解析器最近的定位快照只读指针。
 * 参数：
 *   - pParser: 解析器实例
 * 返回值：成功返回快照指针，参数无效返回 NULL
 */
const BIKE_GNSS_DATA *BIKE_NMEA_GetData(const BIKE_NMEA_PARSER *pParser)
{
    const BIKE_GNSS_DATA *pData;

    pData = NULL;
    if (NULL != pParser)
    {
        pData = &pParser->tData;
    }

    return pData;
}
