#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#include "app_radio.h"
#include "esp_log.h"
#include "lv_port.h"

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(font_radio_cn_18);

static const char *TAG = "app_ui";

static lv_obj_t *main_page;
static lv_obj_t *apps_page;
static lv_obj_t *radio_page;
static lv_obj_t *touch_label;
static lv_obj_t *time_group;
static lv_obj_t *clock_cards[4];
static lv_obj_t *clock_labels[4];
static lv_obj_t *colon_label;
static lv_obj_t *date_label;
static lv_obj_t *week_label;
static lv_obj_t *weather_temp_label;
static lv_obj_t *weather_meta_label;
static lv_obj_t *weather_stage;
static lv_obj_t *weather_sun;
static lv_obj_t *weather_cloud_a;
static lv_obj_t *weather_cloud_b;
static lv_obj_t *weather_cloud_c;
static lv_obj_t *weather_rain[3];
static lv_obj_t *weather_bolt;
static lv_obj_t *quote_label;
static lv_obj_t *network_status_label;
static lv_obj_t *status_bar_time_labels[2];
static lv_obj_t *radio_station_label;
static lv_obj_t *radio_state_label;
static lv_obj_t *radio_meta_label;
static uint8_t clock_digits[4] = {1, 4, 2, 8};

static lv_style_t style_screen;
static lv_style_t style_en;
static lv_style_t style_muted;
static lv_style_t style_gold;
static lv_style_t style_green;
static lv_style_t style_panel;
static lv_style_t style_row;
static lv_style_t style_clock_card;

static constexpr lv_color_t COLOR_BG = LV_COLOR_MAKE(0x00, 0x00, 0x00);
static constexpr lv_color_t COLOR_SURFACE = LV_COLOR_MAKE(0x0b, 0x0c, 0x0d);
static constexpr lv_color_t COLOR_SURFACE_2 = LV_COLOR_MAKE(0x12, 0x14, 0x13);
static constexpr lv_color_t COLOR_TEXT = LV_COLOR_MAKE(0xf6, 0xef, 0xdf);
static constexpr lv_color_t COLOR_CREAM = LV_COLOR_MAKE(0xff, 0xf4, 0xd8);
static constexpr lv_color_t COLOR_MUTED = LV_COLOR_MAKE(0x8a, 0x8a, 0x82);
static constexpr lv_color_t COLOR_LINE = LV_COLOR_MAKE(0x34, 0x35, 0x31);
static constexpr lv_color_t COLOR_GOLD = LV_COLOR_MAKE(0xff, 0xbd, 0x55);
static constexpr lv_color_t COLOR_GREEN = LV_COLOR_MAKE(0x82, 0xd7, 0x78);
static constexpr lv_color_t COLOR_PURPLE = LV_COLOR_MAKE(0xaa, 0x78, 0xff);
static constexpr lv_color_t COLOR_BLUE = LV_COLOR_MAKE(0x68, 0x9d, 0xff);
static constexpr lv_color_t COLOR_RAIN = LV_COLOR_MAKE(0x68, 0x9d, 0xff);
static constexpr lv_color_t COLOR_STORM = LV_COLOR_MAKE(0x15, 0x13, 0x1c);
static constexpr lv_color_t COLOR_FOG = LV_COLOR_MAKE(0xbc, 0xc2, 0xbd);

struct AppRow {
    const char *cn;
    const char *en;
    const char *status;
    lv_color_t color;
};

static const AppRow APP_ROWS[] = {
    {"RAD", "Radio", "Music FM", COLOR_GOLD},
    {"PIC", "Photo", "128 photos", COLOR_GREEN},
    {"AI", "XiaoZhi", "Online", COLOR_PURPLE},
    {"CAL", "Calendar", "2026/06/14", COLOR_PURPLE},
    {"QTE", "Quote", "Updated", COLOR_GOLD},
    {"SET", "Settings", "System", COLOR_BLUE},
};

static void add_gesture_bubble(lv_obj_t *obj)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

static lv_obj_t *label_en(lv_obj_t *parent, const char *text, lv_style_t *style)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_add_style(label, style, 0);
    add_gesture_bubble(label);
    return label;
}

