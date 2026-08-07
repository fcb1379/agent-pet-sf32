#include "agent_pet_merit.h"

#include <rtthread.h>
#include <string.h>
#include <time.h>

#ifndef BSP_USING_PC_SIMULATOR
    #include "share_prefs.h"
#endif

#define LOG_TAG "agent_pet_merit"
#include "log.h"

#define AGENTPET_MERIT_PREF_NAME              "agent_pet_daily_merit_pref_v1__"
#define AGENTPET_MERIT_PREF_DAY_KEY           "merit_day"
#define AGENTPET_MERIT_PREF_COUNT_KEY         "merit_count"
#define AGENTPET_MERIT_MIN_DAY                (20200101UL)
#define AGENTPET_MERIT_MAX_DAY                (20381231UL)

/* Module-local merit state. All fields are copied inside RT-Thread critical
 * sections; the generation makes deferred persistence race-free. */
static AGENTPET_MERIT_SNAPSHOT l_tMeritSnapshot;
/* True after the first initialization completes; prevents duplicate RTOS resources. */
static bool l_bMeritInitialized;
/* Dirty flag for deferred preferences writes, cleared only for the saved generation. */
static bool l_bMeritDirty;
#ifndef BSP_USING_PC_SIMULATOR
/* Preferences handle owned for the firmware lifetime; writes occur only on the worker. */
static share_prefs_t *l_pMeritPrefs;
#endif

/*
 * Local_IsValidDay
 * Function: validate a bounded YYYYMMDD calendar identifier.
 * Parameters:
 *   - ulDay: candidate date.
 * Return: true for a structurally valid supported date.
 */
