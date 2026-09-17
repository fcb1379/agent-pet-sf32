#include "bike_compass.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#define BIKE_COMPASS_PI (3.14159265358979323846)
#define BIKE_COMPASS_DEG10_PER_RADIAN (1800.0 / BIKE_COMPASS_PI)

/* BIKE_COMPASS_ResetCalibration: 清空水平面硬铁偏移校准状态。
 * 参数：
 *   - pCalibration: 待清空的校准对象
 * 返回值：无
 */
void BIKE_COMPASS_ResetCalibration(BIKE_COMPASS_CALIBRATION *pCalibration)
{
    if (NULL != pCalibration)
    {
        (void)memset(pCalibration, 0, sizeof(*pCalibration));
    }

    return;
}

/* BikeCompass_GetSpan: 计算有符号最小值和最大值之间的非负跨度。
 * 参数：
 *   - lMinimum: 已观测最小值
 *   - lMaximum: 已观测最大值
 * 返回值：饱和到 UINT32_MAX 的跨度
 */
static uint32_t BikeCompass_GetSpan(int32_t lMinimum, int32_t lMaximum)
{
    uint64_t udSpan;

    if (lMaximum < lMinimum)
    {
        return 0U;
    }
    udSpan = (uint64_t)((int64_t)lMaximum - (int64_t)lMinimum);

    return (UINT32_MAX < udSpan) ? UINT32_MAX : (uint32_t)udSpan;
}

/* BIKE_COMPASS_UpdateCalibration: 更新 X/Y 轴极值并在转动范围足够后
 * 输出去除硬铁中心偏移的水平面向量。
 * 参数：
 *   - pCalibration: 运行时校准状态
 *   - lRawX/lRawY: 当前传感器中心化原始计数
 *   - ulMinimumSpan: 单轴进入就绪状态所需的最小跨度
 *   - ulMinimumSamples: 进入就绪状态所需的最少样本数
 *   - pCorrectedX/pCorrectedY: 输出去偏后的水平面向量
 * 返回值：校准范围和样本数均满足要求返回 true，否则返回 false
 */
bool BIKE_COMPASS_UpdateCalibration(BIKE_COMPASS_CALIBRATION *pCalibration,
                                    int32_t lRawX, int32_t lRawY,
                                    uint32_t ulMinimumSpan,
                                    uint32_t ulMinimumSamples,
                                    int32_t *pCorrectedX,
                                    int32_t *pCorrectedY)
{
    int64_t dCenterX;
    int64_t dCenterY;
    uint32_t ulSpanX;
    uint32_t ulSpanY;

    if ((NULL == pCalibration) || (NULL == pCorrectedX) ||
        (NULL == pCorrectedY) || (0U == ulMinimumSpan) ||
        (0U == ulMinimumSamples))
    {
        return false;
    }
    if (!pCalibration->bInitialized)
    {
        pCalibration->lMinimumX = lRawX;
        pCalibration->lMaximumX = lRawX;
        pCalibration->lMinimumY = lRawY;
        pCalibration->lMaximumY = lRawY;
        pCalibration->ulSampleCount = 1U;
        pCalibration->bInitialized = true;
    }
    else
    {
        if (lRawX < pCalibration->lMinimumX)
        {
            pCalibration->lMinimumX = lRawX;
        }
        if (lRawX > pCalibration->lMaximumX)
        {
            pCalibration->lMaximumX = lRawX;
        }
        if (lRawY < pCalibration->lMinimumY)
        {
            pCalibration->lMinimumY = lRawY;
        }
        if (lRawY > pCalibration->lMaximumY)
        {
            pCalibration->lMaximumY = lRawY;
        }
        if (UINT32_MAX != pCalibration->ulSampleCount)
        {
            pCalibration->ulSampleCount++;
        }
    }

    ulSpanX = BikeCompass_GetSpan(pCalibration->lMinimumX,
                                  pCalibration->lMaximumX);
    ulSpanY = BikeCompass_GetSpan(pCalibration->lMinimumY,
                                  pCalibration->lMaximumY);
    if ((ulMinimumSamples > pCalibration->ulSampleCount) ||
        (ulMinimumSpan > ulSpanX) || (ulMinimumSpan > ulSpanY))
    {
        return false;
    }

    dCenterX = ((int64_t)pCalibration->lMinimumX +
                (int64_t)pCalibration->lMaximumX) / 2LL;
    dCenterY = ((int64_t)pCalibration->lMinimumY +
                (int64_t)pCalibration->lMaximumY) / 2LL;
    *pCorrectedX = (int32_t)((int64_t)lRawX - dCenterX);
    *pCorrectedY = (int32_t)((int64_t)lRawY - dCenterY);

    return true;
}

