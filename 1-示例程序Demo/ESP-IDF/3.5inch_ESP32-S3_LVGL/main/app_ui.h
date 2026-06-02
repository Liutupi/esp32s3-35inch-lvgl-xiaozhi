#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_ui_create(void);
void app_ui_touch_update(uint16_t x, uint16_t y, uint8_t point_count);
void app_ui_handle_swipe(int16_t dx, int16_t dy);
void app_ui_set_network_status(const char *status);
void app_ui_set_time(int hour, int minute, int month, int day, const char *weekday);
void app_ui_set_daily_quote(const char *quote);
void app_ui_set_weather(const char *temperature, const char *summary, int weather_code);
void app_ui_set_radio(const char *station, const char *state, const char *meta);

#ifdef __cplusplus
}
#endif