static bool Local_IsValidDay(uint32_t ulDay)
{
    static const uint8_t l_aMonthDays[12] =
    {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    uint32_t ulYear;
    uint32_t ulMonth;
    uint32_t ulDate;
    uint32_t ulMaximumDate;
    bool bLeapYear;

    if ((AGENTPET_MERIT_MIN_DAY > ulDay) || (AGENTPET_MERIT_MAX_DAY < ulDay))
    {
        return false;
    }
    ulYear = ulDay / 10000U;
    ulMonth = (ulDay / 100U) % 100U;
    ulDate = ulDay % 100U;
    if ((1U > ulMonth) || (12U < ulMonth))
    {
        return false;
    }
    ulMaximumDate = l_aMonthDays[ulMonth - 1U];
    bLeapYear = ((0U == (ulYear % 4U)) && (0U != (ulYear % 100U))) ||
        (0U == (ulYear % 400U));
    if ((2U == ulMonth) && bLeapYear)
    {
        ulMaximumDate++;
    }

    return (1U <= ulDate) && (ulMaximumDate >= ulDate);
}

/*
 * Local_CurrentDay
 * Function: obtain canonical and legacy identifiers for the RTC's local date.
 * Parameters:
 *   - pLegacyDay: optional output for the former year*1000+yday encoding.
 * Return: YYYYMMDD, or zero while RTC time is invalid.
 */
static uint32_t Local_CurrentDay(uint32_t *pLegacyDay)
{
    time_t tNow;
    struct tm tDate;
    uint32_t ulYear;

    if (NULL != pLegacyDay)
    {
        *pLegacyDay = 0U;
    }
    tNow = time(NULL);
    if ((time_t)86400 > tNow)
    {
        return 0U;
    }
#if defined(_MSC_VER)
    if (0 != gmtime_s(&tDate, &tNow))
#else
    if (NULL == gmtime_r(&tNow, &tDate))
#endif
    {
        return 0U;
    }
    ulYear = (uint32_t)tDate.tm_year + 1900U;
    if (NULL != pLegacyDay)
    {
        *pLegacyDay = (ulYear * 1000U) + (uint32_t)tDate.tm_yday;
    }

    return (ulYear * 10000U) +
        (((uint32_t)tDate.tm_mon + 1U) * 100U) +
        (uint32_t)tDate.tm_mday;
}

/*
 * AGENTPETMERIT_Save
 * Function: persist one coherent snapshot from a safe application lifecycle point.
 * Parameters: none.
 * Return: none.
 */
void AGENTPETMERIT_Save(void)
{
#ifndef BSP_USING_PC_SIMULATOR
    AGENTPET_MERIT_SNAPSHOT tSnapshot;
    bool bDirty;
    rt_err_t tDayResult;
    rt_err_t tCountResult;

    rt_enter_critical();
    tSnapshot = l_tMeritSnapshot;
    bDirty = l_bMeritDirty;
    rt_exit_critical();
    if (!bDirty || (NULL == l_pMeritPrefs) || !Local_IsValidDay(tSnapshot.ulDay))
    {
        return;
    }

    tDayResult = share_prefs_set_int(
        l_pMeritPrefs,
        AGENTPET_MERIT_PREF_DAY_KEY,
        (int32_t)tSnapshot.ulDay);
    tCountResult = share_prefs_set_int(
        l_pMeritPrefs,
        AGENTPET_MERIT_PREF_COUNT_KEY,
        (int32_t)tSnapshot.ulCount);
    if ((RT_EOK != tDayResult) || (RT_EOK != tCountResult))
    {
        LOG_E("Daily merit save failed result=%d/%d", tDayResult, tCountResult);
        return;
    }

    rt_enter_critical();
    if (tSnapshot.ulGeneration == l_tMeritSnapshot.ulGeneration)
    {
        l_bMeritDirty = false;
    }
    rt_exit_critical();
#endif

    return;
}

/*
 * AGENTPETMERIT_Init
 * Function: restore persisted merit and start bounded deferred persistence.
 * Parameters: none.
 * Return: none.
 */
void AGENTPETMERIT_Init(void)
{
    int32_t lSavedDay;
    int32_t lSavedCount;

    if (l_bMeritInitialized)
    {
        return;
    }

    (void)memset(&l_tMeritSnapshot, 0, sizeof(l_tMeritSnapshot));
    lSavedDay = 0;
    lSavedCount = 0;
#ifndef BSP_USING_PC_SIMULATOR
    l_pMeritPrefs = share_prefs_open(
        AGENTPET_MERIT_PREF_NAME,
        SHAREPREFS_MODE_PRIVATE);
    if (NULL != l_pMeritPrefs)
    {
        lSavedDay = share_prefs_get_int(
            l_pMeritPrefs,
            AGENTPET_MERIT_PREF_DAY_KEY,
            0);
        lSavedCount = share_prefs_get_int(
            l_pMeritPrefs,
            AGENTPET_MERIT_PREF_COUNT_KEY,
            0);
    }
#endif
    l_tMeritSnapshot.ulDay = (0 < lSavedDay) ? (uint32_t)lSavedDay : 0U;
    l_tMeritSnapshot.ulCount = (0 < lSavedCount) ? (uint32_t)lSavedCount : 0U;
    if (AGENTPET_MERIT_MAX_COUNT < l_tMeritSnapshot.ulCount)
    {
        l_tMeritSnapshot.ulCount = AGENTPET_MERIT_MAX_COUNT;
    }
    l_tMeritSnapshot.ulGeneration = 1U;
    l_bMeritDirty = false;
    l_bMeritInitialized = true;
    AGENTPETMERIT_RefreshDay();

    return;
}

/*
 * AGENTPETMERIT_RefreshDay
 * Function: roll over to the RTC's current local day and migrate the legacy key.
 * Parameters: none.
 * Return: none.
 */
void AGENTPETMERIT_RefreshDay(void)
{
    uint32_t ulCurrentDay;
    uint32_t ulLegacyDay;

    ulCurrentDay = Local_CurrentDay(&ulLegacyDay);
    if (!Local_IsValidDay(ulCurrentDay))
    {
        return;
    }

    rt_enter_critical();
    if (ulCurrentDay != l_tMeritSnapshot.ulDay)
    {
        if (ulLegacyDay != l_tMeritSnapshot.ulDay)
        {
            l_tMeritSnapshot.ulCount = 0U;
        }
        l_tMeritSnapshot.ulDay = ulCurrentDay;
        l_tMeritSnapshot.ulGeneration++;
        l_bMeritDirty = true;
    }
    rt_exit_critical();
    return;
}

/*
 * AGENTPETMERIT_GetSnapshot
 * Function: return a coherent current-day snapshot.
 * Parameters:
 *   - pSnapshot: output snapshot; must not be NULL.
 * Return: true when copied, otherwise false.
 */
bool AGENTPETMERIT_GetSnapshot(AGENTPET_MERIT_SNAPSHOT *pSnapshot)
{
    if (NULL == pSnapshot)
    {
        return false;
    }
    if (!l_bMeritInitialized)
    {
        AGENTPETMERIT_Init();
    }
    AGENTPETMERIT_RefreshDay();
    rt_enter_critical();
    *pSnapshot = l_tMeritSnapshot;
    rt_exit_critical();

    return true;
}

/*
 * AGENTPETMERIT_Merge
 * Function: merge a desktop value; the higher count wins only for the same day.
 * Parameters:
 *   - ulDay: desktop local day encoded as YYYYMMDD.
 *   - ulCount: desktop daily count, range 0..INT32_MAX.
 * Return: true for valid input, otherwise false.
 */
bool AGENTPETMERIT_Merge(uint32_t ulDay, uint32_t ulCount)
{
    bool bChanged;

    if (!Local_IsValidDay(ulDay) || (AGENTPET_MERIT_MAX_COUNT < ulCount))
    {
        return false;
    }
    if (!l_bMeritInitialized)
    {
        AGENTPETMERIT_Init();
    }
    AGENTPETMERIT_RefreshDay();
    bChanged = false;
    rt_enter_critical();
    if (ulDay != l_tMeritSnapshot.ulDay)
    {
        l_tMeritSnapshot.ulDay = ulDay;
        l_tMeritSnapshot.ulCount = ulCount;
        bChanged = true;
    }
    else if (ulCount > l_tMeritSnapshot.ulCount)
    {
        l_tMeritSnapshot.ulCount = ulCount;
        bChanged = true;
    }
    if (bChanged)
    {
        l_tMeritSnapshot.ulGeneration++;
        l_bMeritDirty = true;
    }
    rt_exit_critical();
    return true;
}

/*
 * AGENTPETMERIT_Increment
 * Function: increment today's count with saturation and deferred persistence.
 * Parameters: none.
 * Return: resulting count.
 */
uint32_t AGENTPETMERIT_Increment(void)
{
    uint32_t ulCount;

    if (!l_bMeritInitialized)
    {
        AGENTPETMERIT_Init();
    }
    AGENTPETMERIT_RefreshDay();
    rt_enter_critical();
    if (AGENTPET_MERIT_MAX_COUNT > l_tMeritSnapshot.ulCount)
    {
        l_tMeritSnapshot.ulCount++;
        l_tMeritSnapshot.ulGeneration++;
        l_bMeritDirty = true;
    }
    ulCount = l_tMeritSnapshot.ulCount;
    rt_exit_critical();
    return ulCount;
}
