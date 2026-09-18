#include "bike_power.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define BIKE_POWER_VALID_MINIMUM_DECI_MV (25000U)
#define BIKE_POWER_VALID_MAXIMUM_DECI_MV (48000U)
#define BIKE_POWER_EMPTY_DECI_MV (33000U)
#define BIKE_POWER_FULL_DECI_MV (41000U)
#define BIKE_POWER_PERCENT_MAXIMUM (100U)
#define BIKE_POWER_PERCENT_HYSTERESIS (2U)
#define BIKE_POWER_FILTER_DIVISOR (4U)

/* BIKE_POWER_ResetFilter: 清空电池电压低通和显示回差状态。
 * 参数：
 *   - pFilter: 待清空的滤波对象
 * 返回值：无
 */
void BIKE_POWER_ResetFilter(BIKE_POWER_FILTER *pFilter)
{
    if (NULL != pFilter)
    {
        (void)memset(pFilter, 0, sizeof(*pFilter));
    }

    return;
}

/* BIKE_POWER_VoltageToPercent: 按 X-TRACK 的 3.3~4.1 V 线性模型
 * 将电池电压换算为显示百分比。该值是电压估算，不是库仑计 SOC。
 * 参数：
 *   - ulVoltageDeciMv: 电池电压，单位 0.1 mV
 * 返回值：0~100 的估算百分比
 */
uint8_t BIKE_POWER_VoltageToPercent(uint32_t ulVoltageDeciMv)
{
    uint32_t ulPercent;

    if (BIKE_POWER_EMPTY_DECI_MV >= ulVoltageDeciMv)
    {
        return 0U;
    }
    if (BIKE_POWER_FULL_DECI_MV <= ulVoltageDeciMv)
    {
        return BIKE_POWER_PERCENT_MAXIMUM;
    }
    ulPercent = ((ulVoltageDeciMv - BIKE_POWER_EMPTY_DECI_MV) *
                 BIKE_POWER_PERCENT_MAXIMUM) /
                (BIKE_POWER_FULL_DECI_MV - BIKE_POWER_EMPTY_DECI_MV);

    return (uint8_t)ulPercent;
}

/* BIKE_POWER_UpdateFilter: 对有效电压执行四分之一权重一阶低通，并用
 * 2% 回差抑制边界抖动；0% 和 100% 边界始终允许更新。
 * 参数：
 *   - pFilter: 滤波和回差状态
 *   - ulVoltageDeciMv: 新 ADC 样本，单位 0.1 mV
 *   - pFilteredVoltageDeciMv: 输出低通后的电压
 *   - pPercent: 输出稳定后的显示百分比
 * 返回值：样本和参数有效返回 true，否则返回 false
 */
bool BIKE_POWER_UpdateFilter(BIKE_POWER_FILTER *pFilter,
                             uint32_t ulVoltageDeciMv,
                             uint32_t *pFilteredVoltageDeciMv,
                             uint8_t *pPercent)
{
    uint32_t ulFilteredVoltageDeciMv;
    uint8_t ucCandidatePercent;
    uint8_t ucDifference;

    if ((NULL == pFilter) || (NULL == pFilteredVoltageDeciMv) ||
        (NULL == pPercent) ||
        (BIKE_POWER_VALID_MINIMUM_DECI_MV > ulVoltageDeciMv) ||
        (BIKE_POWER_VALID_MAXIMUM_DECI_MV < ulVoltageDeciMv))
    {
        return false;
    }
    if (!pFilter->bInitialized)
    {
        pFilter->ulFilteredVoltageDeciMv = ulVoltageDeciMv;
        pFilter->ucDisplayedPercent =
            BIKE_POWER_VoltageToPercent(ulVoltageDeciMv);
        pFilter->bInitialized = true;
    }
    else
    {
        ulFilteredVoltageDeciMv =
            ((pFilter->ulFilteredVoltageDeciMv *
              (BIKE_POWER_FILTER_DIVISOR - 1U)) +
             ulVoltageDeciMv + (BIKE_POWER_FILTER_DIVISOR / 2U)) /
            BIKE_POWER_FILTER_DIVISOR;
        pFilter->ulFilteredVoltageDeciMv = ulFilteredVoltageDeciMv;
        ucCandidatePercent = BIKE_POWER_VoltageToPercent(
            ulFilteredVoltageDeciMv);
        ucDifference = (ucCandidatePercent >=
                        pFilter->ucDisplayedPercent) ?
                       (uint8_t)(ucCandidatePercent -
                                 pFilter->ucDisplayedPercent) :
                       (uint8_t)(pFilter->ucDisplayedPercent -
                                 ucCandidatePercent);
        if ((BIKE_POWER_PERCENT_HYSTERESIS <= ucDifference) ||
            (0U == ucCandidatePercent) ||
            (BIKE_POWER_PERCENT_MAXIMUM == ucCandidatePercent))
        {
            pFilter->ucDisplayedPercent = ucCandidatePercent;
        }
    }
    *pFilteredVoltageDeciMv = pFilter->ulFilteredVoltageDeciMv;
    *pPercent = pFilter->ucDisplayedPercent;

    return true;
}

