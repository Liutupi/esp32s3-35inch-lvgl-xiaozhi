#include "app_time_weather.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_net.h"
#include "app_ui.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_time_weather";
static const char *WEATHER_URL =
    "http://api.open-meteo.com/v1/forecast?latitude=31.2304&longitude=121.4737&current=temperature_2m,weather_code&timezone=Asia%2FShanghai";

static bool s_sntp_started;

static const char *weekday_name(int wday)
{
    static const char *names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (wday < 0 || wday > 6) {
        return "---";
    }
    return names[wday];
}

static const char *weather_desc(int code)
{
    if (code == 0) return "Sunny";
    if (code == 1 || code == 2 || code == 3) return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 95) return "Storm";
    return "Weather";
}

static void start_sntp_once(void)
{
    if (s_sntp_started) {
        return;
    }
    s_sntp_started = true;
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started");
}

static bool wait_time_ready(void)
{
    time_t now = 0;
    tm timeinfo = {};
    for (int i = 0; i < 30; ++i) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2024 - 1900)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return false;
}

static void update_time_ui(void)
{
    time_t now = 0;
    tm info = {};
    time(&now);
    localtime_r(&now, &info);
    if (info.tm_year < (2024 - 1900)) {
        return;
    }
    app_ui_set_time(info.tm_hour, info.tm_min, info.tm_mon + 1, info.tm_mday, weekday_name(info.tm_wday));
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data && evt->data_len > 0) {
        char *buf = (char *)evt->user_data;
        size_t used = strlen(buf);
        size_t room = 1024 - used - 1;
        size_t copy = evt->data_len < (int)room ? evt->data_len : room;
        memcpy(buf + used, evt->data, copy);
        buf[used + copy] = 0;
    }
    return ESP_OK;
}

static void fetch_weather(void)
{
    char response[1024] = {};
    esp_http_client_config_t config = {};
    config.url = WEATHER_URL;
    config.timeout_ms = 8000;
    config.event_handler = http_event_handler;
    config.user_data = response;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status < 200 || status >= 300 || response[0] == 0) {
        ESP_LOGW(TAG, "weather fetch failed err=%s status=%d", esp_err_to_name(err), status);
        app_ui_set_weather("-- C", "Weather pending");
        return;
    }

    cJSON *root = cJSON_Parse(response);
    cJSON *current = root ? cJSON_GetObjectItem(root, "current") : NULL;
    cJSON *temp = current ? cJSON_GetObjectItem(current, "temperature_2m") : NULL;
    cJSON *code = current ? cJSON_GetObjectItem(current, "weather_code") : NULL;
    if (!cJSON_IsNumber(temp) || !cJSON_IsNumber(code)) {
        cJSON_Delete(root);
        app_ui_set_weather("-- C", "Weather pending");
        return;
    }

    char temp_text[16];
    snprintf(temp_text, sizeof(temp_text), "%d C", (int)lround(temp->valuedouble));
    app_ui_set_weather(temp_text, weather_desc(code->valueint));
    cJSON_Delete(root);
}

static void time_weather_task(void *arg)
{
    (void)arg;
    int weather_ticks = 3600;
    while (true) {
        if (!app_net_wait_connected(30000)) {
            app_ui_set_network_status("Waiting for WiFi. Setup AP: xiaozhi-setup");
            continue;
        }

        start_sntp_once();
        if (wait_time_ready()) {
            update_time_ui();
        }

        if (weather_ticks >= 3600) {
            fetch_weather();
            weather_ticks = 0;
        }

        for (int i = 0; i < 60; ++i) {
            if (!app_net_is_connected()) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            ++weather_ticks;
        }
        update_time_ui();
    }
}

void app_time_weather_start(void)
{
    xTaskCreate(time_weather_task, "time_weather", 6144, NULL, 4, NULL);
}
