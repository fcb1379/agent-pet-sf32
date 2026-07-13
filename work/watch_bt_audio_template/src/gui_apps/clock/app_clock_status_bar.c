#include "app_clock_status_bar.h"
#include "app_mem.h"
#include "ble_ios_services.h"

static lv_obj_t *app_clock_main_status_bar;
static lv_obj_t *status_bar_area_up;
static lv_obj_t *status_bar_area_down;
static lv_obj_t *ios_noti_title_label;
static lv_obj_t *ios_noti_content_label;
static lv_obj_t *ios_media_title_label;
static lv_obj_t *ios_media_content_label;
static lv_timer_t *ios_status_refresh_timer;
static uint32_t ios_last_ancs_count;
static uint32_t ios_last_ams_count;

static rt_bool_t alarm_enabled = RT_TRUE;
static rt_bool_t timer_enabled = RT_TRUE;


static lv_obj_t *app_clock_tileview;


static const lv_btnmatrix_ctrl_t btnm_ctrl_map[] =
{
    1 | LV_BTNMATRIX_CTRL_DISABLED,  1 | LV_BTNMATRIX_CTRL_DISABLED,
    1 | LV_BTNMATRIX_CTRL_CHECKABLE, 1 | LV_BTNMATRIX_CTRL_CHECKABLE,
};

static const char *btnm_map[] = {"BT", "GPS", "\n",
                                 "ALARM", "TIMER", ""
                                };
#define PX_5mm LV_DPX(32) //160 is 1 inch(about 2.5cm)
#define PX_1cm LV_DPX(64) //160 is 1 inch(about 2.5cm)
#define PHONE_TEXT_WIDTH LV_PCT(86)
#define PHONE_LINE_GAP LV_DPX(12)
#define PHONE_TITLE_HEIGHT LV_DPX(40)
#define PHONE_CONTENT_HEIGHT LV_DPX(34)

#ifndef SF32LB55X
    #if (LV_USE_LABEL && LV_USE_CANVAS && LV_DRAW_COMPLEX) && defined(BSP_USING_PSRAM)
        #define ENABLE_GRADIENT_LABEL
    #endif
#endif /* SF32LB55X */
static void app_clock_main_press_to_show_status_bar(lv_event_t *event)
{

    if (LV_EVENT_PRESSED == event->code)
    {
        //rt_kprintf("app_clock_main_press_to_show_status_bar\n");

        lv_obj_set_tile_id(app_clock_main_status_bar, 0, 1, false);
        lv_obj_clear_flag(app_clock_main_status_bar, LV_OBJ_FLAG_HIDDEN);
        //Bring status bar to foreground
        lv_obj_move_foreground(app_clock_main_status_bar);
    }
}



static void app_clock_main_status_bar_event_cb(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);


    switch (event->code)
    {
    case LV_EVENT_RELEASED:
    case LV_EVENT_VALUE_CHANGED:
    {
        rt_uint32_t active_pos = (rt_uint32_t)lv_event_get_param(event);
        rt_kprintf("LV_EVENT_VALUE_CHANGED  %d\n", active_pos);

        if (1 == active_pos)
            lv_obj_add_flag(app_clock_main_status_bar, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(app_clock_main_status_bar, LV_OBJ_FLAG_HIDDEN);

        if (1 == active_pos) lv_ext_font_reset();


        break;
    }
    case LV_EVENT_SHORT_CLICKED:
    case LV_EVENT_LONG_PRESSED:
    case LV_EVENT_CLICKED:

    case LV_EVENT_FOCUSED:
    default:
        //printf("Released\n");

        break;
    }
}

static void btnm_event_handler(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);

    if (event->code == LV_EVENT_VALUE_CHANGED)
    {
        const char *txt = lv_btnmatrix_get_btn_text(obj, lv_btnmatrix_get_selected_btn(obj));

        if (txt)
        {
            rt_kprintf("%s was pressed\n", txt);

            if (0 == strcmp(txt, "ALARM"))
                alarm_enabled = !alarm_enabled;

            if (0 == strcmp(txt, "TIMER"))
                timer_enabled = !timer_enabled;
        }
    }
}

#ifdef ENABLE_GRADIENT_LABEL
/*
    pull up hidden control panel
*/
#define MASK_WIDTH 200
#define MASK_HEIGHT 45

