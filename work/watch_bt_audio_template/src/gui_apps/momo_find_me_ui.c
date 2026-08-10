#include "momo_find_me_ui.h"

#ifdef MOMO_FIND_ME

#include <rtthread.h>

#include "ble_watch_link.h"
#include "littlevgl2rtt.h"
#include "momo_find_me.h"

#define MOMO_FIND_GUI_MASCOT_ZOOM (128U)

LV_IMG_DECLARE(agent_pet_mascot);

typedef struct _MOMO_FIND_GUI_ENV
{
    lv_obj_t *pRoot;
    uint32_t ulVisibleGeneration;
} MOMO_FIND_GUI_ENV;

/* GUI 线程独占的覆盖层对象；不得从 BLE、闹钟或按键驱动线程访问。 */
static MOMO_FIND_GUI_ENV l_tFindGui;

static void Local_StopEvent(lv_event_t *pEvent)
{
    (void)pEvent;
    (void)MOMOFIND_StopAt(MOMO_FIND_END_USER_STOP,
                          (uint32_t)rt_tick_get());

    return;
}

static bool Local_CreateOverlay(uint32_t ulGeneration)
{
    lv_obj_t *pMascot;
    lv_obj_t *pTitle;
    lv_obj_t *pHint;

    l_tFindGui.pRoot = lv_obj_create(lv_layer_top());
    if (NULL == l_tFindGui.pRoot)
    {
        return false;
    }
    lv_obj_remove_style_all(l_tFindGui.pRoot);
    lv_obj_set_pos(l_tFindGui.pRoot, 0, 0);
    lv_obj_set_size(l_tFindGui.pRoot, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(l_tFindGui.pRoot, lv_color_hex(0x08131FU), 0);
    lv_obj_set_style_bg_opa(l_tFindGui.pRoot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l_tFindGui.pRoot, 3, 0);
    lv_obj_set_style_border_color(l_tFindGui.pRoot, lv_color_hex(0x55D7FFU), 0);
    lv_obj_add_flag(l_tFindGui.pRoot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(l_tFindGui.pRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(l_tFindGui.pRoot, Local_StopEvent,
                        LV_EVENT_SHORT_CLICKED, NULL);

    pTitle = lv_label_create(l_tFindGui.pRoot);
    pMascot = lv_img_create(l_tFindGui.pRoot);
    pHint = lv_label_create(l_tFindGui.pRoot);
    if ((NULL == pTitle) || (NULL == pMascot) || (NULL == pHint))
    {
        lv_obj_del(l_tFindGui.pRoot);
        l_tFindGui.pRoot = NULL;
        return false;
    }

    lv_label_set_text(pTitle, "Momo is here!");
    lv_obj_set_style_text_color(pTitle, lv_color_hex(0xFFFFFFU), 0);
    lv_obj_align(pTitle, LV_ALIGN_TOP_MID, 0, 24);

    lv_img_set_src(pMascot, &agent_pet_mascot);
    lv_img_set_zoom(pMascot, MOMO_FIND_GUI_MASCOT_ZOOM);
    lv_obj_center(pMascot);

    lv_label_set_text(pHint, "Tap to stop");
    lv_obj_set_style_text_color(pHint, lv_color_hex(0x55D7FFU), 0);
    lv_obj_align(pHint, LV_ALIGN_BOTTOM_MID, 0, -26);
    l_tFindGui.ulVisibleGeneration = ulGeneration;

    return true;
}

void MOMOFINDGUI_Init(void)
{
    l_tFindGui.pRoot = NULL;
    l_tFindGui.ulVisibleGeneration = 0U;
#ifdef BSP_USING_PC_SIMULATOR
    MOMOFIND_Init();
#endif /* BSP_USING_PC_SIMULATOR */

    return;
}

void MOMOFINDGUI_Poll(void)
{
    MOMO_FIND_SNAPSHOT tSnapshot;
    MOMO_FIND_END_EVENT tEvent;
    uint32_t ulNowTick;

    ulNowTick = (uint32_t)rt_tick_get();
    MOMOFIND_PollAt(ulNowTick);
    if (!MOMOFIND_GetSnapshot(&tSnapshot))
    {
        return;
    }

    if ((MOMO_FIND_STATE_ACTIVE == tSnapshot.eState) ||
        (MOMO_FIND_STATE_ARMING == tSnapshot.eState))
    {
        lv_disp_trig_activity(NULL);
        if (NULL == l_tFindGui.pRoot)
        {
            (void)Local_CreateOverlay(tSnapshot.ulGeneration);
        }
    }
    else if (MOMO_FIND_STATE_STOPPING == tSnapshot.eState)
    {
        if (NULL != l_tFindGui.pRoot)
        {
            lv_obj_del(l_tFindGui.pRoot);
            l_tFindGui.pRoot = NULL;
        }
        (void)MOMOFIND_AcknowledgeUiStopped(ulNowTick);
    }

    if (MOMOFIND_PeekEndEvent(&tEvent))
    {
#ifdef BSP_USING_PC_SIMULATOR
        MOMOFIND_AcknowledgeEndEvent();
#else
        if (ble_link_queue_find_end(
                tEvent.usSessionId,
                MOMOFIND_EndReasonName(tEvent.eReason)))
        {
            MOMOFIND_AcknowledgeEndEvent();
        }
#endif /* BSP_USING_PC_SIMULATOR */
    }

    return;
}

bool MOMOFINDGUI_HandleHomeKey(void)
{
    MOMO_FIND_SNAPSHOT tSnapshot;

    if (!MOMOFIND_GetSnapshot(&tSnapshot) ||
        ((MOMO_FIND_STATE_ACTIVE != tSnapshot.eState) &&
         (MOMO_FIND_STATE_ARMING != tSnapshot.eState)))
    {
        return false;
    }
    (void)MOMOFIND_StopAt(MOMO_FIND_END_USER_STOP,
                          (uint32_t)rt_tick_get());

    return true;
}

#else

void MOMOFINDGUI_Init(void)
{
    return;
}

void MOMOFINDGUI_Poll(void)
{
    return;
}

bool MOMOFINDGUI_HandleHomeKey(void)
{
    return false;
}

#endif /* MOMO_FIND_ME */
