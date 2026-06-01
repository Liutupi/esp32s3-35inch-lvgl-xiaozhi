#include "app_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lv_port.h"

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_48);

static const char *TAG = "app_ui";

static lv_obj_t *main_page;
static lv_obj_t *apps_page;
static lv_obj_t *touch_label;
static lv_obj_t *time_group;
static lv_obj_t *date_label;
static lv_obj_t *week_label;
static lv_obj_t *weather_temp_label;
static lv_obj_t *weather_meta_label;
static lv_obj_t *quote_label;

static lv_style_t style_screen;
static lv_style_t style_en;
static lv_style_t style_muted;
static lv_style_t style_gold;
static lv_style_t style_green;
static lv_style_t style_panel;
static lv_style_t style_row;

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
    ESP_LOGI(TAG, "show main");
}

static void show_apps(void)
{
    lv_obj_clear_flag(apps_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(main_page, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "show apps");
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

static void pulse_opa_cb(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, value, 0);
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
}

static void create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *wifi = label_en(parent, "WiFi", &style_green);
    lv_obj_align(wifi, LV_ALIGN_TOP_RIGHT, -168, 12);

    lv_obj_t *time = label_en(parent, "14:28", &style_en);
    lv_obj_set_style_text_font(time, &lv_font_montserrat_20, 0);
    lv_obj_align(time, LV_ALIGN_TOP_RIGHT, -86, 9);

    lv_obj_t *battery = label_en(parent, "80%", &style_green);
    lv_obj_align(battery, LV_ALIGN_TOP_RIGHT, -20, 12);
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 86, 34);
    lv_obj_set_style_radius(btn, 17, 0);
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

static void create_weather_stage(lv_obj_t *parent)
{
    lv_obj_t *weather_panel = create_panel(parent, 168, 138, 292, 58);
    lv_obj_set_style_bg_color(weather_panel, COLOR_SURFACE_2, 0);

    lv_obj_t *title = label_en(weather_panel, "Weather", &style_muted);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 10);

    lv_obj_t *glow = lv_obj_create(weather_panel);
    lv_obj_remove_style_all(glow);
    lv_obj_set_size(glow, 82, 82);
    lv_obj_set_style_radius(glow, 41, 0);
    lv_obj_set_style_bg_color(glow, COLOR_GOLD, 0);
    lv_obj_set_style_bg_opa(glow, LV_OPA_20, 0);
    lv_obj_align(glow, LV_ALIGN_TOP_MID, 0, 34);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, glow);
    lv_anim_set_values(&anim, LV_OPA_10, LV_OPA_40);
    lv_anim_set_time(&anim, 1500);
    lv_anim_set_playback_time(&anim, 1500);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&anim, pulse_opa_cb);
    lv_anim_start(&anim);

    lv_obj_t *sun = lv_obj_create(weather_panel);
    lv_obj_remove_style_all(sun);
    lv_obj_set_size(sun, 50, 50);
    lv_obj_set_style_radius(sun, 25, 0);
    lv_obj_set_style_bg_color(sun, COLOR_GOLD, 0);
    lv_obj_set_style_bg_opa(sun, LV_OPA_COVER, 0);
    lv_obj_align(sun, LV_ALIGN_TOP_MID, 0, 50);

    weather_temp_label = label_en(weather_panel, "-- C", &style_en);
    lv_obj_set_style_text_font(weather_temp_label, &lv_font_montserrat_20, 0);
    lv_obj_align(weather_temp_label, LV_ALIGN_BOTTOM_LEFT, 14, -30);

    weather_meta_label = label_en(weather_panel, "Weather pending", &style_green);
    lv_obj_set_style_text_font(weather_meta_label, &lv_font_montserrat_12, 0);
    lv_obj_align(weather_meta_label, LV_ALIGN_BOTTOM_LEFT, 14, -12);
}

