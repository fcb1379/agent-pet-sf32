#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"

#define APP_ID "calculator"
#define CALC_MAX_DIGITS 14

typedef enum
{
    CALC_KEY_CLEAR = 1,
    CALC_KEY_SIGN,
    CALC_KEY_PERCENT,
    CALC_KEY_DIVIDE,
    CALC_KEY_7,
    CALC_KEY_8,
    CALC_KEY_9,
    CALC_KEY_MULTIPLY,
    CALC_KEY_4,
    CALC_KEY_5,
    CALC_KEY_6,
    CALC_KEY_SUBTRACT,
    CALC_KEY_1,
    CALC_KEY_2,
    CALC_KEY_3,
    CALC_KEY_ADD,
    CALC_KEY_0,
    CALC_KEY_DECIMAL,
    CALC_KEY_EQUALS,
} calculator_key_t;

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *display;
    lv_obj_t *expression;
    double accumulator;
    char pending_operator;
    char value[24];
    uint8_t has_accumulator;
    uint8_t replace_value;
    uint8_t error;
} calculator_ui_t;

static calculator_ui_t g_calculator_ui;

static void calculator_set_value(double value)
{
    rt_snprintf(g_calculator_ui.value, sizeof(g_calculator_ui.value), "%.10g", value);
    if (rt_strlen(g_calculator_ui.value) > CALC_MAX_DIGITS)
    {
        rt_snprintf(g_calculator_ui.value, sizeof(g_calculator_ui.value), "%.7g", value);
    }
}

static double calculator_value(void)
{
    return strtod(g_calculator_ui.value, RT_NULL);
}

static void calculator_refresh(void)
{
    char expression[32];

    if (g_calculator_ui.error)
    {
        lv_label_set_text(g_calculator_ui.display, "Error");
        lv_label_set_text(g_calculator_ui.expression, "Tap C to continue");
        return;
    }

    lv_label_set_text(g_calculator_ui.display, g_calculator_ui.value);
    if (g_calculator_ui.pending_operator)
    {
        rt_snprintf(expression, sizeof(expression), "%.8g %c", g_calculator_ui.accumulator,
                    g_calculator_ui.pending_operator);
        lv_label_set_text(g_calculator_ui.expression, expression);
    }
    else
    {
        lv_label_set_text(g_calculator_ui.expression, "Calculator");
    }
}

static void calculator_reset(void)
{
    g_calculator_ui.accumulator = 0;
    g_calculator_ui.pending_operator = '\0';
    g_calculator_ui.has_accumulator = 0;
    g_calculator_ui.replace_value = 1;
    g_calculator_ui.error = 0;
    rt_strncpy(g_calculator_ui.value, "0", sizeof(g_calculator_ui.value));
    calculator_refresh();
}

static int calculator_apply_pending(double rhs)
{
    switch (g_calculator_ui.pending_operator)
    {
    case '+':
        g_calculator_ui.accumulator += rhs;
        break;
    case '-':
        g_calculator_ui.accumulator -= rhs;
        break;
    case '*':
        g_calculator_ui.accumulator *= rhs;
        break;
    case '/':
        if (rhs == 0)
        {
            g_calculator_ui.error = 1;
            return -RT_EINVAL;
        }
        g_calculator_ui.accumulator /= rhs;
        break;
    default:
        g_calculator_ui.accumulator = rhs;
        break;
    }

    return RT_EOK;
}

static void calculator_append(const char *text)
{
    size_t length;

    if (g_calculator_ui.error)
    {
        calculator_reset();
    }

    if (g_calculator_ui.replace_value)
    {
        rt_strncpy(g_calculator_ui.value, text, sizeof(g_calculator_ui.value));
        g_calculator_ui.replace_value = 0;
    }
    else if (!(g_calculator_ui.value[0] == '0' && g_calculator_ui.value[1] == '\0' && text[0] != '.'))
    {
        length = rt_strlen(g_calculator_ui.value);
        if (length + rt_strlen(text) < sizeof(g_calculator_ui.value) - 1 && length < CALC_MAX_DIGITS)
        {
            strcat(g_calculator_ui.value, text);
        }
    }
    else
    {
        rt_strncpy(g_calculator_ui.value, text, sizeof(g_calculator_ui.value));
    }

    calculator_refresh();
}