/* BIKE_COMPASS_GetCalibrationPercent: 按 X/Y 较小跨度计算校准进度。
 * 参数：
 *   - pCalibration: 当前校准状态
 *   - ulMinimumSpan: 满量程进度对应的单轴最小跨度
 * 返回值：0~100 的校准进度
 */
uint8_t BIKE_COMPASS_GetCalibrationPercent(
    const BIKE_COMPASS_CALIBRATION *pCalibration, uint32_t ulMinimumSpan)
{
    uint32_t ulSpanX;
    uint32_t ulSpanY;
    uint32_t ulMinimumObservedSpan;
    uint64_t udPercent;

    if ((NULL == pCalibration) || (!pCalibration->bInitialized) ||
        (0U == ulMinimumSpan))
    {
        return 0U;
    }
    ulSpanX = BikeCompass_GetSpan(pCalibration->lMinimumX,
                                  pCalibration->lMaximumX);
    ulSpanY = BikeCompass_GetSpan(pCalibration->lMinimumY,
                                  pCalibration->lMaximumY);
    ulMinimumObservedSpan = (ulSpanX < ulSpanY) ? ulSpanX : ulSpanY;
    udPercent = ((uint64_t)ulMinimumObservedSpan * 100ULL) /
                (uint64_t)ulMinimumSpan;

    return (100ULL < udPercent) ? 100U : (uint8_t)udPercent;
}

/* BIKE_COMPASS_CalculateHeadingDeg10: 将水平面向量转换为 0~359.9 度。
 * 参数：
 *   - lX/lY: 已去除中心偏移的水平面向量
 *   - pHeadingDeg10: 输出 0.1 度单位航向
 * 返回值：向量非零且输出指针有效返回 true，否则返回 false
 */
bool BIKE_COMPASS_CalculateHeadingDeg10(int32_t lX, int32_t lY,
                                        uint16_t *pHeadingDeg10)
{
    double dHeadingDeg10;
    uint32_t ulRoundedHeading;

    if ((NULL == pHeadingDeg10) || ((0 == lX) && (0 == lY)))
    {
        return false;
    }
    dHeadingDeg10 = atan2((double)lY, (double)lX) *
                    BIKE_COMPASS_DEG10_PER_RADIAN;
    if (0.0 > dHeadingDeg10)
    {
        dHeadingDeg10 += 3600.0;
    }
    ulRoundedHeading = (uint32_t)(dHeadingDeg10 + 0.5);
    if (3600U <= ulRoundedHeading)
    {
        ulRoundedHeading -= 3600U;
    }
    *pHeadingDeg10 = (uint16_t)ulRoundedHeading;

    return true;
}

#ifndef BIKE_COMPASS_HOST_BUILD

#include <board.h>
#include <bf0_hal.h>
#include <drivers/i2c.h>
#include <rtdevice.h>
#include <rtthread.h>

#define LOG_TAG "bike.mag"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#define BIKE_COMPASS_I2C_NAME "i2c2"
#define BIKE_COMPASS_I2C_ADDRESS (0x30U)
#define BIKE_COMPASS_DATA_REGISTER (0x00U)
#define BIKE_COMPASS_STATUS_REGISTER (0x18U)
#define BIKE_COMPASS_CONTROL0_REGISTER (0x1BU)
#define BIKE_COMPASS_CONTROL1_REGISTER (0x1CU)
#define BIKE_COMPASS_PRODUCT_ID_REGISTER (0x39U)
#define BIKE_COMPASS_PRODUCT_ID_VALUE (0x10U)
#define BIKE_COMPASS_MEASUREMENT_READY_MASK (0x40U)
#define BIKE_COMPASS_AUTO_SET_RESET_MASK (0x20U)
#define BIKE_COMPASS_TRIGGER_MEASUREMENT_MASK (0x01U)
#define BIKE_COMPASS_SOFTWARE_RESET_MASK (0x80U)
#define BIKE_COMPASS_NULL_FIELD_COUNT (524288L)
#define BIKE_COMPASS_COUNTS_PER_MILLIGAUSS (16L)
#define BIKE_COMPASS_DATA_LENGTH (9U)
#define BIKE_COMPASS_MEASUREMENT_TIMEOUT_MS (20U)
#define BIKE_COMPASS_SAMPLE_PERIOD_MS (200U)
#define BIKE_COMPASS_MINIMUM_SPAN_COUNTS (4096U)
#define BIKE_COMPASS_MINIMUM_CALIBRATION_SAMPLES (32U)
#define BIKE_COMPASS_ERROR_THRESHOLD (3U)
#define BIKE_COMPASS_THREAD_STACK_SIZE (2048U)
#define BIKE_COMPASS_THREAD_PRIORITY (21U)

