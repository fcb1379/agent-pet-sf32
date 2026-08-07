#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <bf0_hal.h>
#include "audio_diagnostics.h"

#define AUDIO_DIAG_MONITOR_STACK_SIZE (2048U)
#define AUDIO_DIAG_MONITOR_PERIOD_MS (500U)
#define AUDIO_DIAG_STALL_TIMEOUT_MS (4000U)
#define AUDIO_DIAG_MONITOR_PRIORITY (RT_THREAD_PRIORITY_HIGH + (RT_THREAD_PRIORITY_HIGHER * 2))
#define AUDIO_DIAG_RTC_PHASE_MASK (0x0000FFFFUL)
#define AUDIO_DIAG_WDT_CONTEXT_MAGIC (0xAD000000UL)
#define AUDIO_DIAG_WDT_CONTEXT_MAGIC_MASK (0xFF000000UL)
#define AUDIO_DIAG_WDT_IPSR_SHIFT (15U)
#define AUDIO_DIAG_WDT_BASEPRI_SHIFT (7U)
#define AUDIO_DIAG_WDT_PRIMASK_SHIFT (6U)
#define AUDIO_DIAG_WDT_FAULTMASK_SHIFT (5U)
#define AUDIO_DIAG_WDT_IRQ_NEST_MASK (0x1FUL)

/* Current persistent audio phase, range AUDIO_DIAGNOSTIC_PHASE values; shared
 * between audio tasks, the monitor task and the watchdog ISR. */
static volatile uint16_t g_usAudioDiagnosticPhase = AUDIO_DIAG_PHASE_IDLE;
/* Millisecond tick of the latest phase/progress update, range 0~UINT32_MAX;
 * used by the monitor to detect an audio operation that stopped progressing. */
static volatile uint32_t g_ulAudioDiagnosticPhaseTick;
/* Audio operation state, RT_TRUE only while a wooden-fish effect is active;
 * prevents ordinary long-running A2DP playback from being treated as stalled. */
static volatile rt_bool_t g_bAudioDiagnosticActive;
/* Stall latch, RT_TRUE after the first dump; prevents repeated large dumps and
 * deliberately stops watchdog feeding until the hardware timeout occurs. */
static volatile rt_bool_t g_bAudioDiagnosticStalled;
/* Static RT-Thread object for the watchdog monitor; no heap allocation is used. */
static struct rt_thread g_tAudioDiagnosticThread;
/* Fixed monitor stack, 2048 bytes, word-aligned for Cortex-M33 context saves. */
static rt_uint32_t g_aAudioDiagnosticStack[AUDIO_DIAG_MONITOR_STACK_SIZE / sizeof(rt_uint32_t)];

/***************************
 * AUDIODIAG_PersistPhase: Store the latest audio phase in the low 16 bits of
 * the SDK module-record RTC backup register while preserving SDK status bits.
 * Parameters:
 *   - usPhase: Persistent 16-bit audio phase value.
 * Return value: None.
 ***************************/
static void AUDIODIAG_PersistPhase(uint16_t usPhase)
{
    rt_base_t tLevel;
    uint32_t ulRawRecord;

    tLevel = rt_hw_interrupt_disable();
    ulRawRecord = HAL_Get_backup(RTC_BACKUP_MODULE_RECORD);
    ulRawRecord &= ~AUDIO_DIAG_RTC_PHASE_MASK;
    ulRawRecord |= (uint32_t)usPhase;
    HAL_Set_backup(RTC_BACKUP_MODULE_RECORD, ulRawRecord);
    rt_hw_interrupt_enable(tLevel);

    return;
}

/***************************
 * AUDIODIAG_PhaseName: Convert a persistent audio phase to printable text.
 * Parameters:
 *   - usPhase: Persistent 16-bit phase value.
 * Return value: Static phase-name string.
 ***************************/