static void show_main(void)
{
    lv_obj_clear_flag(main_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(apps_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(radio_page, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "show main");
}

static void show_apps(void)
{
    lv_obj_clear_flag(apps_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(main_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(radio_page, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "show apps");
}

static void show_radio(void)
{
    lv_obj_clear_flag(radio_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(main_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(apps_page, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "show radio");
}

static void menu_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        show_apps();
    }
}

static void back_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        show_main();
    }
}

static void radio_back_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        app_radio_stop();
        show_apps();
    }
}

static void radio_tile_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        show_radio();
    }
}

static void radio_play_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        app_radio_play_pause();
    }
}

static void radio_stop_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        app_radio_stop();
    }
}

static void radio_next_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        app_radio_next();
    }
}

static void radio_prev_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        app_radio_prev();
    }
}

static void apps_gesture_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        ESP_LOGI(TAG, "right swipe -> home");
        show_main();
    }
}

static void radio_gesture_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        ESP_LOGI(TAG, "right swipe -> apps");
        show_apps();
    }
}

static void obj_opa_cb(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, value, 0);
}

static void obj_y_cb(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, value);
}

static void rain_y_cb(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, value);
}

static void colon_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!colon_label) {
        return;
    }
    lv_opa_t opa = lv_obj_get_style_opa(colon_label, 0);
    lv_obj_set_style_opa(colon_label, opa < LV_OPA_50 ? LV_OPA_COVER : LV_OPA_20, 0);
}

void app_ui_handle_swipe(int16_t dx, int16_t dy)
{
    const int16_t min_dx = 70;
    const int16_t max_dy = 90;

    if (dx > min_dx && LV_ABS(dy) < max_dy) {
        show_main();
    } else if (dx < -min_dx && LV_ABS(dy) < max_dy) {
        show_apps();
    }
}

static void init_styles(void)
{
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, COLOR_BG);
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
    lv_style_set_border_width(&style_screen, 0);
    lv_style_set_pad_all(&style_screen, 0);

    lv_style_init(&style_en);
    lv_style_set_text_color(&style_en, COLOR_TEXT);
    lv_style_set_text_font(&style_en, &lv_font_montserrat_16);

    lv_style_init(&style_muted);
    lv_style_set_text_color(&style_muted, COLOR_MUTED);
    lv_style_set_text_font(&style_muted, &lv_font_montserrat_14);

    lv_style_init(&style_gold);
    lv_style_set_text_color(&style_gold, COLOR_GOLD);
    lv_style_set_text_font(&style_gold, &lv_font_montserrat_16);

    lv_style_init(&style_green);
    lv_style_set_text_color(&style_green, COLOR_GREEN);
    lv_style_set_text_font(&style_green, &lv_font_montserrat_16);

    lv_style_init(&style_panel);
    lv_style_set_bg_color(&style_panel, COLOR_SURFACE);
    lv_style_set_bg_opa(&style_panel, LV_OPA_COVER);
    lv_style_set_border_color(&style_panel, COLOR_LINE);
    lv_style_set_border_width(&style_panel, 1);
    lv_style_set_radius(&style_panel, 6);
    lv_style_set_pad_all(&style_panel, 0);

    lv_style_init(&style_row);
    lv_style_set_bg_color(&style_row, COLOR_SURFACE);
    lv_style_set_bg_opa(&style_row, LV_OPA_COVER);
    lv_style_set_border_color(&style_row, COLOR_LINE);
    lv_style_set_border_width(&style_row, 1);
    lv_style_set_radius(&style_row, 6);
    lv_style_set_pad_all(&style_row, 0);

    lv_style_init(&style_clock_card);
    lv_style_set_bg_color(&style_clock_card, LV_COLOR_MAKE(0x08, 0x08, 0x08));
    lv_style_set_bg_opa(&style_clock_card, LV_OPA_COVER);
    lv_style_set_border_color(&style_clock_card, LV_COLOR_MAKE(0x2a, 0x28, 0x22));
    lv_style_set_border_width(&style_clock_card, 1);
    lv_style_set_radius(&style_clock_card, 5);
    lv_style_set_shadow_color(&style_clock_card, LV_COLOR_MAKE(0x30, 0x22, 0x10));
    lv_style_set_shadow_width(&style_clock_card, 10);
    lv_style_set_shadow_opa(&style_clock_card, LV_OPA_20);
    lv_style_set_pad_all(&style_clock_card, 0);
}

