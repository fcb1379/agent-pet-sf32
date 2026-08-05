#include "pet_imu.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "bf0_hal.h"
#include "drv_gpio.h"

#define DBG_ENABLE
#define DBG_LEVEL DBG_INFO
#define DBG_SECTION_NAME "agent.pet.imu"
#include <rtdbg.h>

#ifndef LOCAL
    #define LOCAL static
#endif

#ifndef INPUT
    #define INPUT
#endif

#define PET_IMU_I2C_BUS_NAME             "i2c3"
#define PET_IMU_SENSOR_POWER_DELAY_MS    (50U)
#define PET_IMU_I2C_ADDRESS_PRIMARY      (0x6AU)
#define PET_IMU_I2C_ADDRESS_SECONDARY    (0x6BU)
#define PET_IMU_WHO_AM_I_LSM6DSL         (0x6AU)
#define PET_IMU_WHO_AM_I_LSM6DS3TR_C     (0x69U)

#define PET_IMU_REG_WHO_AM_I             (0x0FU)
#define PET_IMU_REG_CTRL1_XL             (0x10U)
#define PET_IMU_REG_CTRL2_G              (0x11U)
#define PET_IMU_REG_CTRL3_C              (0x12U)
#define PET_IMU_REG_STATUS               (0x1EU)
#define PET_IMU_REG_OUT_TEMP_L           (0x20U)

#define PET_IMU_CTRL_ODR_52_HZ           (0x30U)
#define PET_IMU_CTRL_ACCEL_2G            (0x00U)
#define PET_IMU_CTRL_GYRO_250_DPS        (0x00U)
#define PET_IMU_CTRL3_SW_RESET           (0x01U)
#define PET_IMU_CTRL3_IF_INC             (0x04U)
#define PET_IMU_CTRL3_BDU                (0x40U)
#define PET_IMU_STATUS_ACCEL_READY       (0x01U)
#define PET_IMU_STATUS_GYRO_READY        (0x02U)
#define PET_IMU_STATUS_DATA_READY        (PET_IMU_STATUS_ACCEL_READY | PET_IMU_STATUS_GYRO_READY)

#define PET_IMU_SAMPLE_BYTES             (14U)
#define PET_IMU_RESET_TIMEOUT_MS         (100U)
#define PET_IMU_READY_TIMEOUT_MS         (100U)
#define PET_IMU_ACCEL_SCALE_NUMERATOR    (61L)
#define PET_IMU_ACCEL_SCALE_DENOMINATOR  (1000L)
#define PET_IMU_GYRO_SCALE_NUMERATOR     (875L)
#define PET_IMU_GYRO_SCALE_DENOMINATOR   (100L)
#define PET_IMU_TEMPERATURE_OFFSET       (2500L)
#define PET_IMU_TEMPERATURE_NUMERATOR    (100L)
#define PET_IMU_TEMPERATURE_DENOMINATOR  (256L)

/* l_pI2cBus: 六轴传感器使用的RT-Thread I2C3总线句柄，仅在初始化成功后有效，
 * 用于所有寄存器访问；取值为NULL或有效设备地址，不允许在中断中访问。 */
LOCAL struct rt_i2c_bus_device *l_pI2cBus;

/* l_ucDeviceAddress: 已探测到的六轴传感器7位I2C地址，仅可为0x6A或0x6B，
 * 初始化期间确定，后续读写均使用该地址。 */
LOCAL uint8_t l_ucDeviceAddress;

/* l_bInitialized: 六轴驱动初始化状态，false表示不可读取，true表示总线和芯片均已验证；
 * 由初始化流程写入，供公开接口进行状态检查。 */
LOCAL bool l_bInitialized;

/***************************
 * Local_BoardIoInit: 初始化黄山派传感器总线引脚并开启两级传感器电源
 * 参数：无
 * 返回值：无
 ***************************/
LOCAL void Local_BoardIoInit(void)
{
    rt_base_t tSystemPowerPin;
    rt_base_t tSensorPowerPin;

    HAL_PIN_Set(PAD_PA40, I2C3_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA39, I2C3_SDA, PIN_PULLUP, 1);

    tSystemPowerPin = GET_PIN(1, 38);
    rt_pin_mode(tSystemPowerPin, PIN_MODE_OUTPUT);
    rt_pin_write(tSystemPowerPin, PIN_HIGH);

    tSensorPowerPin = GET_PIN(1, 30);
    rt_pin_mode(tSensorPowerPin, PIN_MODE_OUTPUT);
    rt_pin_write(tSensorPowerPin, PIN_HIGH);

    rt_thread_mdelay(PET_IMU_SENSOR_POWER_DELAY_MS);

    return;
}

