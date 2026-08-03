#include "rtthread.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "agent_pet_ble_service.h"

/* Provided by app_pet.c in the PC simulator build. */
void pet_simulator_run(void);
void pet_simulator_stop(void);

void HAL_MspInit(void)
{
    BSP_IO_Init();
}

/**
  * @brief  Main program - matches SDK lvgl_v8_ttf simulator pattern.
  *          lv_ex_data_pool_init() is required after littlevgl2rtt_init
  *          or the LVGL renderer crashes (division by zero).
  */
int main(void)
{
    rt_err_t ret = RT_EOK;
    rt_uint32_t ms;

    ret = littlevgl2rtt_init("lcd");
    if (ret != RT_EOK)
    {
        return ret;
    }
    lv_ex_data_pool_init();

    /* Mock BLE status so the pet UI renders a demo snapshot.
     * Edit mock_ble.c to switch between idle/running/needs_input/completed/error. */
    AGENTPETBLE_Init();

    pet_simulator_run();

    while (1)
    {
        ms = lv_task_handler();
        rt_thread_mdelay(ms);
    }
    return RT_EOK;
}