#ifndef BIKE_POWER_HOST_BUILD

#include <drivers/adc.h>
#include <rtdevice.h>
#include <rtthread.h>

#ifdef BSP_USING_CHARGER
#include "charge.h"
#define BIKE_POWER_HAS_CHARGER (1)
#else
#define BIKE_POWER_HAS_CHARGER (0)
#endif

#include "bike_sound.h"

#define DBG_TAG "bike.power"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BIKE_POWER_ADC_NAME "bat1"
#define BIKE_POWER_CHARGE_NAME "charge"
#define BIKE_POWER_ADC_CHANNEL (7U)
#define BIKE_POWER_SAMPLE_PERIOD_MS (30000U)
#define BIKE_POWER_ERROR_THRESHOLD (3U)
#define BIKE_POWER_THREAD_STACK_SIZE (1536U)
#define BIKE_POWER_THREAD_PRIORITY (22U)

/* l_tBikePowerSnapshot: UI 与采样线程共享的电池和供电状态快照。
 * 电压范围 25000~48000（0.1 mV），百分比范围 0~100；仅在互斥锁
 * 保护下读写，不可作为精确 SOC 或安全关机阈值。
 */
static BIKE_POWER_SNAPSHOT l_tBikePowerSnapshot;

/* l_tBikePowerFilter: 采样线程独占的电压低通和百分比回差状态。
 * 生命周期覆盖整个服务运行期，不由 UI 直接访问。
 */
static BIKE_POWER_FILTER l_tBikePowerFilter;

/* l_tBikePowerMutex: 保护电池和供电状态快照的静态互斥锁。
 * 初始化成功后永久有效，所有快照访问均使用无限等待的线程上下文锁。
 */
static struct rt_mutex l_tBikePowerMutex;

/* l_tBikePowerThread: 低频电池采样静态线程控制块。
 * 线程优先级 22，仅负责 ADC/charger 读取和快照发布。
 */
static struct rt_thread l_tBikePowerThread;

/* l_aBikePowerThreadStack: 电池采样线程固定 1536 字节栈。
 * 静态分配以避免运行期堆碎片，不得被其他线程复用。
 */
ALIGN(RT_ALIGN_SIZE)
static uint8_t l_aBikePowerThreadStack[BIKE_POWER_THREAD_STACK_SIZE];

/* l_pBikePowerAdc: GPADC1 的 bat1 设备句柄，初始化前为 NULL。
 * 只访问固定内部 VBAT channel 7，调用前始终检查非 NULL。
 */
static rt_device_t l_pBikePowerAdc;

/* l_pBikePowerCharge: 片上充电器设备句柄，不可用时为 NULL。
 * 仅执行状态读取，不修改充电电流、目标电压或使能状态。
 */
static rt_device_t l_pBikePowerCharge;

/* l_bBikePowerInitialized: 电池服务已执行初始化的标志。
 * false 表示互斥锁不可用，true 表示快照接口允许尝试加锁。
 */
static bool l_bBikePowerInitialized;

/* l_bBikePowerHasChargeSample: 充电状态线程已获得首个有效样本。 */
static bool l_bBikePowerHasChargeSample;

