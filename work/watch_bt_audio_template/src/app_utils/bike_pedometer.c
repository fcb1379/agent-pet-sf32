#include "bike_pedometer.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

/* BIKE_PEDOMETER_ResetAccumulator: 清空硬件计数扩展状态。
 * 参数：
 *   - pAccumulator: 待清空的累计器
 * 返回值：无
 */
void BIKE_PEDOMETER_ResetAccumulator(BIKE_PEDOMETER_ACCUMULATOR *pAccumulator)
{
    if (NULL != pAccumulator)
    {
        (void)memset(pAccumulator, 0, sizeof(*pAccumulator));
    }

    return;
}

/* BIKE_PEDOMETER_UpdateAccumulator: 处理正常递增、16 位回绕、传感器复位
 * 和异常跳变。异常跳变不计入累计值，但会更新基线以便下一帧恢复。
 * 参数：
 *   - pAccumulator: 计步累计器
 *   - usRawStepCount: 当前硬件原始计数
 *   - usMaximumDelta: 单次采样允许的最大增量
 * 返回值：本次计数有效返回 true，否则返回 false
 */
bool BIKE_PEDOMETER_UpdateAccumulator(BIKE_PEDOMETER_ACCUMULATOR *pAccumulator,
                                      uint16_t usRawStepCount,
                                      uint16_t usMaximumDelta)
{
    uint32_t ulDelta;

    if (NULL == pAccumulator)
    {
        return false;
    }
    if (!pAccumulator->bInitialized)
    {
        pAccumulator->ulTotalSteps = usRawStepCount;
        pAccumulator->usPreviousRaw = usRawStepCount;
        pAccumulator->bInitialized = true;
        return true;
    }

    if (usRawStepCount >= pAccumulator->usPreviousRaw)
    {
        ulDelta = (uint32_t)usRawStepCount -
                  (uint32_t)pAccumulator->usPreviousRaw;
    }
    else if ((0xF000U <= pAccumulator->usPreviousRaw) &&
             (0x0FFFU >= usRawStepCount))
    {
        ulDelta = ((uint32_t)UINT16_MAX + 1U) -
                  (uint32_t)pAccumulator->usPreviousRaw +
                  (uint32_t)usRawStepCount;
    }
    else
    {
        /* 非回绕下降表示传感器计数器复位，从新值继续累计。 */
        ulDelta = usRawStepCount;
    }
    pAccumulator->usPreviousRaw = usRawStepCount;
    if ((uint32_t)usMaximumDelta < ulDelta)
    {
        return false;
    }
    if ((UINT32_MAX - pAccumulator->ulTotalSteps) < ulDelta)
    {
        pAccumulator->ulTotalSteps = UINT32_MAX;
    }
    else
    {
        pAccumulator->ulTotalSteps += ulDelta;
    }

    return true;
}

#ifndef BIKE_PEDOMETER_HOST_BUILD

#include <board.h>
#include <bf0_hal.h>
#include <drivers/i2c.h>
#include <rtdevice.h>
#include <rtthread.h>

#include "st_lsm6dsl_sensor_v1.h"

#define LOG_TAG "bike.step"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#define BIKE_PEDOMETER_I2C_NAME "i2c2"
#define BIKE_PEDOMETER_SENSOR_NAME "lsm6dsl"
#define BIKE_PEDOMETER_DEVICE_NAME "step_lsm6dsl"
#define BIKE_PEDOMETER_I2C_ADDRESS (0x6AU)
#define BIKE_PEDOMETER_WHO_AM_I_REGISTER (0x0FU)
#define BIKE_PEDOMETER_WHO_AM_I_VALUE (0x6AU)
#define BIKE_PEDOMETER_CTRL1_XL_REGISTER (0x10U)
#define BIKE_PEDOMETER_CTRL10_C_REGISTER (0x19U)
#define BIKE_PEDOMETER_ODR_MASK (0xF0U)
#define BIKE_PEDOMETER_ENABLE_MASK (0x14U)
#define BIKE_PEDOMETER_STEP_REGISTER (0x4BU)
#define BIKE_PEDOMETER_SAMPLE_PERIOD_MS (1000U)
#define BIKE_PEDOMETER_MAXIMUM_DELTA (100U)
#define BIKE_PEDOMETER_ERROR_THRESHOLD (3U)
#define BIKE_PEDOMETER_THREAD_STACK_SIZE (2048U)
#define BIKE_PEDOMETER_THREAD_PRIORITY (21U)

/* l_tBikePedometerSnapshot: UI 与采样线程共享的计步快照。 */
static BIKE_PEDOMETER_SNAPSHOT l_tBikePedometerSnapshot;

