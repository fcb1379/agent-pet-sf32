#ifndef BIKE_NMEA_H
#define BIKE_NMEA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_NMEA_SENTENCE_MAX (128U)

/* BIKE_NMEA_RESULT: 单字节输入后的解析结果。 */
typedef enum _BIKE_NMEA_RESULT
{
    BIKE_NMEA_RESULT_NONE = 0,
    BIKE_NMEA_RESULT_GGA,
    BIKE_NMEA_RESULT_RMC,
    BIKE_NMEA_RESULT_VTG,
    BIKE_NMEA_RESULT_ERROR
} BIKE_NMEA_RESULT;

/* BIKE_GNSS_DATA: DX-GP10 NMEA 的固定容量定位快照。
 * 成员说明：
 *   - bFixValid: 当前定位是否有效
 *   - ucFixQuality: GGA 定位质量，0 表示无效
 *   - ucSatellites: 当前参与定位的卫星数
 *   - ucHour/ucMinute/ucSecond: UTC 时间
 *   - ucDay/ucMonth/usYear: UTC 日期
 *   - lLatitudeE7/lLongitudeE7: 经纬度，单位 1e-7 度
 *   - lAltitudeCm: 海拔，单位厘米
 *   - ulSpeedCmPerSec: 地速，单位厘米每秒
 *   - usCourseDeg10: 航向，单位 0.1 度
 */
typedef struct _BIKE_GNSS_DATA
{
    bool bFixValid;
    uint8_t ucFixQuality;
    uint8_t ucSatellites;
    uint8_t ucHour;
    uint8_t ucMinute;
    uint8_t ucSecond;
    uint8_t ucDay;
    uint8_t ucMonth;
    uint16_t usYear;
    int32_t lLatitudeE7;
    int32_t lLongitudeE7;
    int32_t lAltitudeCm;
    uint32_t ulSpeedCmPerSec;
    uint16_t usCourseDeg10;
} BIKE_GNSS_DATA;

/* BIKE_NMEA_PARSER: 固定容量逐字节 NMEA 解析器。
 * 成员说明：
 *   - aSentence: 当前语句缓冲区，最大 127 字节并保留结尾 NUL
 *   - usLength: 当前已接收长度
 *   - bCollecting: 是否已经接收到语句起始符 '$'
 *   - ulAcceptedCount: 校验及格式均正确的已识别语句数
 *   - ulChecksumErrorCount: 校验失败语句数
 *   - ulOverflowCount: 超长语句数
 *   - tData: 最近一次有效解析得到的定位快照
 */
typedef struct _BIKE_NMEA_PARSER
{
    char aSentence[BIKE_NMEA_SENTENCE_MAX];
    uint16_t usLength;
    bool bCollecting;
    uint32_t ulAcceptedCount;
    uint32_t ulChecksumErrorCount;
    uint32_t ulOverflowCount;
    BIKE_GNSS_DATA tData;
} BIKE_NMEA_PARSER;

void BIKE_NMEA_Init(BIKE_NMEA_PARSER *pParser);
BIKE_NMEA_RESULT BIKE_NMEA_Feed(BIKE_NMEA_PARSER *pParser, uint8_t ucByte);
const BIKE_GNSS_DATA *BIKE_NMEA_GetData(const BIKE_NMEA_PARSER *pParser);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_NMEA_H */