static const char *AUDIODIAG_PhaseName(uint16_t usPhase)
{
    switch (usPhase)
    {
    case AUDIO_DIAG_PHASE_IDLE:
        return "idle";
    case AUDIO_DIAG_PHASE_EFFECT_QUEUED:
        return "effect_queued";
    case AUDIO_DIAG_PHASE_EFFECT_START:
        return "effect_start";
    case AUDIO_DIAG_PHASE_FILE_OPENED:
        return "file_opened";
    case AUDIO_DIAG_PHASE_HEADER_VALID:
        return "header_valid";
    case AUDIO_DIAG_PHASE_AUDIO_OPEN_BEGIN:
        return "audio_open_begin";
    case AUDIO_DIAG_PHASE_AUDIO_OPEN_DONE:
        return "audio_open_done";
    case AUDIO_DIAG_PHASE_WRITE_PROGRESS:
        return "write_progress";
    case AUDIO_DIAG_PHASE_DRAIN_BEGIN:
        return "drain_begin";
    case AUDIO_DIAG_PHASE_AUDIO_CLOSE_BEGIN:
        return "audio_close_begin";
    case AUDIO_DIAG_PHASE_EFFECT_DONE:
        return "effect_done";
    case AUDIO_DIAG_PHASE_EFFECT_ERROR:
        return "effect_error";
    case AUDIO_DIAG_PHASE_SPEAKER_OPEN_BEGIN:
        return "speaker_open_begin";
    case AUDIO_DIAG_PHASE_AUDPRC_START_BEGIN:
        return "audprc_start_begin";
    case AUDIO_DIAG_PHASE_AUDPRC_START_DONE:
        return "audprc_start_done";
    case AUDIO_DIAG_PHASE_CODEC_START_BEGIN:
        return "codec_start_begin";
    case AUDIO_DIAG_PHASE_CODEC_START_DONE:
        return "codec_start_done";
    case AUDIO_DIAG_PHASE_PA_START_BEGIN:
        return "pa_start_begin";
    case AUDIO_DIAG_PHASE_PA_GPIO_DONE:
        return "pa_gpio_done";
    case AUDIO_DIAG_PHASE_PA_WAIT_BEGIN:
        return "pa_wait_begin";
    case AUDIO_DIAG_PHASE_PA_WAIT_DONE:
        return "pa_wait_done";
    case AUDIO_DIAG_PHASE_DAC_UNMUTE_BEGIN:
        return "dac_unmute_begin";
    case AUDIO_DIAG_PHASE_SPEAKER_OPEN_DONE:
        return "speaker_open_done";
    case AUDIO_DIAG_PHASE_TX_IRQ_ENTER:
        return "tx_irq_enter";
    case AUDIO_DIAG_PHASE_TX_BUFFER_BEGIN:
        return "tx_buffer_begin";
    case AUDIO_DIAG_PHASE_TX_BUFFER_DONE:
        return "tx_buffer_done";
    case AUDIO_DIAG_PHASE_TX_IRQ_EXIT:
        return "tx_irq_exit";
    default:
        return "unknown";
    }
}

/***************************
 * AUDIODIAG_PersistWatchdogContext: Persist the interrupt mask state before
 * watchdog diagnostics attempt any console output.
 * Parameters: None.
 * Return value: Packed context written to RTC backup register 6.
 ***************************/
static uint32_t AUDIODIAG_PersistWatchdogContext(void)
{
    uint32_t ulContext;
    uint32_t ulInterruptNest;

    ulInterruptNest = (uint32_t)rt_interrupt_get_nest();
    if (AUDIO_DIAG_WDT_IRQ_NEST_MASK < ulInterruptNest)
    {
        ulInterruptNest = AUDIO_DIAG_WDT_IRQ_NEST_MASK;
    }
    ulContext = AUDIO_DIAG_WDT_CONTEXT_MAGIC |
                ((__get_IPSR() & 0x1FFUL) << AUDIO_DIAG_WDT_IPSR_SHIFT) |
                ((__get_BASEPRI() & 0xFFUL) << AUDIO_DIAG_WDT_BASEPRI_SHIFT) |
                ((__get_PRIMASK() & 0x01UL) << AUDIO_DIAG_WDT_PRIMASK_SHIFT) |
                ((__get_FAULTMASK() & 0x01UL) << AUDIO_DIAG_WDT_FAULTMASK_SHIFT) |
                ulInterruptNest;
    HAL_Set_backup(RTC_BAKCUP_WDT_STATUS, ulContext);

    return ulContext;
}