/* l_tBikePedometerAccumulator: 采样线程独占的 16 位计数扩展状态。 */
static BIKE_PEDOMETER_ACCUMULATOR l_tBikePedometerAccumulator;

/* l_tBikePedometerMutex: 保护计步快照的静态互斥锁。 */
static struct rt_mutex l_tBikePedometerMutex;

/* l_tBikePedometerThread: 板载计步传感器的静态采样线程控制块。 */
static struct rt_thread l_tBikePedometerThread;

/* l_aBikePedometerThreadStack: 计步采样线程固定 2048 字节栈。 */
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikePedometerThreadStack[BIKE_PEDOMETER_THREAD_STACK_SIZE];

/* l_pBikePedometerBus: I2C2 总线句柄，初始化前为 NULL。 */
static struct rt_i2c_bus_device *l_pBikePedometerBus;

/* l_pBikePedometerDevice: SDK LSM6DSL 计步设备句柄，初始化前为 NULL。 */
static rt_device_t l_pBikePedometerDevice;

/* l_bBikePedometerInitialized: 计步模块已执行初始化的标志。 */
static bool l_bBikePedometerInitialized;

/* BikePedometer_Lock: 获取计步快照互斥锁。
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikePedometer_Lock(void)
{
    return l_bBikePedometerInitialized &&
           (RT_EOK == rt_mutex_take(&l_tBikePedometerMutex,
                                    RT_WAITING_FOREVER));
}

/* BikePedometer_Unlock: 释放计步快照互斥锁。
 * 返回值：无
 */
static void BikePedometer_Unlock(void)
{
    (void)rt_mutex_release(&l_tBikePedometerMutex);

    return;
}

/* BikePedometer_SetStatus: 在线程安全快照中更新传感器状态。
 * 参数：
 *   - eStatus: 新状态
 * 返回值：无
 */
static void BikePedometer_SetStatus(BIKE_PEDOMETER_STATUS eStatus)
{
    if (BikePedometer_Lock())
    {
        l_tBikePedometerSnapshot.eStatus = eStatus;
        BikePedometer_Unlock();
    }

    return;
}

/* BikePedometer_ConfigurePins: 将 QSPI 屏幕未使用的 PA39/PA40 配置为
 * 黄山派板载传感器 I2C2 SDA/SCL。
 * 返回值：当前屏幕接口允许该复用时返回 true，否则返回 false
 */
static bool BikePedometer_ConfigurePins(void)
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

/* BikePedometer_ReadRegisters: 通过带返回值检查的 I2C 事务读取寄存器。
 * 参数：
 *   - ucRegister: 起始寄存器
 *   - pData: 输出数据
 *   - usLength: 读取长度
 * 返回值：完整读取返回 true，否则返回 false
 */
static bool BikePedometer_ReadRegisters(uint8_t ucRegister, uint8_t *pData,
                                        uint16_t usLength)
{
    struct rt_i2c_msg aMessages[2];

    if ((NULL == l_pBikePedometerBus) || (NULL == pData) || (0U == usLength))
    {
        return false;
    }
    (void)memset(aMessages, 0, sizeof(aMessages));
    aMessages[0].addr = BIKE_PEDOMETER_I2C_ADDRESS;
    aMessages[0].flags = RT_I2C_WR;
    aMessages[0].buf = &ucRegister;
    aMessages[0].len = 1U;
    aMessages[1].addr = BIKE_PEDOMETER_I2C_ADDRESS;
    aMessages[1].flags = RT_I2C_RD;
    aMessages[1].buf = pData;
    aMessages[1].len = usLength;

    return (2 == rt_i2c_transfer(l_pBikePedometerBus, aMessages, 2U));
}

/* BikePedometer_Probe: 在调用 SDK 驱动注册前校验 WHO_AM_I，避免缺失
 * 传感器时触发驱动失败路径留下无效设备对象。
 * 返回值：检测到兼容 LSM6DS3TR-C/LSM6DSL 返回 true，否则返回 false
 */
static bool BikePedometer_Probe(void)
{
    uint8_t ucDeviceId;

    ucDeviceId = 0U;
    if (!BikePedometer_ReadRegisters(BIKE_PEDOMETER_WHO_AM_I_REGISTER,
                                     &ucDeviceId, 1U))
    {
        LOG_E("WHO_AM_I read failed on %s", BIKE_PEDOMETER_I2C_NAME);
        return false;
    }
    if (BIKE_PEDOMETER_WHO_AM_I_VALUE != ucDeviceId)
    {
        LOG_E("unexpected WHO_AM_I: 0x%02x", ucDeviceId);
        return false;
    }

    return true;
}