/* l_bBikePowerPreviousExternalPower: 上次有效外部供电状态，
 * 仅由电池采样线程读写，用于边沿提示音。
 */
static bool l_bBikePowerPreviousExternalPower;

/* BikePower_Lock: 获取电池状态快照互斥锁。
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikePower_Lock(void)
{
    return l_bBikePowerInitialized &&
           (RT_EOK == rt_mutex_take(&l_tBikePowerMutex,
                                    RT_WAITING_FOREVER));
}

/* BikePower_Unlock: 释放电池状态快照互斥锁。
 * 返回值：无
 */
static void BikePower_Unlock(void)
{
    (void)rt_mutex_release(&l_tBikePowerMutex);

    return;
}

/* BikePower_IncrementCounter: 饱和递增诊断计数器。
 * 参数：
 *   - pCounter: 待递增的计数器
 * 返回值：无
 */
static void BikePower_IncrementCounter(uint32_t *pCounter)
{
    if ((NULL != pCounter) && (UINT32_MAX > *pCounter))
    {
        (*pCounter)++;
    }

    return;
}

/* BikePower_SetStatus: 在线程安全快照中更新电池采样状态。
 * 参数：
 *   - eStatus: 新状态
 * 返回值：无
 */
static void BikePower_SetStatus(BIKE_POWER_STATUS eStatus)
{
    if (BikePower_Lock())
    {
        l_tBikePowerSnapshot.eStatus = eStatus;
        BikePower_Unlock();
    }

    return;
}

/* BikePower_ReadBattery: 临时使能内部 VBAT 通道 7，读取 SDK 校准后的
 * 0.1 mV 电压并立即关闭 ADC。
 * 参数：
 *   - pVoltageDeciMv: 输出电池电压，单位 0.1 mV
 * 返回值：使能、读取、范围检查和关闭全部成功返回 true
 */
static bool BikePower_ReadBattery(uint32_t *pVoltageDeciMv)
{
    uint32_t ulVoltageDeciMv;
    rt_err_t eEnableResult;
    rt_err_t eDisableResult;

    if ((NULL == l_pBikePowerAdc) || (NULL == pVoltageDeciMv))
    {
        return false;
    }
    eEnableResult = rt_adc_enable((rt_adc_device_t)l_pBikePowerAdc,
                                  BIKE_POWER_ADC_CHANNEL);
    if (RT_EOK != eEnableResult)
    {
        LOG_E("ADC enable failed: %d", eEnableResult);
        return false;
    }
    ulVoltageDeciMv = rt_adc_read((rt_adc_device_t)l_pBikePowerAdc,
                                  BIKE_POWER_ADC_CHANNEL);
    eDisableResult = rt_adc_disable((rt_adc_device_t)l_pBikePowerAdc,
                                    BIKE_POWER_ADC_CHANNEL);
    if (RT_EOK != eDisableResult)
    {
        LOG_E("ADC disable failed: %d", eDisableResult);
        return false;
    }
    if ((BIKE_POWER_VALID_MINIMUM_DECI_MV > ulVoltageDeciMv) ||
        (BIKE_POWER_VALID_MAXIMUM_DECI_MV < ulVoltageDeciMv))
    {
        LOG_E("invalid VBAT sample: %lu", (unsigned long)ulVoltageDeciMv);
        return false;
    }
    *pVoltageDeciMv = ulVoltageDeciMv;

    return true;
}

/* BikePower_ReadChargeState: 读取外部供电和充满状态。
 * 参数：
 *   - pExternalPower: 输出外部供电存在标志
 *   - pFull: 输出充满标志
 * 返回值：两个状态均读取成功返回 true，否则返回 false
 */
static bool BikePower_ReadChargeState(bool *pExternalPower, bool *pFull)
{
#if BIKE_POWER_HAS_CHARGER
    uint8_t ucExternalPower;
    uint8_t ucFull;
    rt_err_t eResult;

    if ((NULL == l_pBikePowerCharge) || (NULL == pExternalPower) ||
        (NULL == pFull))
    {
        return false;
    }
    ucExternalPower = 0U;
    ucFull = 0U;
    eResult = rt_device_control(l_pBikePowerCharge,
                                RT_CHARGE_GET_STATUS,
                                &ucExternalPower);
    if (RT_EOK != eResult)
    {
        return false;
    }
    eResult = rt_device_control(l_pBikePowerCharge,
                                RT_CHARGE_GET_FULL_STATUS,
                                &ucFull);
    if (RT_EOK != eResult)
    {
        return false;
    }
    *pExternalPower = (0U != ucExternalPower);
    *pFull = (0U != ucFull);

    return true;
#else
    (void)pExternalPower;
    (void)pFull;

    return false;
#endif
}