/***************************
 * AUDIODIAG_PrintThread: Print one RT-Thread state without allocating memory.
 * Parameters:
 *   - pName: RT-Thread object name to inspect.
 * Return value: None.
 ***************************/
static void AUDIODIAG_PrintThread(const char *pName)
{
    rt_thread_t pThread;

    if (NULL == pName)
    {
        return;
    }

    pThread = rt_thread_find((char *)pName);
    if (NULL == pThread)
    {
        rt_kprintf("audio_diag: thread %s not found\n", pName);
        return;
    }

    rt_kprintf("audio_diag: thread=%s stat=%u pri=%u sp=%p stack=%p/%lu tick=%lu err=%d entry=%p\n",
               pThread->name,
               (uint32_t)(pThread->stat & RT_THREAD_STAT_MASK),
               (uint32_t)pThread->current_priority,
               pThread->sp,
               pThread->stack_addr,
               (uint32_t)pThread->stack_size,
               (uint32_t)pThread->remaining_tick,
               (int32_t)pThread->error,
               pThread->entry);

    return;
}

/***************************
 * AUDIODIAG_DumpState: Print the watchdog and audio scheduling snapshot.
 * Parameters:
 *   - pReason: Static reason string for the dump.
 * Return value: None.
 ***************************/
static void AUDIODIAG_DumpState(const char *pReason)
{
    extern long list_thread(void);
    rt_thread_t pCurrentThread;
    uint32_t ulNow;
    uint32_t ulRawRecord;

    ulNow = rt_tick_get_millisecond();
    ulRawRecord = HAL_Get_backup(RTC_BACKUP_MODULE_RECORD);
    pCurrentThread = rt_thread_self();

    rt_kprintf("\n========== AUDIO WATCHDOG SNAPSHOT ==========\n");
    rt_kprintf("audio_diag: reason=%s phase=0x%04x(%s) age_ms=%lu active=%u stalled=%u\n",
               (NULL != pReason) ? pReason : "unknown",
               (uint32_t)g_usAudioDiagnosticPhase,
               AUDIODIAG_PhaseName(g_usAudioDiagnosticPhase),
               ulNow - g_ulAudioDiagnosticPhaseTick,
               (uint32_t)g_bAudioDiagnosticActive,
               (uint32_t)g_bAudioDiagnosticStalled);
    rt_kprintf("audio_diag: tick=%lu irq_nest=%u current=%s psp=%p msp=%p icsr=0x%08x rtc_record=0x%08x\n",
               ulNow,
               (uint32_t)rt_interrupt_get_nest(),
               (NULL != pCurrentThread) ? pCurrentThread->name : "none",
               (void *)__get_PSP(),
               (void *)__get_MSP(),
               (uint32_t)SCB->ICSR,
               ulRawRecord);
    AUDIODIAG_PrintThread("audiosvr");
    AUDIODIAG_PrintThread("locmusic");
    rt_kprintf("audio_diag: all thread states follow\n");
    (void)list_thread();
    rt_kprintf("========== END AUDIO WATCHDOG SNAPSHOT ======\n");

    return;
}

/***************************
 * AUDIODIAG_SetPhase: Update RAM heartbeat and RTC-backed progress marker.
 * Parameters:
 *   - ePhase: New application audio phase.
 * Return value: None.
 ***************************/