/* BikePedometer_ReadRawStep: 读取带 I2C 传输校验的 16 位步数寄存器。
 * 参数：
 *   - pRawStepCount: 输出原始计数
 * 返回值：读取成功返回 true，否则返回 false
 */
static bool BikePedometer_ReadRawStep(uint16_t *pRawStepCount)
{
    uint8_t aStepBytes[2];

    if (NULL == pRawStepCount)
    {
        return false;
    }
    if (!BikePedometer_ReadRegisters(BIKE_PEDOMETER_STEP_REGISTER,
                                     aStepBytes, sizeof(aStepBytes)))
    {
        return false;
    }
    *pRawStepCount = (uint16_t)aStepBytes[0] |
                     ((uint16_t)aStepBytes[1] << 8U);

    return true;
}

/* BikePedometer_VerifyEnabled: 回读加速度计 ODR、功能引擎和计步使能位，
 * 防止 SDK 底层写失败但上层仍返回成功。
 * 返回值：计步配置已生效返回 true，否则返回 false
 */
static bool BikePedometer_VerifyEnabled(void)
{
    uint8_t ucControl1;
    uint8_t ucControl10;

    ucControl1 = 0U;
    ucControl10 = 0U;
    if ((!BikePedometer_ReadRegisters(BIKE_PEDOMETER_CTRL1_XL_REGISTER,
                                      &ucControl1, 1U)) ||
        (!BikePedometer_ReadRegisters(BIKE_PEDOMETER_CTRL10_C_REGISTER,
                                      &ucControl10, 1U)))
    {
        return false;
    }

    return ((0U != (ucControl1 & BIKE_PEDOMETER_ODR_MASK)) &&
            (BIKE_PEDOMETER_ENABLE_MASK ==
             (ucControl10 & BIKE_PEDOMETER_ENABLE_MASK)));
}

/* BikePedometer_ThreadEntry: 每秒读取硬件计步器并发布线程安全快照。
 * 参数：
 *   - pParameter: 未使用
 * 返回值：无
 */
static void BikePedometer_ThreadEntry(void *pParameter)
{
    uint32_t ulConsecutiveErrors;
    uint16_t usRawStepCount;
    bool bAccepted;

    (void)pParameter;
    ulConsecutiveErrors = 0U;
    while (true)
    {
        if (BikePedometer_ReadRawStep(&usRawStepCount))
        {
            bAccepted = BIKE_PEDOMETER_UpdateAccumulator(
                &l_tBikePedometerAccumulator, usRawStepCount,
                BIKE_PEDOMETER_MAXIMUM_DELTA);
            ulConsecutiveErrors = 0U;
            if (BikePedometer_Lock())
            {
                l_tBikePedometerSnapshot.eStatus = BIKE_PEDOMETER_STATUS_READY;
                l_tBikePedometerSnapshot.usRawStepCount = usRawStepCount;
                l_tBikePedometerSnapshot.ulStepCount =
                    l_tBikePedometerAccumulator.ulTotalSteps;
                if (bAccepted)
                {
                    l_tBikePedometerSnapshot.ulLastUpdateMs =
                        (uint32_t)rt_tick_get_millisecond();
                }
                else
                {
                    l_tBikePedometerSnapshot.ulReadErrorCount++;
                }
                BikePedometer_Unlock();
            }
        }
        else
        {
            ulConsecutiveErrors++;
            if (BikePedometer_Lock())
            {
                l_tBikePedometerSnapshot.ulReadErrorCount++;
                if (BIKE_PEDOMETER_ERROR_THRESHOLD <= ulConsecutiveErrors)
                {
                    l_tBikePedometerSnapshot.eStatus =
                        BIKE_PEDOMETER_STATUS_ERROR;
                }
                BikePedometer_Unlock();
            }
        }
        rt_thread_mdelay(BIKE_PEDOMETER_SAMPLE_PERIOD_MS);
    }
}

/* BIKE_PEDOMETER_Init: 初始化 PA39/PA40、I2C2、SDK LSM6DSL 计步驱动和
 * 固定栈采样线程。
 * 返回值：初始化成功返回 true，否则返回 false
 */