/***************************
 * Local_WriteRegister: 写入六轴传感器单个寄存器
 * 参数：
 *   - ucRegister: 寄存器地址
 *   - ucValue: 待写入的寄存器值
 * 返回值：成功返回RT_EOK，失败返回负错误码
 ***************************/
LOCAL int32_t Local_WriteRegister(uint8_t ucRegister, uint8_t ucValue)
{
    rt_size_t ulWritten;

    if (RT_NULL == l_pI2cBus)
    {
        return -RT_EIO;
    }

    ulWritten = rt_i2c_mem_write(l_pI2cBus, l_ucDeviceAddress, ucRegister, 8U,
                                 &ucValue, 1U);
    if (1U != ulWritten)
    {
        return -RT_EIO;
    }

    return RT_EOK;
}

/***************************
 * Local_ReadRegisters: 连续读取六轴传感器寄存器
 * 参数：
 *   - ucRegister: 起始寄存器地址
 *   - pData: 输出数据缓冲区
 *   - usLength: 读取字节数
 * 返回值：成功返回RT_EOK，失败返回负错误码
 ***************************/
LOCAL int32_t Local_ReadRegisters(uint8_t ucRegister, uint8_t *pData, uint16_t usLength)
{
    rt_size_t ulRead;

    if ((RT_NULL == l_pI2cBus) || (NULL == pData) || (0U == usLength))
    {
        return -RT_EINVAL;
    }

    ulRead = rt_i2c_mem_read(l_pI2cBus, l_ucDeviceAddress, ucRegister, 8U,
                             pData, usLength);
    if ((rt_size_t)usLength != ulRead)
    {
        return -RT_EIO;
    }

    return RT_EOK;
}

/***************************
 * Local_DecodeInt16: 将小端字节转换为有符号16位数
 * 参数：
 *   - pData: 至少包含两个字节的输入数据
 * 返回值：转换后的有符号16位数
 ***************************/
LOCAL int16_t Local_DecodeInt16(INPUT const uint8_t *pData)
{
    uint16_t usValue;

    usValue = (uint16_t)pData[0] | ((uint16_t)pData[1] << 8U);

    return (int16_t)usValue;
}

/***************************
 * Local_ProbeDevice: 探测板载六轴的I2C地址与芯片ID
 * 参数：无
 * 返回值：成功返回RT_EOK，失败返回负错误码
 ***************************/
LOCAL int32_t Local_ProbeDevice(void)
{
    static const uint8_t aAddressList[] =
    {
        PET_IMU_I2C_ADDRESS_PRIMARY,
        PET_IMU_I2C_ADDRESS_SECONDARY
    };
    uint8_t ucIndex;
    uint8_t ucWhoAmI;
    rt_size_t ulRead;

    for (ucIndex = 0U; ucIndex < (uint8_t)(sizeof(aAddressList) / sizeof(aAddressList[0])); ucIndex++)
    {
        l_ucDeviceAddress = aAddressList[ucIndex];
        ulRead = rt_i2c_mem_read(l_pI2cBus, l_ucDeviceAddress, PET_IMU_REG_WHO_AM_I,
                                 8U, &ucWhoAmI, 1U);
        if ((1U == ulRead) &&
                ((PET_IMU_WHO_AM_I_LSM6DSL == ucWhoAmI) ||
                 (PET_IMU_WHO_AM_I_LSM6DS3TR_C == ucWhoAmI)))
        {
            LOG_I("detected LSM6 device: addr=0x%02x id=0x%02x",
                  l_ucDeviceAddress, ucWhoAmI);
            return RT_EOK;
        }
    }

    return -RT_EIO;
}

/***************************
 * Local_ResetDevice: 软件复位六轴并等待复位完成
 * 参数：无
 * 返回值：成功返回RT_EOK，失败或超时返回负错误码
 ***************************/
LOCAL int32_t Local_ResetDevice(void)
{
    uint8_t ucControl;
    uint16_t usElapsed;
    int32_t lRetVal;

    lRetVal = Local_WriteRegister(PET_IMU_REG_CTRL3_C, PET_IMU_CTRL3_SW_RESET);
    if (RT_EOK != lRetVal)
    {
        return lRetVal;
    }

    for (usElapsed = 0U; usElapsed < PET_IMU_RESET_TIMEOUT_MS; usElapsed++)
    {
        rt_thread_mdelay(1U);
        lRetVal = Local_ReadRegisters(PET_IMU_REG_CTRL3_C, &ucControl, 1U);
        if (RT_EOK != lRetVal)
        {
            return lRetVal;
        }
        if (0U == (ucControl & PET_IMU_CTRL3_SW_RESET))
        {
            return RT_EOK;
        }
    }

    return -RT_ETIMEOUT;
}