/* l_tBikeCompassSnapshot: UI 与采样线程共享的电子罗盘快照。 */
static BIKE_COMPASS_SNAPSHOT l_tBikeCompassSnapshot;

/* l_tBikeCompassCalibration: 采样线程独占的水平面运行时校准状态。 */
static BIKE_COMPASS_CALIBRATION l_tBikeCompassCalibration;

/* l_tBikeCompassMutex: 保护电子罗盘快照的静态互斥锁。 */
static struct rt_mutex l_tBikeCompassMutex;

/* l_tBikeCompassThread: 板载磁力计静态采样线程控制块。 */
static struct rt_thread l_tBikeCompassThread;

/* l_aBikeCompassThreadStack: 磁力计采样线程固定 2048 字节栈。 */
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikeCompassThreadStack[BIKE_COMPASS_THREAD_STACK_SIZE];

/* l_pBikeCompassBus: I2C2 总线句柄，初始化前为 NULL。 */
static struct rt_i2c_bus_device *l_pBikeCompassBus;

/* l_bBikeCompassInitialized: 电子罗盘已执行初始化的标志。 */
static bool l_bBikeCompassInitialized;

/* BikeCompass_Lock: 获取电子罗盘快照互斥锁。
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeCompass_Lock(void)
{
    return l_bBikeCompassInitialized &&
           (RT_EOK == rt_mutex_take(&l_tBikeCompassMutex,
                                    RT_WAITING_FOREVER));
}

/* BikeCompass_Unlock: 释放电子罗盘快照互斥锁。
 * 返回值：无
 */
static void BikeCompass_Unlock(void)
{
    (void)rt_mutex_release(&l_tBikeCompassMutex);

    return;
}

/* BikeCompass_SetStatus: 在线程安全快照中更新磁力计状态。
 * 参数：
 *   - eStatus: 新状态
 * 返回值：无
 */
static void BikeCompass_SetStatus(BIKE_COMPASS_STATUS eStatus)
{
    if (BikeCompass_Lock())
    {
        l_tBikeCompassSnapshot.eStatus = eStatus;
        BikeCompass_Unlock();
    }

    return;
}

/* BikeCompass_ConfigurePins: 将 PA39/PA40 配置为黄山派传感器 I2C2。
 * 返回值：当前 LCD 接口允许复用时返回 true，否则返回 false
 */
static bool BikeCompass_ConfigurePins(void)
{
#if defined(BSP_LCDC_USING_QADSPI) || defined(BSP_LCDC_USING_SPI_DCX_1DATA)
    HAL_PIN_Set(PAD_PA39, I2C2_SDA, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA40, I2C2_SCL, PIN_PULLUP, 1);

    return true;
#else
    LOG_E("PA39/PA40 reserved by current LCD interface");
    return false;
#endif
}

/* BikeCompass_ReadRegisters: 通过带返回值检查的 I2C 事务读取寄存器。
 * 参数：
 *   - ucRegister: 起始寄存器
 *   - pData: 输出数据
 *   - usLength: 读取长度
 * 返回值：完整读取返回 true，否则返回 false
 */