static void create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *wifi = label_en(parent, "WiFi", &style_green);
    lv_obj_align(wifi, LV_ALIGN_TOP_RIGHT, -168, 12);

    lv_obj_t *time = label_en(parent, "--:--", &style_en);
    lv_obj_set_style_text_font(time, &lv_font_montserrat_20, 0);
    lv_obj_align(time, LV_ALIGN_TOP_RIGHT, -86, 9);
    for (size_t i = 0; i < sizeof(status_bar_time_labels) / sizeof(status_bar_time_labels[0]); ++i) {
        if (!status_bar_time_labels[i]) {
            status_bar_time_labels[i] = time;
            break;
        }
    }

    lv_obj_t *battery = label_en(parent, "80%", &style_green);
    lv_obj_align(battery, LV_ALIGN_TOP_RIGHT, -20, 12);
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 76, 32);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, COLOR_SURFACE_2, 0);
    lv_obj_set_style_border_color(btn, COLOR_GREEN, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    add_gesture_bubble(btn);

    lv_obj_t *txt = label_en(btn, text, &style_en);
    lv_obj_center(txt);
    return btn;
}

static lv_obj_t *create_panel(lv_obj_t *parent, int16_t w, int16_t h, int16_t x, int16_t y)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_add_style(panel, &style_panel, 0);
    lv_obj_set_size(panel, w, h);
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    add_gesture_bubble(panel);
    return panel;
}

static lv_obj_t *circle(lv_obj_t *parent, int16_t size, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, size / 2, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    add_gesture_bubble(obj);
    return obj;
}

static lv_obj_t *bar(lv_obj_t *parent, int16_t w, int16_t h, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, LV_MIN(w, h) / 2, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    add_gesture_bubble(obj);
    return obj;
}

static void start_weather_animations(void)
{
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, weather_sun);
    lv_anim_set_values(&anim, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_time(&anim, 1600);
    lv_anim_set_playback_time(&anim, 1600);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&anim, obj_opa_cb);
    lv_anim_start(&anim);

    for (int i = 0; i < 3; ++i) {
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, weather_rain[i]);
        lv_anim_set_values(&anim, 84, 122);
        lv_anim_set_time(&anim, 700 + i * 120);
        lv_anim_set_playback_delay(&anim, 80 * i);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&anim, rain_y_cb);
        lv_anim_start(&anim);
    }
}