void AUDIODIAG_SetPhase(AUDIO_DIAGNOSTIC_PHASE ePhase)
{
    rt_base_t tLevel;
    uint16_t usPreviousPhase;
    rt_bool_t bPersist;

    bPersist = RT_FALSE;
    tLevel = rt_hw_interrupt_disable();
    usPreviousPhase = g_usAudioDiagnosticPhase;
    g_usAudioDiagnosticPhase = (uint16_t)ePhase;
    g_ulAudioDiagnosticPhaseTick = rt_tick_get_millisecond();
    if ((AUDIO_DIAG_PHASE_EFFECT_DONE == ePhase) ||
        (AUDIO_DIAG_PHASE_EFFECT_ERROR == ePhase) ||
        (AUDIO_DIAG_PHASE_IDLE == ePhase))
    {
        g_bAudioDiagnosticActive = RT_FALSE;
        g_bAudioDiagnosticStalled = RT_FALSE;
    }
    else if (AUDIO_DIAG_PHASE_EFFECT_QUEUED == ePhase)
    {
        g_bAudioDiagnosticActive = RT_TRUE;
        g_bAudioDiagnosticStalled = RT_FALSE;
    }
    else
    {
        /* Intermediate phases preserve the state of an active wooden-fish operation. */
    }
    if (usPreviousPhase != (uint16_t)ePhase)
    {
        bPersist = RT_TRUE;
    }
    rt_hw_interrupt_enable(tLevel);

    if (RT_TRUE == bPersist)
    {
        AUDIODIAG_PersistPhase((uint16_t)ePhase);
        rt_kprintf("audio_diag: phase=0x%04x %s\n",
                   (uint32_t)ePhase,
                   AUDIODIAG_PhaseName((uint16_t)ePhase));
    }

    return;
}

/***************************
 * AUDIODIAG_ServerStageHook: Receive detailed speaker-open stages from the
 * SDK audio server without coupling SDK sources to application headers.
 * Parameters:
 *   - usPhase: AUDIO_DIAGNOSTIC_PHASE value emitted by the audio server.
 * Return value: None.
 ***************************/
void AUDIODIAG_ServerStageHook(uint16_t usPhase)
{
    if ((RT_TRUE == g_bAudioDiagnosticActive) &&
        ((uint16_t)AUDIO_DIAG_PHASE_SPEAKER_OPEN_BEGIN <= usPhase) &&
        ((uint16_t)AUDIO_DIAG_PHASE_TX_IRQ_EXIT >= usPhase))
    {
        if (0U != __get_IPSR())
        {
            /* DMA callbacks only update RAM. RTC and console access are not
             * safe in an ISR; the watchdog hook persists this value first. */
            g_usAudioDiagnosticPhase = usPhase;
        }
        else
        {
            AUDIODIAG_SetPhase((AUDIO_DIAGNOSTIC_PHASE)usPhase);
        }
    }

    return;
}

/***************************
 * wdt_store_exception_information: Watchdog ISR hook that attempts a final
 * lock-free console snapshot before the second-stage hardware reset.
 * Parameters: None.
 * Return value: None.
 ***************************/
void wdt_store_exception_information(void)
{
    /* The NMI can interrupt the polling UART while its lock is held. Persist
     * the phase and CPU mask state without printing so the second-stage reset
     * is not trapped by a recursive console lock. */
    AUDIODIAG_PersistPhase(g_usAudioDiagnosticPhase);
    (void)AUDIODIAG_PersistWatchdogContext();

    return;
}

/***************************
 * AUDIODIAG_MonitorEntry: Feed the hardware watchdog only while the audio
 * progress heartbeat remains healthy.
 * Parameters:
 *   - pParameter: Unused RT-Thread entry parameter.
 * Return value: None.
 ***************************/
static void AUDIODIAG_MonitorEntry(void *pParameter)
{
    uint32_t ulNow;

    (void)pParameter;
    while (1)
    {
        ulNow = rt_tick_get_millisecond();
        if ((RT_TRUE == g_bAudioDiagnosticActive) &&
            (AUDIO_DIAG_STALL_TIMEOUT_MS < (ulNow - g_ulAudioDiagnosticPhaseTick)))
        {
            if (RT_FALSE == g_bAudioDiagnosticStalled)
            {
                g_bAudioDiagnosticStalled = RT_TRUE;
                AUDIODIAG_DumpState("audio progress timeout");
                rt_kprintf("audio_diag: watchdog feeding stopped\n");
            }
        }
        else
        {
#ifdef RT_USING_WDT
            rt_hw_watchdog_pet();
#endif /* RT_USING_WDT */
        }
        rt_thread_mdelay(AUDIO_DIAG_MONITOR_PERIOD_MS);
    }
}