static bool BikeCompass_ReadRegisters(uint8_t ucRegister, uint8_t *pData,
                                      uint16_t usLength)
{
    struct rt_i2c_msg aMessages[2];

    if ((NULL == l_pBikeCompassBus) || (NULL == pData) || (0U == usLength))
    {
        return false;
    }
    (void)memset(aMessages, 0, sizeof(aMessages));
    aMessages[0].addr = BIKE_COMPASS_I2C_ADDRESS;
    aMessages[0].flags = RT_I2C_WR;
    aMessages[0].buf = &ucRegister;
    aMessages[0].len = 1U;
    aMessages[1].addr = BIKE_COMPASS_I2C_ADDRESS;
    aMessages[1].flags = RT_I2C_RD;
    aMessages[1].buf = pData;
    aMessages[1].len = usLength;

    return (2 == rt_i2c_transfer(l_pBikeCompassBus, aMessages, 2U));
}

/* BikeCompass_WriteRegister: 通过带返回值检查的 I2C 事务写单字节寄存器。
 * 参数：
 *   - ucRegister: 目标寄存器
 *   - ucValue: 待写值
 * 返回值：完整写入返回 true，否则返回 false
 */
static bool BikeCompass_WriteRegister(uint8_t ucRegister, uint8_t ucValue)
{
    struct rt_i2c_msg tMessage;
    uint8_t aWriteData[2];

    if (NULL == l_pBikeCompassBus)
    {
        return false;
    }
    aWriteData[0] = ucRegister;
    aWriteData[1] = ucValue;
    (void)memset(&tMessage, 0, sizeof(tMessage));
    tMessage.addr = BIKE_COMPASS_I2C_ADDRESS;
    tMessage.flags = RT_I2C_WR;
    tMessage.buf = aWriteData;
    tMessage.len = sizeof(aWriteData);

    return (1 == rt_i2c_transfer(l_pBikeCompassBus, &tMessage, 1U));
}

/* BikeCompass_Probe: 校验 MMC5603NJ 产品 ID。
 * 返回值：检测到产品 ID 0x10 返回 true，否则返回 false
 */
static bool BikeCompass_Probe(void)
{
    uint8_t ucDeviceId;

    ucDeviceId = 0U;
    if (!BikeCompass_ReadRegisters(BIKE_COMPASS_PRODUCT_ID_REGISTER,
                                   &ucDeviceId, 1U))
    {
        LOG_E("product ID read failed on %s", BIKE_COMPASS_I2C_NAME);
        return false;
    }
    if (BIKE_COMPASS_PRODUCT_ID_VALUE != ucDeviceId)
    {
        LOG_E("unexpected product ID: 0x%02x", ucDeviceId);
        return false;
    }

    return true;
}

/* BikeCompass_ReadSample: 触发一次带自动 SET/RESET 的测量，等待数据就绪
 * 并读取完整 9 字节三轴 20 位数据。
 * 参数：
 *   - pRawX/pRawY/pRawZ: 输出减去零场中心后的原始计数
 * 返回值：测量和读取成功返回 true，否则返回 false
 */
static bool BikeCompass_ReadSample(int32_t *pRawX, int32_t *pRawY,
                                   int32_t *pRawZ)
{
    uint8_t aData[BIKE_COMPASS_DATA_LENGTH];
    uint8_t ucStatus;
    uint32_t ulRawX;
    uint32_t ulRawY;
    uint32_t ulRawZ;
    uint32_t ulWaitMs;

    if ((NULL == pRawX) || (NULL == pRawY) || (NULL == pRawZ))
    {
        return false;
    }
    if (!BikeCompass_WriteRegister(
            BIKE_COMPASS_CONTROL0_REGISTER,
            BIKE_COMPASS_AUTO_SET_RESET_MASK |
                BIKE_COMPASS_TRIGGER_MEASUREMENT_MASK))
    {
        return false;
    }

    ucStatus = 0U;
    for (ulWaitMs = 0U; ulWaitMs < BIKE_COMPASS_MEASUREMENT_TIMEOUT_MS;
         ulWaitMs++)
    {
        rt_thread_mdelay(1U);
        if (!BikeCompass_ReadRegisters(BIKE_COMPASS_STATUS_REGISTER,
                                       &ucStatus, 1U))
        {
            return false;
        }
        if (0U != (ucStatus & BIKE_COMPASS_MEASUREMENT_READY_MASK))
        {
            break;
        }
    }
    if (0U == (ucStatus & BIKE_COMPASS_MEASUREMENT_READY_MASK))
    {
        return false;
    }
    if (!BikeCompass_ReadRegisters(BIKE_COMPASS_DATA_REGISTER, aData,
                                   sizeof(aData)))
    {
        return false;
    }

    ulRawX = ((uint32_t)aData[0] << 12U) |
             ((uint32_t)aData[1] << 4U) |
             ((uint32_t)aData[6] >> 4U);
    ulRawY = ((uint32_t)aData[2] << 12U) |
             ((uint32_t)aData[3] << 4U) |
             ((uint32_t)aData[7] >> 4U);
    ulRawZ = ((uint32_t)aData[4] << 12U) |
             ((uint32_t)aData[5] << 4U) |
             ((uint32_t)aData[8] >> 4U);
    *pRawX = (int32_t)ulRawX - BIKE_COMPASS_NULL_FIELD_COUNT;
    *pRawY = (int32_t)ulRawY - BIKE_COMPASS_NULL_FIELD_COUNT;
    *pRawZ = (int32_t)ulRawZ - BIKE_COMPASS_NULL_FIELD_COUNT;

    return true;
}