static void set_weather_scene(const char *summary, int weather_code)
{
    if (!weather_stage) {
        return;
    }

    bool rain = false;
    bool storm = false;
    bool fog = false;
    bool cloudy = false;
    if (weather_code >= 0) {
        cloudy = weather_code == 1 || weather_code == 2 || weather_code == 3;
        fog = weather_code == 45 || weather_code == 48;
        rain = (weather_code >= 51 && weather_code <= 67) || (weather_code >= 80 && weather_code <= 82);
        storm = weather_code >= 95;
    } else if (summary) {
        cloudy = strstr(summary, "Cloud") != NULL;
        fog = strstr(summary, "Fog") != NULL;
        rain = strstr(summary, "Rain") != NULL;
        storm = strstr(summary, "Storm") != NULL;
    }

    lv_obj_set_style_bg_color(weather_stage, storm ? COLOR_STORM : COLOR_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(weather_sun, (cloudy || rain || fog || storm) ? LV_OPA_40 : LV_OPA_COVER, 0);
    if (storm) {
        lv_obj_clear_flag(weather_bolt, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(weather_bolt, LV_OBJ_FLAG_HIDDEN);
    }

    bool show_cloud = cloudy || rain || fog || storm;
    lv_obj_set_style_bg_opa(weather_cloud_a, show_cloud ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_bg_opa(weather_cloud_b, show_cloud ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_bg_opa(weather_cloud_c, show_cloud ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_bg_color(weather_cloud_a, fog ? COLOR_FOG : COLOR_CREAM, 0);
    lv_obj_set_style_bg_color(weather_cloud_b, fog ? COLOR_FOG : COLOR_CREAM, 0);
    lv_obj_set_style_bg_color(weather_cloud_c, fog ? COLOR_FOG : COLOR_CREAM, 0);

    for (int i = 0; i < 3; ++i) {
        if (rain || storm) {
            lv_obj_clear_flag(weather_rain[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(weather_rain[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void create_weather_stage(lv_obj_t *parent)
{
    lv_obj_t *weather_panel = create_panel(parent, 166, 154, 294, 50);
    lv_obj_set_style_bg_color(weather_panel, COLOR_SURFACE_2, 0);
    weather_stage = weather_panel;

    lv_obj_t *title = label_en(weather_panel, "Weather", &style_muted);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

    weather_sun = circle(weather_panel, 54, COLOR_GOLD, LV_OPA_COVER);
    lv_obj_align(weather_sun, LV_ALIGN_TOP_LEFT, 56, 32);

    weather_cloud_a = circle(weather_panel, 38, COLOR_CREAM, LV_OPA_30);
    lv_obj_align(weather_cloud_a, LV_ALIGN_TOP_LEFT, 48, 66);
    weather_cloud_b = circle(weather_panel, 46, COLOR_CREAM, LV_OPA_30);
    lv_obj_align(weather_cloud_b, LV_ALIGN_TOP_LEFT, 76, 58);
    weather_cloud_c = bar(weather_panel, 80, 26, COLOR_CREAM, LV_OPA_30);
    lv_obj_align(weather_cloud_c, LV_ALIGN_TOP_LEFT, 48, 82);

    for (int i = 0; i < 3; ++i) {
        weather_rain[i] = bar(weather_panel, 5, 20, COLOR_RAIN, LV_OPA_COVER);
        lv_obj_align(weather_rain[i], LV_ALIGN_TOP_LEFT, 62 + i * 24, 94);
        lv_obj_add_flag(weather_rain[i], LV_OBJ_FLAG_HIDDEN);
    }

    weather_bolt = label_en(weather_panel, "Z", &style_gold);
    lv_obj_set_style_text_font(weather_bolt, &lv_font_montserrat_20, 0);
    lv_obj_align(weather_bolt, LV_ALIGN_TOP_LEFT, 116, 76);
    lv_obj_add_flag(weather_bolt, LV_OBJ_FLAG_HIDDEN);

    weather_temp_label = label_en(weather_panel, "-- C", &style_en);
    lv_obj_set_style_text_font(weather_temp_label, &lv_font_montserrat_20, 0);
    lv_obj_align(weather_temp_label, LV_ALIGN_BOTTOM_LEFT, 12, -27);

    weather_meta_label = label_en(weather_panel, "Weather pending", &style_green);
    lv_obj_set_width(weather_meta_label, 142);
    lv_label_set_long_mode(weather_meta_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(weather_meta_label, &lv_font_montserrat_12, 0);
    lv_obj_align(weather_meta_label, LV_ALIGN_BOTTOM_LEFT, 12, -10);

    start_weather_animations();
    set_weather_scene(NULL, -1);
}

static lv_obj_t *create_clock_card(lv_obj_t *parent, int16_t x, lv_color_t color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, &style_clock_card, 0);
    lv_obj_set_size(card, 48, 92);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, x, 52);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    add_gesture_bubble(card);

    lv_obj_t *top = lv_obj_create(card);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, 44, 1);
    lv_obj_set_style_bg_color(top, COLOR_LINE, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_70, 0);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 45);

    lv_obj_t *shade = lv_obj_create(card);
    lv_obj_remove_style_all(shade);
    lv_obj_set_size(shade, 46, 43);
    lv_obj_set_style_bg_color(shade, LV_COLOR_MAKE(0x16, 0x13, 0x0d), 0);
    lv_obj_set_style_bg_opa(shade, LV_OPA_40, 0);
    lv_obj_align(shade, LV_ALIGN_TOP_MID, 0, 1);

    lv_obj_t *digit = label_en(card, "0", &style_en);
    lv_obj_set_style_text_font(digit, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(digit, color, 0);
    lv_obj_align(digit, LV_ALIGN_CENTER, 0, -2);
    return card;
}

static void flip_digit(uint8_t index, uint8_t digit, bool animate)
{
    if (index >= 4 || !clock_labels[index]) {
        return;
    }
    if (clock_digits[index] == digit && animate) {
        return;
    }

    clock_digits[index] = digit;
    char text[2] = {(char)('0' + digit), 0};
    lv_label_set_text(clock_labels[index], text);
    lv_obj_align(clock_labels[index], LV_ALIGN_CENTER, 0, -2);

    if (!animate) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, clock_labels[index]);
    lv_anim_set_values(&anim, -10, -2);
    lv_anim_set_time(&anim, 180);
    lv_anim_set_exec_cb(&anim, obj_y_cb);
    lv_anim_start(&anim);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, clock_labels[index]);
    lv_anim_set_values(&anim, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_time(&anim, 180);
    lv_anim_set_exec_cb(&anim, obj_opa_cb);
    lv_anim_start(&anim);
}

static void render_big_time(int hour, int minute, bool animate)
{
    const uint8_t digits[4] = {
        (uint8_t)((hour / 10) % 10),
        (uint8_t)(hour % 10),
        (uint8_t)((minute / 10) % 10),
        (uint8_t)(minute % 10),
    };
    for (uint8_t i = 0; i < 4; ++i) {
        flip_digit(i, digits[i], animate);
    }
}

static void create_big_time(lv_obj_t *parent)
{
    time_group = lv_obj_create(parent);
    lv_obj_remove_style_all(time_group);
    lv_obj_set_size(time_group, 254, 154);
    lv_obj_align(time_group, LV_ALIGN_TOP_LEFT, 20, 18);
    lv_obj_clear_flag(time_group, LV_OBJ_FLAG_SCROLLABLE);
    add_gesture_bubble(time_group);

    const int16_t x[4] = {0, 54, 132, 186};
    for (uint8_t i = 0; i < 4; ++i) {
        clock_cards[i] = create_clock_card(time_group, x[i], i < 2 ? COLOR_CREAM : COLOR_GOLD);
        clock_labels[i] = lv_obj_get_child(clock_cards[i], 2);
    }

    colon_label = label_en(time_group, ":", &style_en);
    lv_obj_set_style_text_font(colon_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(colon_label, COLOR_CREAM, 0);
    lv_obj_align(colon_label, LV_ALIGN_TOP_LEFT, 111, 54);
    lv_timer_create(colon_timer_cb, 500, NULL);

    render_big_time(14, 28, false);
}

static void create_main_page(lv_obj_t *root)
{
    main_page = lv_obj_create(root);
    lv_obj_add_style(main_page, &style_screen, 0);
    lv_obj_set_size(main_page, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(main_page, LV_OBJ_FLAG_SCROLLABLE);
    add_gesture_bubble(main_page);

    lv_obj_t *brand_a = label_en(main_page, "nothing", &style_en);
    lv_obj_set_style_text_font(brand_a, &lv_font_montserrat_20, 0);
    lv_obj_align(brand_a, LV_ALIGN_TOP_LEFT, 20, 10);

    lv_obj_t *brand_b = label_en(main_page, "impossible", &style_gold);
    lv_obj_set_style_text_font(brand_b, &lv_font_montserrat_20, 0);
    lv_obj_align(brand_b, LV_ALIGN_TOP_LEFT, 106, 10);

    create_big_time(main_page);

    date_label = label_en(main_page, "06 / 14     |", &style_en);
    lv_obj_set_style_text_color(date_label, COLOR_CREAM, 0);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_20, 0);
    lv_obj_align(date_label, LV_ALIGN_TOP_LEFT, 52, 174);

    week_label = label_en(main_page, "SAT", &style_green);
    lv_obj_set_style_text_font(week_label, &lv_font_montserrat_20, 0);
    lv_obj_align(week_label, LV_ALIGN_TOP_LEFT, 178, 174);

    create_weather_stage(main_page);

    lv_obj_t *quote_panel = create_panel(main_page, 438, 94, 20, 214);

    lv_obj_t *quote_title = label_en(quote_panel, "Quote", &style_gold);
    lv_obj_align(quote_title, LV_ALIGN_TOP_LEFT, 16, 9);

    quote_label = label_en(quote_panel, "Every sunrise is a fresh chance to begin again.", &style_en);
    lv_obj_set_width(quote_label, 405);
    lv_label_set_long_mode(quote_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(quote_label, &lv_font_montserrat_16, 0);
    lv_obj_align(quote_label, LV_ALIGN_TOP_LEFT, 16, 32);

    network_status_label = label_en(quote_panel, "WiFi setup: join xiaozhi-setup, open 192.168.10.1", &style_muted);
    lv_obj_set_width(network_status_label, 405);
    lv_label_set_long_mode(network_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(network_status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(network_status_label, LV_ALIGN_BOTTOM_LEFT, 16, -7);

    lv_obj_t *menu = create_button(main_page, "Menu", menu_event_cb);
    lv_obj_align(menu, LV_ALIGN_TOP_RIGHT, -18, 10);
}

static void create_app_tile(lv_obj_t *parent, uint8_t index, const AppRow *row)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_add_style(box, &style_row, 0);
    lv_obj_set_size(box, 204, 62);
    const int16_t x = 24 + (index % 2) * 218;
    const int16_t y = 86 + (index / 2) * 72;
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(box, apps_gesture_cb, LV_EVENT_GESTURE, NULL);
    if (index == 0) {
        lv_obj_add_event_cb(box, radio_tile_event_cb, LV_EVENT_CLICKED, NULL);
    }
    add_gesture_bubble(box);

    lv_obj_t *cn = label_en(box, row->cn, &style_en);
    lv_obj_set_style_text_color(cn, row->color, 0);
    lv_obj_set_style_text_font(cn, &lv_font_montserrat_20, 0);
    lv_obj_align(cn, LV_ALIGN_TOP_LEFT, 14, 8);

    lv_obj_t *en = label_en(box, row->en, &style_gold);
    lv_obj_set_style_text_color(en, COLOR_TEXT, 0);
    lv_obj_align(en, LV_ALIGN_TOP_LEFT, 70, 10);

    lv_obj_t *status = label_en(box, row->status, &style_muted);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 70, 34);

    lv_obj_t *arrow = label_en(box, ">", &style_muted);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -14, 0);
}

static void create_apps_page(lv_obj_t *root)
{
    apps_page = lv_obj_create(root);
    lv_obj_add_style(apps_page, &style_screen, 0);
    lv_obj_set_size(apps_page, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(apps_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(apps_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(apps_page, apps_gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *brand = label_en(apps_page, "nothing impossible", &style_en);
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_20, 0);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 18, 10);

    create_status_bar(apps_page);

    lv_obj_t *title = label_en(apps_page, "Apps", &style_en);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 24, 48);

    lv_obj_t *sub = label_en(apps_page, "App Center", &style_muted);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 86, 53);

    lv_obj_t *back = create_button(apps_page, "Back", back_event_cb);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -22, 45);

    for (uint8_t i = 0; i < sizeof(APP_ROWS) / sizeof(APP_ROWS[0]); ++i) {
        create_app_tile(apps_page, i, &APP_ROWS[i]);
    }

    lv_obj_t *hint = label_en(apps_page, "Swipe left: Apps   Swipe right: Home", &style_muted);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

static void create_radio_page(lv_obj_t *root)
{
    radio_page = lv_obj_create(root);
    lv_obj_add_style(radio_page, &style_screen, 0);
    lv_obj_set_size(radio_page, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(radio_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(radio_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(radio_page, radio_gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *brand = label_en(radio_page, "nothing impossible", &style_en);
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_20, 0);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 18, 10);

    create_status_bar(radio_page);

    lv_obj_t *title = label_en(radio_page, "Radio", &style_en);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 24, 50);

    lv_obj_t *sub = label_en(radio_page, "Network FM", &style_muted);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 94, 55);

    lv_obj_t *back = create_button(radio_page, "Back", radio_back_event_cb);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -22, 45);

    lv_obj_t *panel = create_panel(radio_page, 432, 134, 24, 88);
    lv_obj_set_style_bg_color(panel, COLOR_SURFACE_2, 0);

    radio_station_label = label_en(panel, "CNR中国之声", &style_gold);
    lv_obj_set_style_text_font(radio_station_label, &font_radio_cn_18, 0);
    lv_obj_align(radio_station_label, LV_ALIGN_TOP_LEFT, 16, 14);

    radio_state_label = label_en(panel, "Ready", &style_green);
    lv_obj_set_style_text_font(radio_state_label, &lv_font_montserrat_20, 0);
    lv_obj_align(radio_state_label, LV_ALIGN_TOP_LEFT, 16, 52);

    radio_meta_label = label_en(panel, "Tap Play to probe MP3 stream", &style_muted);
    lv_obj_set_width(radio_meta_label, 390);
    lv_label_set_long_mode(radio_meta_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(radio_meta_label, &lv_font_montserrat_14, 0);
    lv_obj_align(radio_meta_label, LV_ALIGN_BOTTOM_LEFT, 16, -14);

    lv_obj_t *prev = create_button(radio_page, "Prev", radio_prev_event_cb);
    lv_obj_align(prev, LV_ALIGN_TOP_LEFT, 24, 238);

    lv_obj_t *play = create_button(radio_page, "Play", radio_play_event_cb);
    lv_obj_align(play, LV_ALIGN_TOP_LEFT, 136, 238);

    lv_obj_t *stop = create_button(radio_page, "Stop", radio_stop_event_cb);
    lv_obj_align(stop, LV_ALIGN_TOP_LEFT, 248, 238);

    lv_obj_t *next = create_button(radio_page, "Next", radio_next_event_cb);
    lv_obj_align(next, LV_ALIGN_TOP_LEFT, 360, 238);

    lv_obj_t *hint = label_en(radio_page, "HTTP stream probe first. Audio output comes next.", &style_muted);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

void app_ui_create(void)
{
    init_styles();

    lv_obj_t *root = lv_scr_act();
    lv_obj_set_style_bg_color(root, COLOR_BG, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    create_main_page(root);
    create_apps_page(root);
    create_radio_page(root);

    touch_label = label_en(root, "", &style_muted);
    lv_obj_add_flag(touch_label, LV_OBJ_FLAG_HIDDEN);
}

void app_ui_touch_update(uint16_t x, uint16_t y, uint8_t point_count)
{
    if (!touch_label) {
        return;
    }

    (void)x;
    (void)y;
    (void)point_count;
}

void app_ui_set_network_status(const char *status)
{
    if (!network_status_label || !status) {
        return;
    }
    if (lvgl_port_lock(0)) {
        lv_label_set_text(network_status_label, status);
        lvgl_port_unlock();
    }
}

void app_ui_set_time(int hour, int minute, int month, int day, const char *weekday)
{
    if (!time_group || !date_label || !week_label) {
        return;
    }
    if (lvgl_port_lock(0)) {
        char date_text[24];
        char time_text[8];
        snprintf(date_text, sizeof(date_text), "%02d / %02d     |", month, day);
        snprintf(time_text, sizeof(time_text), "%02d:%02d", hour, minute);
        render_big_time(hour, minute, true);
        lv_label_set_text(date_label, date_text);
        lv_label_set_text(week_label, weekday ? weekday : "---");
        for (size_t i = 0; i < sizeof(status_bar_time_labels) / sizeof(status_bar_time_labels[0]); ++i) {
            if (status_bar_time_labels[i]) {
                lv_label_set_text(status_bar_time_labels[i], time_text);
            }
        }
        lvgl_port_unlock();
    }
}

void app_ui_set_daily_quote(const char *quote)
{
    if (!quote_label || !quote) {
        return;
    }
    if (lvgl_port_lock(0)) {
        lv_label_set_text(quote_label, quote);
        lvgl_port_unlock();
    }
}

void app_ui_set_radio(const char *station, const char *state, const char *meta)
{
    if (!radio_station_label || !radio_state_label || !radio_meta_label) {
        return;
    }
    if (lvgl_port_lock(0)) {
        lv_label_set_text(radio_station_label, station ? station : "Radio");
        lv_label_set_text(radio_state_label, state ? state : "Ready");
        lv_label_set_text(radio_meta_label, meta ? meta : "Network radio");
        lvgl_port_unlock();
    }
}

void app_ui_set_weather(const char *temperature, const char *summary, int weather_code)
{
    if (!weather_temp_label || !weather_meta_label) {
        return;
    }
    if (lvgl_port_lock(0)) {
        lv_label_set_text(weather_temp_label, temperature ? temperature : "-- C");
        lv_label_set_text(weather_meta_label, summary ? summary : "Weather pending");
        set_weather_scene(summary, weather_code);
        lvgl_port_unlock();
    }
}
