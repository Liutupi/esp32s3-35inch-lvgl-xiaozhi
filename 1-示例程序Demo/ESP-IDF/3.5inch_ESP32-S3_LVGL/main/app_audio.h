#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_audio_start(i2c_master_bus_handle_t i2c_bus);
esp_err_t app_audio_set_sample_rate(uint32_t sample_rate_hz);
esp_err_t app_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms);
esp_err_t app_audio_play_test_tone(uint32_t frequency_hz, uint32_t duration_ms);
void app_audio_set_amp_enabled(bool enabled);
bool app_audio_is_ready(void);

#ifdef __cplusplus
}
#endif