/***************************
 * PETIMU_SetEnabled: 开启或关闭加速度计和陀螺仪采样
 * 参数：
 *   - bEnabled: true开启52Hz采样，false进入掉电模式
 * 返回值：成功返回RT_EOK，失败返回负错误码
 ***************************/
int32_t PETIMU_SetEnabled(bool bEnabled)
{
    uint8_t ucAccelControl;
    uint8_t ucGyroControl;
    int32_t lRetVal;

    if (false == l_bInitialized)
    {
        return -RT_EBUSY;
    }

    if (true == bEnabled)
    {
        ucAccelControl = PET_IMU_CTRL_ODR_52_HZ | PET_IMU_CTRL_ACCEL_2G;
        ucGyroControl = PET_IMU_CTRL_ODR_52_HZ | PET_IMU_CTRL_GYRO_250_DPS;
    }
    else
    {
        ucAccelControl = PET_IMU_CTRL_ACCEL_2G;
        ucGyroControl = PET_IMU_CTRL_GYRO_250_DPS;
    }

    lRetVal = Local_WriteRegister(PET_IMU_REG_CTRL1_XL, ucAccelControl);
    if (RT_EOK == lRetVal)
    {
        lRetVal = Local_WriteRegister(PET_IMU_REG_CTRL2_G, ucGyroControl);
    }
    if (RT_EOK != lRetVal)
    {
        /* 任一写操作失败时尽力让两个通道都回到掉电状态，避免半配置状态。 */
        (void)Local_WriteRegister(PET_IMU_REG_CTRL1_XL, PET_IMU_CTRL_ACCEL_2G);
        (void)Local_WriteRegister(PET_IMU_REG_CTRL2_G, PET_IMU_CTRL_GYRO_250_DPS);
    }

    return lRetVal;
}

/***************************
 * PETIMU_Init: 初始化黄山派板载LSM6系列六轴传感器
 * 参数：无
 * 返回值：成功返回RT_EOK，失败返回负错误码
 ***************************/
int32_t PETIMU_Init(void)
{
    uint8_t ucControl;
    int32_t lRetVal;

    if (true == l_bInitialized)
    {
        return RT_EOK;
    }

    Local_BoardIoInit();

    l_pI2cBus = (struct rt_i2c_bus_device *)rt_device_find(PET_IMU_I2C_BUS_NAME);
    if (RT_NULL == l_pI2cBus)
    {
        LOG_E("I2C bus %s not found", PET_IMU_I2C_BUS_NAME);
        return -RT_EIO;
    }

    lRetVal = Local_ProbeDevice();
    if (RT_EOK != lRetVal)
    {
        LOG_E("LSM6 device not found");
        return lRetVal;
    }

    lRetVal = Local_ResetDevice();
    if (RT_EOK != lRetVal)
    {
        LOG_E("device reset failed: %d", lRetVal);
        return lRetVal;
    }

    ucControl = PET_IMU_CTRL3_BDU | PET_IMU_CTRL3_IF_INC;
    lRetVal = Local_WriteRegister(PET_IMU_REG_CTRL3_C, ucControl);
    if (RT_EOK != lRetVal)
    {
        return lRetVal;
    }

    l_bInitialized = true;
    lRetVal = PETIMU_SetEnabled(true);
    if (RT_EOK != lRetVal)
    {
        l_bInitialized = false;
        return lRetVal;
    }

    LOG_I("six-axis sensor initialized at 52 Hz");

    return RT_EOK;
}

/***************************
 * PETIMU_Read: 读取一次六轴和温度数据
 * 参数：
 *   - pSample: 采样结果输出指针
 * 返回值：成功返回RT_EOK，数据未就绪返回-RT_EBUSY，其他失败返回负错误码
 ***************************/
