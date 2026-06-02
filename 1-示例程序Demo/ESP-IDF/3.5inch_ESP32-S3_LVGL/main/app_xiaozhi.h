#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_XIAOZHI_STATE_IDLE = 0,
    APP_XIAOZHI_STATE_CONFIGURING,
    APP_XIAOZHI_STATE_CONNECTING,
    APP_XIAOZHI_STATE_LISTENING,
    APP_XIAOZHI_STATE_SPEAKING,
    APP_XIAOZHI_STATE_ERROR,
} app_xiaozhi_state_t;

void app_xiaozhi_start(void);
void app_xiaozhi_toggle(void);
void app_xiaozhi_stop_session(void);
bool app_xiaozhi_is_active(void);
app_xiaozhi_state_t app_xiaozhi_state(void);

#ifdef __cplusplus
}
#endif