static void calculator_select_operator(char operation)
{
    double value = calculator_value();

    if (g_calculator_ui.error)
    {
        return;
    }

    if (g_calculator_ui.has_accumulator && !g_calculator_ui.replace_value)
    {
        if (calculator_apply_pending(value) != RT_EOK)
        {
            calculator_refresh();
            return;
        }
    }
    else if (!g_calculator_ui.has_accumulator)
    {
        g_calculator_ui.accumulator = value;
        g_calculator_ui.has_accumulator = 1;
    }

    g_calculator_ui.pending_operator = operation;
    g_calculator_ui.replace_value = 1;
    calculator_set_value(g_calculator_ui.accumulator);
    calculator_refresh();
}

static void calculator_equals(void)
{
    if (!g_calculator_ui.error && g_calculator_ui.has_accumulator && g_calculator_ui.pending_operator)
    {
        if (calculator_apply_pending(calculator_value()) == RT_EOK)
        {
            calculator_set_value(g_calculator_ui.accumulator);
            g_calculator_ui.pending_operator = '\0';
            g_calculator_ui.has_accumulator = 0;
            g_calculator_ui.replace_value = 1;
        }
    }
    calculator_refresh();
}

static void calculator_button_event(lv_event_t *event)
{
    calculator_key_t key = (calculator_key_t)(uintptr_t)lv_event_get_user_data(event);

    if (lv_event_get_code(event) != LV_EVENT_CLICKED && lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED)
    {
        return;
    }

    switch (key)
    {
    case CALC_KEY_CLEAR:
        calculator_reset();
        break;
    case CALC_KEY_SIGN:
        if (!g_calculator_ui.error)
        {
            calculator_set_value(-calculator_value());
            calculator_refresh();
        }
        break;
    case CALC_KEY_PERCENT:
        if (!g_calculator_ui.error)
        {
            calculator_set_value(calculator_value() / 100.0);
            calculator_refresh();
        }
        break;
    case CALC_KEY_DIVIDE:
        calculator_select_operator('/');
        break;
    case CALC_KEY_MULTIPLY:
        calculator_select_operator('*');
        break;
    case CALC_KEY_SUBTRACT:
        calculator_select_operator('-');
        break;
    case CALC_KEY_ADD:
        calculator_select_operator('+');
        break;
    case CALC_KEY_EQUALS:
        calculator_equals();
        break;
    case CALC_KEY_DECIMAL:
        if (!strchr(g_calculator_ui.value, '.'))
        {
            calculator_append(g_calculator_ui.replace_value ? "0." : ".");
        }
        break;
    case CALC_KEY_0:
        calculator_append("0");
        break;
    case CALC_KEY_1:
        calculator_append("1");
        break;
    case CALC_KEY_2:
        calculator_append("2");
        break;
    case CALC_KEY_3:
        calculator_append("3");
        break;
    case CALC_KEY_4:
        calculator_append("4");
        break;
    case CALC_KEY_5:
        calculator_append("5");
        break;
    case CALC_KEY_6:
        calculator_append("6");
        break;
    case CALC_KEY_7:
        calculator_append("7");
        break;
    case CALC_KEY_8:
        calculator_append("8");
        break;
    case CALC_KEY_9:
        calculator_append("9");
        break;
    default:
    {
        break;
    }
    }
}

static lv_obj_t *calculator_create_button(lv_obj_t *parent, const char *text,
                                          lv_coord_t x, lv_coord_t y, lv_coord_t width,
                                          lv_coord_t height, calculator_key_t key,
                                          lv_color_t color)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 5, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, calculator_button_event, LV_EVENT_ALL, (void *)(uintptr_t)key);

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

