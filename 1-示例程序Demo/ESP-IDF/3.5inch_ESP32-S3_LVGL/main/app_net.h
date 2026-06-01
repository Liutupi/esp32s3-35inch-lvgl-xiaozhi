#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_net_start(void);
bool app_net_is_connected(void);
bool app_net_wait_connected(uint32_t timeout_ms);
esp_err_t app_net_save_credentials(const char *ssid, const char *password);
const char *app_net_portal_ssid(void);

#ifdef __cplusplus
}
#endif