/* BikePower_UpdateChargeSound: 按外部供电边沿提交充电提示音。
 * 参数：
 *   - bExternalPower: 当前外部供电状态
 * 返回值：无
 */
static void BikePower_UpdateChargeSound(bool bExternalPower)
{
    BIKE_SOUND_EVENT eEvent;

    if ((!l_bBikePowerHasChargeSample) ||
        (l_bBikePowerPreviousExternalPower != bExternalPower))
    {
        l_bBikePowerHasChargeSample = true;
        if (l_bBikePowerPreviousExternalPower != bExternalPower)
        {
            eEvent = bExternalPower ? BIKE_SOUND_EVENT_CHARGE_START :
                                      BIKE_SOUND_EVENT_CHARGE_END;
            if (!BIKE_SOUND_Request(eEvent))
            {
                LOG_W("charge sound request failed event=%u", eEvent);
            }
        }
        l_bBikePowerPreviousExternalPower = bExternalPower;
    }

    return;
}

/* BikePower_PublishSample: 滤波有效电压并发布电池、外部供电快照。
 * 参数：
 *   - ulVoltageDeciMv: 当前有效 ADC 样本，单位 0.1 mV
 * 返回值：滤波和快照发布成功返回 true，否则返回 false
 */
static bool BikePower_PublishSample(uint32_t ulVoltageDeciMv)
{
    uint32_t ulFilteredVoltageDeciMv;
    uint8_t ucPercent;
    bool bExternalPower;
    bool bFull;
    bool bChargeStatusValid;

    if (!BIKE_POWER_UpdateFilter(&l_tBikePowerFilter, ulVoltageDeciMv,
                                 &ulFilteredVoltageDeciMv, &ucPercent))
    {
        return false;
    }
    bExternalPower = false;
    bFull = false;
    bChargeStatusValid = BikePower_ReadChargeState(&bExternalPower, &bFull);
    if (BikePower_Lock())
    {
        l_tBikePowerSnapshot.eStatus = BIKE_POWER_STATUS_READY;
        l_tBikePowerSnapshot.ulVoltageDeciMv =
            ulFilteredVoltageDeciMv;
        l_tBikePowerSnapshot.ucPercent = ucPercent;
        l_tBikePowerSnapshot.ulLastUpdateMs =
            (uint32_t)rt_tick_get_millisecond();
        l_tBikePowerSnapshot.bChargeStatusValid = bChargeStatusValid;
        if (bChargeStatusValid)
        {
            l_tBikePowerSnapshot.bExternalPower = bExternalPower;
            l_tBikePowerSnapshot.bFull = bFull;
        }
        else if (BIKE_POWER_HAS_CHARGER)
        {
            BikePower_IncrementCounter(
                &l_tBikePowerSnapshot.ulChargeErrorCount);
        }
        BikePower_Unlock();
    }
    if (bChargeStatusValid)
    {
        BikePower_UpdateChargeSound(bExternalPower);
    }

    return true;
}

/* BikePower_RecordAdcError: 累加 ADC 错误并在连续失败达到门限后
 * 把服务置为错误状态。
 * 参数：
 *   - ulConsecutiveErrors: 当前连续失败次数
 * 返回值：无
 */
static void BikePower_RecordAdcError(uint32_t ulConsecutiveErrors)
{
    if (BikePower_Lock())
    {
        BikePower_IncrementCounter(&l_tBikePowerSnapshot.ulAdcErrorCount);
        if (BIKE_POWER_ERROR_THRESHOLD <= ulConsecutiveErrors)
        {
            l_tBikePowerSnapshot.eStatus = BIKE_POWER_STATUS_ERROR;
        }
        BikePower_Unlock();
    }

    return;
}