static void calculator_on_start(void)
{
    static const char *const labels[5][4] =
    {
        { "C", "+/-", "%", "/" },
        { "7", "8", "9", "x" },
        { "4", "5", "6", "-" },
        { "1", "2", "3", "+" },
        { "0", ".", "=", "" },
    };
    static const calculator_key_t keys[5][4] =
    {
        { CALC_KEY_CLEAR, CALC_KEY_SIGN, CALC_KEY_PERCENT, CALC_KEY_DIVIDE },
        { CALC_KEY_7, CALC_KEY_8, CALC_KEY_9, CALC_KEY_MULTIPLY },
        { CALC_KEY_4, CALC_KEY_5, CALC_KEY_6, CALC_KEY_SUBTRACT },
        { CALC_KEY_1, CALC_KEY_2, CALC_KEY_3, CALC_KEY_ADD },
        { CALC_KEY_0, CALC_KEY_DECIMAL, CALC_KEY_EQUALS, CALC_KEY_EQUALS },
    };
    const lv_coord_t margin = 8;
    const lv_coord_t gap = 5;
    lv_coord_t button_width = (LV_HOR_RES_MAX - margin * 2 - gap * 3) / 4;
    lv_coord_t grid_y = 62;
    lv_coord_t button_height = (LV_VER_RES_MAX - grid_y - margin - gap * 4) / 5;
    uint8_t row;
    uint8_t col;

    rt_memset(&g_calculator_ui, 0, sizeof(g_calculator_ui));
    g_calculator_ui.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_calculator_ui.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(g_calculator_ui.root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_calculator_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_calculator_ui.root, 0, 0);
    lv_obj_set_style_pad_all(g_calculator_ui.root, 0, 0);
    lv_obj_clear_flag(g_calculator_ui.root, LV_OBJ_FLAG_SCROLLABLE);

    g_calculator_ui.expression = lv_label_create(g_calculator_ui.root);
    lv_obj_set_pos(g_calculator_ui.expression, margin, 8);
    lv_obj_set_size(g_calculator_ui.expression, LV_HOR_RES_MAX - margin * 2, 18);
    lv_label_set_long_mode(g_calculator_ui.expression, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_calculator_ui.expression, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(g_calculator_ui.expression, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(g_calculator_ui.expression, lv_color_hex(0x8ca4bf), 0);

    g_calculator_ui.display = lv_label_create(g_calculator_ui.root);
    lv_obj_set_pos(g_calculator_ui.display, margin, 24);
    lv_obj_set_size(g_calculator_ui.display, LV_HOR_RES_MAX - margin * 2, 34);
    lv_label_set_long_mode(g_calculator_ui.display, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_calculator_ui.display, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(g_calculator_ui.display, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(g_calculator_ui.display, lv_color_white(), 0);

    for (row = 0; row < 5; row++)
    {
        for (col = 0; col < 4; col++)
        {
            lv_coord_t x;
            lv_coord_t width;
            lv_color_t color;

            if (row == 4 && col == 3)
            {
                continue;
            }
            x = margin + col * (button_width + gap);
            width = button_width;
            if (row == 4 && col == 0)
            {
                width = button_width * 2 + gap;
            }
            else if (row == 4 && col == 1)
            {
                x = margin + 2 * (button_width + gap);
            }
            else if (row == 4 && col == 2)
            {
                x = margin + 3 * (button_width + gap);
            }

            color = (col == 3 || (row == 4 && col == 2)) ? lv_color_hex(0x2678c9) :
                    (row == 0 ? lv_color_hex(0x49566b) : lv_color_hex(0x202a37));
            calculator_create_button(g_calculator_ui.root, labels[row][col], x,
                                     grid_y + row * (button_height + gap), width,
                                     button_height, keys[row][col], color);
        }
    }

    calculator_reset();
}

static void calculator_on_stop(void)
{
    if (g_calculator_ui.root)
    {
        lv_obj_del(g_calculator_ui.root);
    }
    rt_memset(&g_calculator_ui, 0, sizeof(g_calculator_ui));
}

static void calculator_msg_handler(gui_app_msg_type_t msg, void *param)
{
    (void)param;

    if (msg == GUI_APP_MSG_ONSTART)
    {
        calculator_on_start();
    }
    else if (msg == GUI_APP_MSG_ONSTOP)
    {
        calculator_on_stop();
    }
}

static int calculator_app_main(intent_t intent)
{
    (void)intent;
    gui_app_regist_msg_handler(APP_ID, calculator_msg_handler);
    return 0;
}

LV_IMG_DECLARE(img_calculator);
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(calculator), LV_EXT_IMG_GET(img_calculator), APP_ID, calculator_app_main);