/***************************
 * AUDIODIAG_Init: Restore the previous RTC marker and start the watchdog owner.
 * Parameters: None.
 * Return value: RT_EOK on success, otherwise an RT-Thread error code.
 ***************************/
static int AUDIODIAG_Init(void)
{
    rt_device_t pWatchdog;
    uint32_t ulPreviousContext;
    uint32_t ulPreviousRecord;
    uint32_t ulWatchdogTimeout;
    rt_err_t tResult;

    ulPreviousRecord = HAL_Get_backup(RTC_BACKUP_MODULE_RECORD);
    rt_kprintf("audio_diag: previous rtc_record=0x%08x phase=0x%04x(%s)\n",
               ulPreviousRecord,
               ulPreviousRecord & AUDIO_DIAG_RTC_PHASE_MASK,
               AUDIODIAG_PhaseName((uint16_t)(ulPreviousRecord & AUDIO_DIAG_RTC_PHASE_MASK)));
    ulPreviousContext = HAL_Get_backup(RTC_BAKCUP_WDT_STATUS);
    if (AUDIO_DIAG_WDT_CONTEXT_MAGIC ==
        (ulPreviousContext & AUDIO_DIAG_WDT_CONTEXT_MAGIC_MASK))
    {
        rt_kprintf("audio_diag: previous watchdog context=0x%08x ipsr=%lu basepri=%lu primask=%lu faultmask=%lu irq_nest=%lu\n",
                   ulPreviousContext,
                   (ulPreviousContext >> AUDIO_DIAG_WDT_IPSR_SHIFT) & 0x1FFUL,
                   (ulPreviousContext >> AUDIO_DIAG_WDT_BASEPRI_SHIFT) & 0xFFUL,
                   (ulPreviousContext >> AUDIO_DIAG_WDT_PRIMASK_SHIFT) & 0x01UL,
                   (ulPreviousContext >> AUDIO_DIAG_WDT_FAULTMASK_SHIFT) & 0x01UL,
                   ulPreviousContext & AUDIO_DIAG_WDT_IRQ_NEST_MASK);
        HAL_Set_backup(RTC_BAKCUP_WDT_STATUS, 0U);
    }
    ulPreviousRecord &= ~AUDIO_DIAG_RTC_PHASE_MASK;
    HAL_Set_backup(RTC_BACKUP_MODULE_RECORD, ulPreviousRecord);
    AUDIODIAG_SetPhase(AUDIO_DIAG_PHASE_IDLE);

    tResult = rt_thread_init(&g_tAudioDiagnosticThread,
                             "audiodiag",
                             AUDIODIAG_MonitorEntry,
                             NULL,
                             g_aAudioDiagnosticStack,
                             sizeof(g_aAudioDiagnosticStack),
                             AUDIO_DIAG_MONITOR_PRIORITY,
                             RT_THREAD_TICK_DEFAULT);
    if (RT_EOK != tResult)
    {
        rt_kprintf("audio_diag: monitor init failed=%d\n", tResult);
        return tResult;
    }

    tResult = rt_thread_startup(&g_tAudioDiagnosticThread);
    if (RT_EOK != tResult)
    {
        rt_kprintf("audio_diag: monitor startup failed=%d\n", tResult);
        return tResult;
    }

#ifdef RT_USING_WDT
    rt_hw_watchdog_set_status(1U);
    pWatchdog = rt_device_find("wdt");
    ulWatchdogTimeout = WDT_TIMEOUT;
    if (NULL != pWatchdog)
    {
        tResult = rt_device_control(pWatchdog,
                                    RT_DEVICE_CTRL_WDT_SET_TIMEOUT,
                                    &ulWatchdogTimeout);
        if (RT_EOK != tResult)
        {
            rt_kprintf("audio_diag: watchdog timeout setup failed=%d\n", tResult);
        }
    }
    rt_hw_watchdog_pet();
    rt_hw_watchdog_hook(0);
    rt_kprintf("audio_diag: watchdog monitor enabled timeout=%u seconds\n", WDT_TIMEOUT);
#endif /* RT_USING_WDT */

    return RT_EOK;
}
INIT_APP_EXPORT(AUDIODIAG_Init);