/* BikePower_ThreadEntry: 启动后立即采样，之后每 30 秒短时开启 ADC。
 * 参数：
 *   - pParameter: 未使用
 * 返回值：无
 */
static void BikePower_ThreadEntry(void *pParameter)
{
    uint32_t ulVoltageDeciMv;
    uint32_t ulConsecutiveErrors;

    (void)pParameter;
    ulConsecutiveErrors = 0U;
    while (true)
    {
        if (BikePower_ReadBattery(&ulVoltageDeciMv) &&
            BikePower_PublishSample(ulVoltageDeciMv))
        {
            ulConsecutiveErrors = 0U;
        }
        else
        {
            if (UINT32_MAX > ulConsecutiveErrors)
            {
                ulConsecutiveErrors++;
            }
            BikePower_RecordAdcError(ulConsecutiveErrors);
        }
        rt_thread_mdelay(BIKE_POWER_SAMPLE_PERIOD_MS);
    }
}

/* BIKE_POWER_Init: 初始化 GPADC1 内部 VBAT 通道、片上充电状态和固定栈
 * 低频采样线程。
 * 返回值：ADC 设备和采样线程初始化成功返回 true，否则返回 false
 */
bool BIKE_POWER_Init(void)
{
    rt_err_t eResult;

    if (l_bBikePowerInitialized)
    {
        return (BIKE_POWER_STATUS_ERROR != l_tBikePowerSnapshot.eStatus);
    }
    (void)memset(&l_tBikePowerSnapshot, 0,
                 sizeof(l_tBikePowerSnapshot));
    l_bBikePowerHasChargeSample = false;
    l_bBikePowerPreviousExternalPower = false;
    BIKE_POWER_ResetFilter(&l_tBikePowerFilter);
    eResult = rt_mutex_init(&l_tBikePowerMutex, "bike_pwr",
                            RT_IPC_FLAG_FIFO);
    if (RT_EOK != eResult)
    {
        LOG_E("mutex init failed: %d", eResult);
        return false;
    }
    l_bBikePowerInitialized = true;
    BikePower_SetStatus(BIKE_POWER_STATUS_SEARCHING);

    l_pBikePowerAdc = rt_device_find(BIKE_POWER_ADC_NAME);
    if (NULL == l_pBikePowerAdc)
    {
        BikePower_SetStatus(BIKE_POWER_STATUS_ERROR);
        LOG_E("%s not found", BIKE_POWER_ADC_NAME);
        return false;
    }
#if BIKE_POWER_HAS_CHARGER
    l_pBikePowerCharge = rt_device_find(BIKE_POWER_CHARGE_NAME);
    if (NULL == l_pBikePowerCharge)
    {
        LOG_E("%s not found; voltage remains available",
              BIKE_POWER_CHARGE_NAME);
    }
#endif
    eResult = rt_thread_init(&l_tBikePowerThread, "bike_pwr",
                             BikePower_ThreadEntry, NULL,
                             l_aBikePowerThreadStack,
                             sizeof(l_aBikePowerThreadStack),
                             BIKE_POWER_THREAD_PRIORITY, 10U);
    if (RT_EOK == eResult)
    {
        eResult = rt_thread_startup(&l_tBikePowerThread);
    }
    if (RT_EOK != eResult)
    {
        BikePower_SetStatus(BIKE_POWER_STATUS_ERROR);
        LOG_E("thread start failed: %d", eResult);
        return false;
    }
    LOG_I("VBAT monitor ready on %s channel %u",
          BIKE_POWER_ADC_NAME, BIKE_POWER_ADC_CHANNEL);

    return true;
}

/* BIKE_POWER_GetSnapshot: 获取电池与外部供电线程安全快照。
 * 参数：
 *   - pSnapshot: 输出快照
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_POWER_GetSnapshot(BIKE_POWER_SNAPSHOT *pSnapshot)
{
    bool bResult;

    bResult = false;
    if ((NULL != pSnapshot) && BikePower_Lock())
    {
        *pSnapshot = l_tBikePowerSnapshot;
        BikePower_Unlock();
        bResult = true;
    }

    return bResult;
}

#endif /* BIKE_POWER_HOST_BUILD */