/* BikeCompass_PublishSample: 更新校准、航向和线程安全快照。
 * 参数：
 *   - lRawX/lRawY/lRawZ: 最近三轴中心化原始计数
 * 返回值：样本已形成有效航向返回 true，否则返回 false
 */
static bool BikeCompass_PublishSample(int32_t lRawX, int32_t lRawY,
                                      int32_t lRawZ)
{
    int32_t lCorrectedX;
    int32_t lCorrectedY;
    uint16_t usHeadingDeg10;
    uint8_t ucCalibrationPercent;
    bool bCalibrated;
    bool bHeadingValid;

    lCorrectedX = 0;
    lCorrectedY = 0;
    bCalibrated = BIKE_COMPASS_UpdateCalibration(
        &l_tBikeCompassCalibration, lRawX, lRawY,
        BIKE_COMPASS_MINIMUM_SPAN_COUNTS,
        BIKE_COMPASS_MINIMUM_CALIBRATION_SAMPLES,
        &lCorrectedX, &lCorrectedY);
    ucCalibrationPercent = BIKE_COMPASS_GetCalibrationPercent(
        &l_tBikeCompassCalibration, BIKE_COMPASS_MINIMUM_SPAN_COUNTS);
    usHeadingDeg10 = 0U;
    bHeadingValid = bCalibrated && BIKE_COMPASS_CalculateHeadingDeg10(
        lCorrectedX, lCorrectedY, &usHeadingDeg10);

    if (BikeCompass_Lock())
    {
        l_tBikeCompassSnapshot.eStatus = bHeadingValid ?
                                        BIKE_COMPASS_STATUS_READY :
                                        BIKE_COMPASS_STATUS_CALIBRATING;
        l_tBikeCompassSnapshot.lXMilliGauss =
            lRawX / BIKE_COMPASS_COUNTS_PER_MILLIGAUSS;
        l_tBikeCompassSnapshot.lYMilliGauss =
            lRawY / BIKE_COMPASS_COUNTS_PER_MILLIGAUSS;
        l_tBikeCompassSnapshot.lZMilliGauss =
            lRawZ / BIKE_COMPASS_COUNTS_PER_MILLIGAUSS;
        l_tBikeCompassSnapshot.usHeadingDeg10 = usHeadingDeg10;
        l_tBikeCompassSnapshot.ucCalibrationPercent = ucCalibrationPercent;
        l_tBikeCompassSnapshot.ulLastUpdateMs =
            (uint32_t)rt_tick_get_millisecond();
        BikeCompass_Unlock();
    }

    return bHeadingValid;
}

/* BikeCompass_ThreadEntry: 以 5 Hz 读取磁力计并发布电子罗盘快照。
 * 参数：
 *   - pParameter: 未使用
 * 返回值：无
 */
static void BikeCompass_ThreadEntry(void *pParameter)
{
    int32_t lRawX;
    int32_t lRawY;
    int32_t lRawZ;
    uint32_t ulConsecutiveErrors;

    (void)pParameter;
    ulConsecutiveErrors = 0U;
    while (true)
    {
        if (BikeCompass_ReadSample(&lRawX, &lRawY, &lRawZ))
        {
            ulConsecutiveErrors = 0U;
            (void)BikeCompass_PublishSample(lRawX, lRawY, lRawZ);
        }
        else
        {
            ulConsecutiveErrors++;
            if (BikeCompass_Lock())
            {
                l_tBikeCompassSnapshot.ulReadErrorCount++;
                if (BIKE_COMPASS_ERROR_THRESHOLD <= ulConsecutiveErrors)
                {
                    l_tBikeCompassSnapshot.eStatus =
                        BIKE_COMPASS_STATUS_ERROR;
                }
                BikeCompass_Unlock();
            }
        }
        rt_thread_mdelay(BIKE_COMPASS_SAMPLE_PERIOD_MS);
    }
}