static lv_obj_t *create_segment(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h, lv_color_t color)
{
    lv_obj_t *seg = lv_obj_create(parent);
    lv_obj_remove_style_all(seg);
    lv_obj_set_size(seg, w, h);
    lv_obj_set_style_bg_color(seg, color, 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(seg, h > w ? w / 2 : h / 2, 0);
    lv_obj_align(seg, LV_ALIGN_TOP_LEFT, x, y);
    add_gesture_bubble(seg);
    return seg;
}

static void draw_digit(lv_obj_t *parent, uint8_t digit, int16_t x, int16_t y, lv_color_t color)
{
    static const uint8_t masks[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66,
        0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };

    const int16_t w = 44;
    const int16_t h = 82;
    const int16_t t = 7;
    const uint8_t mask = masks[digit % 10];

    if (mask & 0x01) create_segment(parent, x + t, y, w - 2 * t, t, color);
    if (mask & 0x02) create_segment(parent, x + w - t, y + t, t, h / 2 - t, color);
    if (mask & 0x04) create_segment(parent, x + w - t, y + h / 2, t, h / 2 - t, color);
    if (mask & 0x08) create_segment(parent, x + t, y + h - t, w - 2 * t, t, color);
    if (mask & 0x10) create_segment(parent, x, y + h / 2, t, h / 2 - t, color);
    if (mask & 0x20) create_segment(parent, x, y + t, t, h / 2 - t, color);
    if (mask & 0x40) create_segment(parent, x + t, y + h / 2 - t / 2, w - 2 * t, t, color);
}

static void draw_colon(lv_obj_t *parent, int16_t x, int16_t y, lv_color_t color)
{
    create_segment(parent, x, y + 23, 9, 9, color);
    create_segment(parent, x, y + 53, 9, 9, color);
}

static void render_big_time(int hour, int minute)
{
    if (!time_group) {
        return;
    }
    lv_obj_clean(time_group);
    const int16_t y = 68;
    draw_digit(time_group, (hour / 10) % 10, 0, y, COLOR_CREAM);
    draw_digit(time_group, hour % 10, 52, y, COLOR_CREAM);
    draw_colon(time_group, 106, y, COLOR_CREAM);
    draw_digit(time_group, (minute / 10) % 10, 130, y, COLOR_GOLD);
    draw_digit(time_group, minute % 10, 182, y, COLOR_GOLD);
}

static void create_big_time(lv_obj_t *parent)
{
    time_group = lv_obj_create(parent);
    lv_obj_remove_style_all(time_group);
    lv_obj_set_size(time_group, 238, 162);
    lv_obj_align(time_group, LV_ALIGN_TOP_LEFT, 20, 0);
    lv_obj_clear_flag(time_group, LV_OBJ_FLAG_SCROLLABLE);
    add_gesture_bubble(time_group);
    render_big_time(14, 28);
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
    lv_obj_align(brand_a, LV_ALIGN_TOP_LEFT, 18, 10);

    lv_obj_t *brand_b = label_en(main_page, "impossible", &style_gold);
    lv_obj_set_style_text_font(brand_b, &lv_font_montserrat_20, 0);
    lv_obj_align(brand_b, LV_ALIGN_TOP_LEFT, 104, 10);

    create_big_time(main_page);

    date_label = label_en(main_page, "06 / 14     |", &style_en);
    lv_obj_set_style_text_color(date_label, COLOR_CREAM, 0);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_20, 0);
    lv_obj_align(date_label, LV_ALIGN_TOP_LEFT, 52, 164);

    week_label = label_en(main_page, "SAT", &style_green);
    lv_obj_set_style_text_font(week_label, &lv_font_montserrat_20, 0);
    lv_obj_align(week_label, LV_ALIGN_TOP_LEFT, 178, 164);

    create_weather_stage(main_page);

    lv_obj_t *quote_panel = create_panel(main_page, 438, 104, 20, 206);

    lv_obj_t *quote_title = label_en(quote_panel, "Quote", &style_gold);
    lv_obj_align(quote_title, LV_ALIGN_TOP_LEFT, 16, 10);

    quote_label = label_en(quote_panel, "WiFi setup: join xiaozhi-setup, open 192.168.4.1", &style_en);
    lv_obj_set_width(quote_label, 405);
    lv_label_set_long_mode(quote_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(quote_label, &lv_font_montserrat_16, 0);
    lv_obj_align(quote_label, LV_ALIGN_TOP_LEFT, 16, 34);

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

void app_ui_create(void)
{
    init_styles();

    lv_obj_t *root = lv_scr_act();
    lv_obj_set_style_bg_color(root, COLOR_BG, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    create_main_page(root);
    create_apps_page(root);

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
    if (!quote_label || !status) {
        return;
    }
    if (lvgl_port_lock(0)) {
        lv_label_set_text(quote_label, status);
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
        snprintf(date_text, sizeof(date_text), "%02d / %02d     |", month, day);
        render_big_time(hour, minute);
        lv_label_set_text(date_label, date_text);
        lv_label_set_text(week_label, weekday ? weekday : "---");
        lvgl_port_unlock();
    }
}

void app_ui_set_weather(const char *temperature, const char *summary)
{
    if (!weather_temp_label || !weather_meta_label) {
        return;
    }
    if (lvgl_port_lock(0)) {
        lv_label_set_text(weather_temp_label, temperature ? temperature : "-- C");
        lv_label_set_text(weather_meta_label, summary ? summary : "Weather pending");
        lvgl_port_unlock();
    }
}