int32_t PETIMU_Read(PET_IMU_SAMPLE *pSample)
{
    uint8_t ucStatus;
    uint8_t aRawData[PET_IMU_SAMPLE_BYTES];
    uint16_t usElapsed;
    int32_t lRetVal;

    if (NULL == pSample)
    {
        return -RT_EINVAL;
    }
    if (false == l_bInitialized)
    {
        return -RT_EBUSY;
    }

    for (usElapsed = 0U; usElapsed < PET_IMU_READY_TIMEOUT_MS; usElapsed++)
    {
        lRetVal = Local_ReadRegisters(PET_IMU_REG_STATUS, &ucStatus, 1U);
        if (RT_EOK != lRetVal)
        {
            return lRetVal;
        }
        if (PET_IMU_STATUS_DATA_READY == (ucStatus & PET_IMU_STATUS_DATA_READY))
        {
            break;
        }
        rt_thread_mdelay(1U);
    }
    if (PET_IMU_READY_TIMEOUT_MS == usElapsed)
    {
        return -RT_ETIMEOUT;
    }

    lRetVal = Local_ReadRegisters(PET_IMU_REG_OUT_TEMP_L, aRawData, PET_IMU_SAMPLE_BYTES);
    if (RT_EOK != lRetVal)
    {
        return lRetVal;
    }

    pSample->sTemperature = Local_DecodeInt16(&aRawData[0]);
    pSample->sGyroX = Local_DecodeInt16(&aRawData[2]);
    pSample->sGyroY = Local_DecodeInt16(&aRawData[4]);
    pSample->sGyroZ = Local_DecodeInt16(&aRawData[6]);
    pSample->sAccelX = Local_DecodeInt16(&aRawData[8]);
    pSample->sAccelY = Local_DecodeInt16(&aRawData[10]);
    pSample->sAccelZ = Local_DecodeInt16(&aRawData[12]);

    pSample->lAccelXMg = ((int32_t)pSample->sAccelX * PET_IMU_ACCEL_SCALE_NUMERATOR) /
                         PET_IMU_ACCEL_SCALE_DENOMINATOR;
    pSample->lAccelYMg = ((int32_t)pSample->sAccelY * PET_IMU_ACCEL_SCALE_NUMERATOR) /
                         PET_IMU_ACCEL_SCALE_DENOMINATOR;
    pSample->lAccelZMg = ((int32_t)pSample->sAccelZ * PET_IMU_ACCEL_SCALE_NUMERATOR) /
                         PET_IMU_ACCEL_SCALE_DENOMINATOR;
    pSample->lGyroXMdps = ((int32_t)pSample->sGyroX * PET_IMU_GYRO_SCALE_NUMERATOR) /
                          PET_IMU_GYRO_SCALE_DENOMINATOR;
    pSample->lGyroYMdps = ((int32_t)pSample->sGyroY * PET_IMU_GYRO_SCALE_NUMERATOR) /
                          PET_IMU_GYRO_SCALE_DENOMINATOR;
    pSample->lGyroZMdps = ((int32_t)pSample->sGyroZ * PET_IMU_GYRO_SCALE_NUMERATOR) /
                          PET_IMU_GYRO_SCALE_DENOMINATOR;
    pSample->lTemperatureCentiC = PET_IMU_TEMPERATURE_OFFSET +
                                  (((int32_t)pSample->sTemperature * PET_IMU_TEMPERATURE_NUMERATOR) /
                                   PET_IMU_TEMPERATURE_DENOMINATOR);

    return RT_EOK;
}

/***************************
 * PETIMU_IsReady: 查询六轴驱动是否初始化成功
 * 参数：无
 * 返回值：初始化成功返回true，否则返回false
 ***************************/
bool PETIMU_IsReady(void)
{
    return l_bInitialized;
}

/***************************
 * PETIMU_AutoInit: RT-Thread组件初始化阶段自动初始化六轴
 * 参数：无
 * 返回值：始终返回RT_EOK，硬件缺失时记录日志但不阻塞系统启动
 ***************************/
LOCAL int PETIMU_AutoInit(void)
{
    int32_t lRetVal;

    lRetVal = PETIMU_Init();
    if (RT_EOK != lRetVal)
    {
        LOG_W("six-axis sensor unavailable: %d", lRetVal);
    }

    return RT_EOK;
}
INIT_COMPONENT_EXPORT(PETIMU_AutoInit);

/***************************
 * PETIMU_Command: FinSH命令读取一次六轴采样
 * 参数：
 *   - lArgumentCount: 命令参数数量
 *   - pArgumentValues: 命令参数数组
 * 返回值：成功返回RT_EOK，失败返回负错误码
 ***************************/
LOCAL int PETIMU_Command(int lArgumentCount, char **pArgumentValues)
{
    PET_IMU_SAMPLE tSample;
    int32_t lRetVal;

    (void)lArgumentCount;
    (void)pArgumentValues;

    lRetVal = PETIMU_Read(&tSample);
    if (RT_EOK == lRetVal)
    {
        rt_kprintf("accel(mg): x=%ld y=%ld z=%ld\n",
                   (long)tSample.lAccelXMg, (long)tSample.lAccelYMg, (long)tSample.lAccelZMg);
        rt_kprintf("gyro(mdps): x=%ld y=%ld z=%ld\n",
                   (long)tSample.lGyroXMdps, (long)tSample.lGyroYMdps, (long)tSample.lGyroZMdps);
        rt_kprintf("temperature(0.01C): %ld\n", (long)tSample.lTemperatureCentiC);
    }
    else
    {
        rt_kprintf("imu read failed: %ld\n", (long)lRetVal);
    }

    return (int)lRetVal;
}
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
MSH_CMD_EXPORT_ALIAS(PETIMU_Command, imu_read, read Huangshan board six-axis sensor);
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