bool BIKE_PEDOMETER_Init(void)
{
    struct rt_sensor_config tConfig;
    rt_err_t eResult;

    if (l_bBikePedometerInitialized)
    {
        return (BIKE_PEDOMETER_STATUS_ERROR !=
                l_tBikePedometerSnapshot.eStatus);
    }
    (void)memset(&l_tBikePedometerSnapshot, 0,
                 sizeof(l_tBikePedometerSnapshot));
    BIKE_PEDOMETER_ResetAccumulator(&l_tBikePedometerAccumulator);
    eResult = rt_mutex_init(&l_tBikePedometerMutex, "bike_step",
                            RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex init failed: %d", eResult);
        return false;
    }
    l_bBikePedometerInitialized = true;
    BikePedometer_SetStatus(BIKE_PEDOMETER_STATUS_SEARCHING);

    if (!BikePedometer_ConfigurePins())
    {
        BikePedometer_SetStatus(BIKE_PEDOMETER_STATUS_ERROR);
        return false;
    }
    rt_thread_mdelay(2U);
    l_pBikePedometerBus = (struct rt_i2c_bus_device *)rt_device_find(
        BIKE_PEDOMETER_I2C_NAME);
    if ((NULL == l_pBikePedometerBus) || (!BikePedometer_Probe()))
    {
        BikePedometer_SetStatus(BIKE_PEDOMETER_STATUS_ERROR);
        return false;
    }

    (void)memset(&tConfig, 0, sizeof(tConfig));
    tConfig.intf.dev_name = BIKE_PEDOMETER_I2C_NAME;
    tConfig.intf.type = RT_SENSOR_INTF_I2C;
    tConfig.intf.user_data = (void *)(uintptr_t)LSM6DSL_ADDR_DEFAULT;
    tConfig.irq_pin.pin = RT_PIN_NONE;
    tConfig.mode = RT_SENSOR_MODE_POLLING;
    tConfig.power = RT_SENSOR_POWER_NORMAL;
    eResult = rt_hw_lsm6dsl_init(BIKE_PEDOMETER_SENSOR_NAME, &tConfig);
    if (RT_EOK != eResult)
    {
        BikePedometer_SetStatus(BIKE_PEDOMETER_STATUS_ERROR);
        LOG_E("LSM6DSL driver init failed: %d", eResult);
        return false;
    }
    l_pBikePedometerDevice = rt_device_find(BIKE_PEDOMETER_DEVICE_NAME);
    if (NULL == l_pBikePedometerDevice)
    {
        BikePedometer_SetStatus(BIKE_PEDOMETER_STATUS_ERROR);
        LOG_E("%s not registered", BIKE_PEDOMETER_DEVICE_NAME);
        return false;
    }
    eResult = rt_device_open(l_pBikePedometerDevice, RT_DEVICE_FLAG_RDONLY);
    if (RT_EOK != eResult)
    {
        l_pBikePedometerDevice = NULL;
        BikePedometer_SetStatus(BIKE_PEDOMETER_STATUS_ERROR);
        LOG_E("step device open failed: %d", eResult);
        return false;
    }
    if (!BikePedometer_VerifyEnabled())
    {
        (void)rt_device_close(l_pBikePedometerDevice);
        l_pBikePedometerDevice = NULL;
        BikePedometer_SetStatus(BIKE_PEDOMETER_STATUS_ERROR);
        LOG_E("pedometer register verification failed");
        return false;
    }

    eResult = rt_thread_init(&l_tBikePedometerThread, "bike_step",
                             BikePedometer_ThreadEntry, NULL,
                             l_aBikePedometerThreadStack,
                             sizeof(l_aBikePedometerThreadStack),
                             BIKE_PEDOMETER_THREAD_PRIORITY, 10U);
    if (RT_EOK == eResult)
    {
        eResult = rt_thread_startup(&l_tBikePedometerThread);
    }
    if (RT_EOK != eResult)
    {
        (void)rt_device_close(l_pBikePedometerDevice);
        l_pBikePedometerDevice = NULL;
        BikePedometer_SetStatus(BIKE_PEDOMETER_STATUS_ERROR);
        LOG_E("thread start failed: %d", eResult);
        return false;
    }
    LOG_I("LSM6DS3TR-C pedometer ready on %s", BIKE_PEDOMETER_I2C_NAME);

    return true;
}

/* BIKE_PEDOMETER_GetSnapshot: 获取板载计步器线程安全快照。
 * 参数：
 *   - pSnapshot: 输出快照
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_PEDOMETER_GetSnapshot(BIKE_PEDOMETER_SNAPSHOT *pSnapshot)
{
    bool bResult;

    bResult = false;
    if ((NULL != pSnapshot) && BikePedometer_Lock())
    {
        *pSnapshot = l_tBikePedometerSnapshot;
        BikePedometer_Unlock();
        bResult = true;
    }

    return bResult;
}

#endif /* BIKE_PEDOMETER_HOST_BUILD */
