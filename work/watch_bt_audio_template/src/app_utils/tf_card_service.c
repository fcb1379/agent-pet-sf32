#include "tf_card_service.h"

#include <rtdevice.h>
#include <sys/stat.h>

#include "bf0_hal.h"
#include "dfs_fs.h"
#include "drv_gpio.h"

#define TF_CARD_DETECT_PIN          GET_PIN(1, 27)
#define TF_CARD_DEBOUNCE_MS         (20U)

/* l_tTfCardMutex: TF 卡初始化与挂载互斥锁，只允许线程上下文访问，防止多个 App 重复挂载 sd0。 */
static struct rt_mutex l_tTfCardMutex;
/* l_bTfCardServiceReady: TF 卡服务初始化标志，false/true；初始化失败时所有挂载请求均被拒绝。 */
static bool l_bTfCardServiceReady;

#ifdef RT_USING_SPI_MSD
extern int rt_spi_msd_init(void);
#endif /* RT_USING_SPI_MSD */

/***************************
 * TfCard_GetErrnoResult: 将 RT-Thread DFS 的 errno 统一为服务使用的负错误码。
 * 参数：无。
 * 返回值：errno 非零时返回对应负错误码，否则返回 -RT_ERROR。
 ***************************/
static rt_err_t TfCard_GetErrnoResult(void)
{
    int lError;

    lError = rt_get_errno();
    if (0 < lError)
    {
        return (rt_err_t)(-lError);
    }
    if (0 > lError)
    {
        return (rt_err_t)lError;
    }

    return -RT_ERROR;
}

/***************************
 * TF_CARD_IsInserted: 读取黄山板 PA27 卡检测开关并进行去抖。
 * 参数：无。
 * 返回值：卡槽连续两次检测为低电平时返回 true，否则返回 false。
 ***************************/
bool TF_CARD_IsInserted(void)
{
#if defined(BSP_USING_PC_SIMULATOR) || !defined(RT_USING_SPI_MSD)
    return false;
#else
    HAL_PIN_Set(PAD_PA27, GPIO_A27, PIN_PULLUP, 1);
    rt_pin_mode(TF_CARD_DETECT_PIN, PIN_MODE_INPUT_PULLUP);
    if (PIN_LOW != rt_pin_read(TF_CARD_DETECT_PIN))
    {
        return false;
    }

    rt_thread_mdelay(TF_CARD_DEBOUNCE_MS);

    return (PIN_LOW == rt_pin_read(TF_CARD_DETECT_PIN));
#endif /* BSP_USING_PC_SIMULATOR || !RT_USING_SPI_MSD */
}

/***************************
 * TF_CARD_IsMounted: 查询 /sdcard 是否已经由 DFS 文件系统接管。
 * 参数：无。
 * 返回值：已挂载返回 true，否则返回 false。
 ***************************/
bool TF_CARD_IsMounted(void)
{
#if defined(BSP_USING_PC_SIMULATOR) || !defined(RT_USING_SPI_MSD)
    return false;
#else
    struct dfs_filesystem *pFileSystem;

    pFileSystem = dfs_filesystem_lookup(TF_CARD_ROOT_PATH);
    if ((NULL == pFileSystem) || (NULL == pFileSystem->path))
    {
        return false;
    }

    return (0 == rt_strcmp(pFileSystem->path, TF_CARD_ROOT_PATH));
#endif /* BSP_USING_PC_SIMULATOR || !RT_USING_SPI_MSD */
}

/***************************
 * TF_CARD_EnsureMounted: 初始化 SPI TF 设备并将 FAT 文件系统挂载到 /sdcard。
 * 参数：无。
 * 返回值：成功返回 RT_EOK；无卡返回 -RT_ENODEV；其他失败返回对应错误码。
 ***************************/
rt_err_t TF_CARD_EnsureMounted(void)
{
#if defined(BSP_USING_PC_SIMULATOR) || !defined(RT_USING_SPI_MSD)
    return -RT_ENOSYS;
#else
    rt_device_t pDevice;
    rt_err_t tResult;
    int lMountResult;

    if (false == l_bTfCardServiceReady)
    {
        return -RT_ERROR;
    }
    tResult = rt_mutex_take(&l_tTfCardMutex, RT_WAITING_FOREVER);
    if (RT_EOK != tResult)
    {
        return tResult;
    }

    /* A mounted filesystem is stronger evidence than the mechanical detect
     * switch. Some card sockets let the switch float after SPI activity. */
    if (true == TF_CARD_IsMounted())
    {
        (void)rt_mutex_release(&l_tTfCardMutex);
        return RT_EOK;
    }

    if (false == TF_CARD_IsInserted())
    {
        (void)rt_mutex_release(&l_tTfCardMutex);
        return -RT_ERROR;
    }

    pDevice = rt_device_find(TF_CARD_DEVICE_NAME);
    if (NULL == pDevice)
    {
        tResult = (rt_err_t)rt_spi_msd_init();
        if (RT_EOK != tResult)
        {
            (void)rt_mutex_release(&l_tTfCardMutex);
            return tResult;
        }

        pDevice = rt_device_find(TF_CARD_DEVICE_NAME);
        if (NULL == pDevice)
        {
            (void)rt_mutex_release(&l_tTfCardMutex);
            return -RT_ERROR;
        }
    }

    if (0 != mkdir(TF_CARD_ROOT_PATH, 0777))
    {
        tResult = TfCard_GetErrnoResult();
        if (-EEXIST != tResult)
        {
            (void)rt_mutex_release(&l_tTfCardMutex);
            return tResult;
        }
    }

    lMountResult = dfs_mount(TF_CARD_DEVICE_NAME,
                             TF_CARD_ROOT_PATH,
                             "elm",
                             0,
                             NULL);
    if ((0 != lMountResult) && (false == TF_CARD_IsMounted()))
    {
        tResult = TfCard_GetErrnoResult();
        (void)rt_mutex_release(&l_tTfCardMutex);
        return tResult;
    }

    rt_kprintf("[TF] mounted %s on %s\n",
               TF_CARD_DEVICE_NAME,
               TF_CARD_ROOT_PATH);
    (void)rt_mutex_release(&l_tTfCardMutex);

    return RT_EOK;
#endif /* BSP_USING_PC_SIMULATOR || !RT_USING_SPI_MSD */
}

/***************************
 * TfCard_Init: 初始化共享 TF 卡挂载互斥锁。
 * 参数：无。
 * 返回值：成功返回 RT_EOK，否则返回 RT-Thread 错误码。
 ***************************/
static int TfCard_Init(void)
{
    rt_err_t tResult;

    tResult = rt_mutex_init(&l_tTfCardMutex, "tf_card", RT_IPC_FLAG_PRIO);
    if (RT_EOK == tResult)
    {
        l_bTfCardServiceReady = true;
    }

    return tResult;
}
INIT_APP_EXPORT(TfCard_Init);