static void add_mask_event_cb(lv_event_t *e)
{
    static lv_draw_mask_map_param_t m;
    static int16_t mask_id;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_opa_t *mask_map = lv_event_get_user_data(e);
    if (code == LV_EVENT_COVER_CHECK)
    {
        lv_event_set_cover_res(e, LV_COVER_RES_MASKED);
    }
    else if (code == LV_EVENT_DRAW_MAIN_BEGIN)
    {
        lv_draw_mask_map_init(&m, &obj->coords, mask_map);
        mask_id = lv_draw_mask_add(&m, NULL);
    }
    else if (code == LV_EVENT_DRAW_MAIN_END)
    {
        lv_draw_mask_free_param(&m);
        lv_draw_mask_remove_id(mask_id);
    }
    else if (code == LV_EVENT_DELETE)
    {
        app_cache_free(mask_map);
    }
}

static lv_obj_t *gradient_label(lv_obj_t *parent, const char *text)
{
    /* Create the mask of a text by drawing it to a canvas*/
    lv_opa_t *mask_map = app_cache_alloc(MASK_WIDTH * MASK_HEIGHT, IMAGE_CACHE_PSRAM);

    LV_ASSERT(mask_map);

    /*Create a "8 bit alpha" canvas and clear it*/
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, mask_map, MASK_WIDTH, MASK_HEIGHT, LV_IMG_CF_ALPHA_8BIT);
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

    /*Draw a label to the canvas. The result "image" will be used as mask*/
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_white();
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
    lv_canvas_draw_text(canvas, 5, 5, MASK_WIDTH, &label_dsc, text);

    /*The mask is reads the canvas is not required anymore*/
    lv_obj_del(canvas);

    /* Create an object from where the text will be masked out.
     * Now it's a rectangle with a gradient but it could be an image too*/
    lv_obj_t *grad = lv_obj_create(parent);
    lv_obj_set_size(grad, MASK_WIDTH, MASK_HEIGHT);
    lv_obj_center(grad);
    lv_obj_set_style_bg_color(grad, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_grad_color(grad, lv_color_hex(0x0000ff), 0);
    lv_obj_set_style_bg_grad_dir(grad, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_radius(grad, 0, 0);
    lv_obj_add_event_cb(grad, add_mask_event_cb, LV_EVENT_ALL, mask_map);

    return grad;
}
#endif /* ENABLE_GRADIENT_LABEL */

static void control_panel_content_init(lv_obj_t *par)
{
    lv_obj_t *redraw_interval_slider, *clock_step_ms_slider;
    lv_obj_t *label1, *label2;

#ifdef ENABLE_GRADIENT_LABEL
    label1 = gradient_label(par, "Functioooooooooooooooons");
#else
    label1 = lv_label_create(par);
    lv_label_set_text(label1, "Functions");
#endif
    lv_obj_align(label1, LV_ALIGN_TOP_MID, 0, PX_5mm);

    lv_obj_t *btnm1 = lv_btnmatrix_create(par);
    lv_btnmatrix_set_map(btnm1, btnm_map);
    lv_obj_set_width(btnm1, LV_PCT(75));
    //lv_btnm_set_btn_width(btnm1, 10, 2);        /*Make "Action1" twice as wide as "Action2"*/
    lv_obj_add_event_cb(btnm1, btnm_event_handler, LV_EVENT_ALL, NULL);
    lv_btnmatrix_set_ctrl_map(btnm1, btnm_ctrl_map);
    lv_obj_align_to(btnm1, label1, LV_ALIGN_OUT_BOTTOM_MID, 0, PX_5mm);



    label2 = lv_label_create(par);
    lv_label_set_text(label2, "clock redraw time & steps");
    lv_obj_align_to(label2, btnm1, LV_ALIGN_OUT_BOTTOM_MID, 0, PX_1cm);

    redraw_interval_slider = lv_slider_create(par);
    lv_bar_set_range(redraw_interval_slider, 1, 3000);
    //lv_bar_set_value(redraw_interval_slider, CLOCK_MIN_REDRAW_INTERVAL_MS, LV_ANIM_ON);
    lv_obj_set_width(redraw_interval_slider, LV_PCT(75));
    lv_obj_align_to(redraw_interval_slider, label2, LV_ALIGN_OUT_BOTTOM_MID, 0, PX_5mm);

    //lv_obj_set_event_cb(redraw_interval_slider, redraw_interval_slider_event_handler);


    clock_step_ms_slider = lv_slider_create(par);
    lv_bar_set_range(clock_step_ms_slider, 1, 6000);
    //lv_bar_set_value(clock_step_ms_slider, CLOCK_MIN_REDRAW_INTERVAL_MS, LV_ANIM_ON);
    lv_obj_set_width(clock_step_ms_slider, LV_PCT(75));
    lv_obj_align_to(clock_step_ms_slider, redraw_interval_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, PX_1cm);

    //lv_obj_set_event_cb(clock_step_ms_slider, clock_step_ms_event_handler);
}

static void ios_status_update_labels(void)
{
    ble_ios_services_snapshot_t snapshot;
    char title[160];
    char content[256];

    if (!ios_noti_title_label || !ios_noti_content_label ||
            !ios_media_title_label || !ios_media_content_label)
    {
        return;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    ble_ios_services_get_snapshot(&snapshot);

    if (snapshot.ancs_count != ios_last_ancs_count)
    {
        ios_last_ancs_count = snapshot.ancs_count;
        if (snapshot.ancs_count == 0)
        {
            lv_label_set_text(ios_noti_title_label, "Notify");
            lv_label_set_text(ios_noti_content_label, "No notice");
        }
        else
        {
            rt_snprintf(title, sizeof(title), "%s",
                        snapshot.last_app[0] ? snapshot.last_app : "Notify");
            rt_snprintf(content, sizeof(content), "%s",
                        snapshot.last_title[0] ? snapshot.last_title :
                        (snapshot.last_message[0] ? snapshot.last_message : "New notice"));
            lv_label_set_text(ios_noti_title_label, title);
            lv_label_set_text(ios_noti_content_label, content);
        }
    }

    if (snapshot.ams_count != ios_last_ams_count)
    {
        ios_last_ams_count = snapshot.ams_count;
        if (snapshot.ams_count == 0)
        {
            lv_label_set_text(ios_media_title_label, "Media");
            lv_label_set_text(ios_media_content_label, "No media");
        }
        else
        {
            rt_snprintf(title, sizeof(title), "%s",
                        snapshot.player[0] ? snapshot.player : "Media");
            rt_snprintf(content, sizeof(content), "%s",
                        snapshot.track[0] ? snapshot.track :
                        (snapshot.artist[0] ? snapshot.artist : "Playing"));
            lv_label_set_text(ios_media_title_label, title);
            lv_label_set_text(ios_media_content_label, content);
        }
    }
}

static void ios_status_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ios_status_update_labels();
}

static lv_obj_t *phone_text_block_create(lv_obj_t *par, lv_obj_t *align_base,
        const char *title, const char *content,
        lv_obj_t **title_label_out, lv_obj_t **content_label_out)
{
    lv_obj_t *title_box = lv_obj_create(par);
    lv_obj_set_size(title_box, PHONE_TEXT_WIDTH, PHONE_TITLE_HEIGHT);
    lv_obj_set_style_bg_color(title_box, LV_COLOR_BLACK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(title_box, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(title_box, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(title_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(title_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(title_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(title_box);
    lv_obj_set_size(title_label, LV_PCT(100), LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title_label, LV_COLOR_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(title_label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(title_label, LV_COLOR_BLACK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(title_label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(title_label);

    lv_obj_t *content_box = lv_obj_create(par);
    lv_obj_set_size(content_box, PHONE_TEXT_WIDTH, PHONE_CONTENT_HEIGHT);
    lv_obj_set_style_bg_color(content_box, LV_COLOR_BLACK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(content_box, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(content_box, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(content_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(content_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(content_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *content_label = lv_label_create(content_box);
    lv_obj_set_size(content_label, LV_PCT(100), LV_PCT(100));
    lv_label_set_long_mode(content_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(content_label, content);
    lv_obj_set_style_text_align(content_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(content_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(content_label, lv_color_hex(0xD8D8D8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(content_label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(content_label, LV_COLOR_BLACK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(content_label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(content_label);

    lv_obj_align_to(title_box, align_base, LV_ALIGN_OUT_BOTTOM_MID, 0, PHONE_LINE_GAP);
    lv_obj_align_to(content_box, title_box, LV_ALIGN_OUT_BOTTOM_MID, 0, LV_DPX(4));

    if (title_label_out)
    {
        *title_label_out = title_label;
    }
    if (content_label_out)
    {
        *content_label_out = content_label;
    }

    return content_box;
}

/*
    drop down hidden msg list
*/
static void msg_list_content_init(lv_obj_t *par)
{
    lv_obj_t *label_header;
    lv_obj_t *align_base;

    label_header = lv_label_create(par);
    lv_label_set_text(label_header, "Phone");
    lv_obj_set_width(label_header, PHONE_TEXT_WIDTH);
    lv_obj_set_style_text_align(label_header, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_header, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_header, LV_COLOR_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label_header, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_header, LV_ALIGN_TOP_MID, 0, LV_DPX(18));

    align_base = label_header;
    align_base = phone_text_block_create(par, align_base,
                                         "Notify",
                                         "No notice",
                                         &ios_noti_title_label,
                                         &ios_noti_content_label);
    align_base = phone_text_block_create(par, align_base,
                                         "Media",
                                         "No media",
                                         &ios_media_title_label,
                                         &ios_media_content_label);

    ios_status_update_labels();
}

void app_clock_main_status_bar_init(lv_obj_t *par, lv_obj_t *clock_tileview)
{
    rt_uint16_t i;
    lv_obj_t *tileview;
    lv_obj_t *pages[3];
    lv_obj_t *status_bar_area;

    //create a invisible object at top of parent, and shown status bar when press it
    for (i = 0; i < 2; i++)
    {
        status_bar_area = lv_obj_create(par);
        lv_obj_set_size(status_bar_area, LV_HOR_RES_MAX, (LV_VER_RES_MAX >> 4));
        lv_obj_set_style_border_opa(status_bar_area, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_scrollbar_mode(status_bar_area, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(status_bar_area, LV_OBJ_FLAG_PRESS_LOCK); //Allow press event to tileview
        lv_obj_add_event_cb(status_bar_area, app_clock_main_press_to_show_status_bar, LV_EVENT_ALL, NULL);
        lv_obj_set_style_bg_opa(status_bar_area, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);


        if (0 == i)
        {
            lv_obj_align(status_bar_area, LV_ALIGN_BOTTOM_MID, 0, 0);
            status_bar_area_down = status_bar_area;
        }
        else if (1 == i)
        {
            lv_obj_align(status_bar_area, LV_ALIGN_TOP_MID, 0, 0);
            status_bar_area_up = status_bar_area;
        }

    }

    //create tile view , page 0  for content, page 1 is transparent
    tileview = lv_tileview_create(par);
    app_clock_main_status_bar = tileview;
    lv_obj_add_flag(tileview, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);


    for (i = 0; i < 3; i++)
    {
        pages[i] = lv_tileview_add_tile(tileview, 0, i, LV_DIR_VER);

        if (i == 1)
        {
            lv_obj_set_style_bg_opa(pages[i], LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            lv_color_t color = LV_COLOR_BLACK;
            lv_obj_set_style_bg_opa(pages[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(pages[i], color, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_obj_set_size(pages[i], LV_HOR_RES_MAX, LV_VER_RES_MAX);
        lv_obj_set_pos(pages[i], 0, (LV_VER_RES_MAX * i));
        lv_obj_set_scrollbar_mode(pages[i], LV_SCROLLBAR_MODE_OFF);
    }

    msg_list_content_init(pages[0]);
    control_panel_content_init(pages[2]);

    ios_status_refresh_timer = lv_timer_create(ios_status_refresh_timer_cb, 1000, NULL);

    //scroll to page[1]
    lv_obj_set_tile_id(tileview, 0, 1, false);
    lv_obj_add_event_cb(tileview, app_clock_main_status_bar_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(tileview, LV_OBJ_FLAG_HIDDEN);

    app_clock_tileview = clock_tileview;
}

void app_clock_main_status_bar_deinit(void)
{
    lv_obj_del(app_clock_main_status_bar);
    lv_obj_del(status_bar_area_up);
    lv_obj_del(status_bar_area_down);

    if (ios_status_refresh_timer)
    {
        lv_timer_del(ios_status_refresh_timer);
        ios_status_refresh_timer = NULL;
    }

    app_clock_main_status_bar = NULL;
    status_bar_area_up = NULL;
    status_bar_area_down = NULL;
    ios_noti_title_label = NULL;
    ios_noti_content_label = NULL;
    ios_media_title_label = NULL;
    ios_media_content_label = NULL;
    ios_last_ancs_count = 0;
    ios_last_ams_count = 0;
    lv_ext_font_reset();
}