/* BIKE_COMPASS_Init: 初始化 PA39/PA40、I2C2、MMC5603NJ 和固定栈
 * 采样线程，不使用 SDK 中带断言和越界读取的 MMC56x3 封装。
 * 返回值：初始化成功返回 true，否则返回 false
 */
bool BIKE_COMPASS_Init(void)
{
    int32_t lRawX;
    int32_t lRawY;
    int32_t lRawZ;
    rt_err_t eResult;

    if (l_bBikeCompassInitialized)
    {
        return (BIKE_COMPASS_STATUS_ERROR !=
                l_tBikeCompassSnapshot.eStatus);
    }
    (void)memset(&l_tBikeCompassSnapshot, 0,
                 sizeof(l_tBikeCompassSnapshot));
    BIKE_COMPASS_ResetCalibration(&l_tBikeCompassCalibration);
    eResult = rt_mutex_init(&l_tBikeCompassMutex, "bike_mag",
                            RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex init failed: %d", eResult);
        return false;
    }
    l_bBikeCompassInitialized = true;
    BikeCompass_SetStatus(BIKE_COMPASS_STATUS_SEARCHING);

    if (!BikeCompass_ConfigurePins())
    {
        BikeCompass_SetStatus(BIKE_COMPASS_STATUS_ERROR);
        return false;
    }
    rt_thread_mdelay(2U);
    l_pBikeCompassBus = (struct rt_i2c_bus_device *)rt_device_find(
        BIKE_COMPASS_I2C_NAME);
    if ((NULL == l_pBikeCompassBus) || (!BikeCompass_Probe()))
    {
        BikeCompass_SetStatus(BIKE_COMPASS_STATUS_ERROR);
        return false;
    }
    if (!BikeCompass_WriteRegister(BIKE_COMPASS_CONTROL1_REGISTER,
                                   BIKE_COMPASS_SOFTWARE_RESET_MASK))
    {
        BikeCompass_SetStatus(BIKE_COMPASS_STATUS_ERROR);
        return false;
    }
    rt_thread_mdelay(20U);
    if ((!BikeCompass_Probe()) ||
        (!BikeCompass_ReadSample(&lRawX, &lRawY, &lRawZ)))
    {
        BikeCompass_SetStatus(BIKE_COMPASS_STATUS_ERROR);
        LOG_E("MMC5603NJ initialization verification failed");
        return false;
    }
    (void)BikeCompass_PublishSample(lRawX, lRawY, lRawZ);

    eResult = rt_thread_init(&l_tBikeCompassThread, "bike_mag",
                             BikeCompass_ThreadEntry, NULL,
                             l_aBikeCompassThreadStack,
                             sizeof(l_aBikeCompassThreadStack),
                             BIKE_COMPASS_THREAD_PRIORITY, 10U);
    if (RT_EOK == eResult)
    {
        eResult = rt_thread_startup(&l_tBikeCompassThread);
    }
    if (RT_EOK != eResult)
    {
        BikeCompass_SetStatus(BIKE_COMPASS_STATUS_ERROR);
        LOG_E("thread start failed: %d", eResult);
        return false;
    }
    LOG_I("MMC5603NJ compass ready on %s", BIKE_COMPASS_I2C_NAME);

    return true;
}

/* BIKE_COMPASS_GetSnapshot: 获取板载电子罗盘线程安全快照。
 * 参数：
 *   - pSnapshot: 输出快照
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_COMPASS_GetSnapshot(BIKE_COMPASS_SNAPSHOT *pSnapshot)
{
    bool bResult;

    bResult = false;
    if ((NULL != pSnapshot) && BikeCompass_Lock())
    {
        *pSnapshot = l_tBikeCompassSnapshot;
        BikeCompass_Unlock();
        bResult = true;
    }

    return bResult;
}

#endif /* BIKE_COMPASS_HOST_BUILD */
