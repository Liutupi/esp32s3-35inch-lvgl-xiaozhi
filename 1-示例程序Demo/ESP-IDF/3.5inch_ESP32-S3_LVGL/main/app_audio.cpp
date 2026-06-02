#include "app_audio.h"

#include "bsp_i2c.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "app_audio";

static constexpr gpio_num_t PIN_AUDIO_PA_EN = GPIO_NUM_1;   // Low level enables amplifier
static constexpr gpio_num_t PIN_I2S_DOUT = GPIO_NUM_15;     // ESP32 data to codec DAC
static constexpr gpio_num_t PIN_I2S_DIN = GPIO_NUM_16;      // Codec ADC data to ESP32
static constexpr gpio_num_t PIN_I2S_MCLK = GPIO_NUM_17;
static constexpr gpio_num_t PIN_I2S_BCLK = GPIO_NUM_18;
static constexpr gpio_num_t PIN_I2S_LRCK = GPIO_NUM_21;
static constexpr uint32_t AUDIO_SAMPLE_RATE_HZ = 44100;

static i2s_chan_handle_t s_tx_chan;
static i2s_chan_handle_t s_rx_chan;
static esp_codec_dev_handle_t s_codec_dev;
static bool s_audio_started;
static bool s_tx_enabled;
static bool s_rx_enabled;
static bool s_codec_seen;
static bool s_codec_open;
static uint32_t s_sample_rate_hz = AUDIO_SAMPLE_RATE_HZ;
static volatile bool s_stop_requested;
static SemaphoreHandle_t s_audio_mutex;
static app_audio_owner_t s_audio_owner = APP_AUDIO_OWNER_NONE;
static bool s_rx_needed;

void app_audio_set_amp_enabled(bool enabled)
{
    gpio_set_level(PIN_AUDIO_PA_EN, enabled ? 0 : 1);
}

static void init_amp_gpio(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << PIN_AUDIO_PA_EN;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    app_audio_set_amp_enabled(false);
}

static void probe_codec(i2c_master_bus_handle_t i2c_bus)
{
    if (!i2c_bus) {
        ESP_LOGW(TAG, "skip codec probe: no i2c bus");
        return;
    }

    static const uint8_t candidates[] = {0x18, 0x19};
    if (!bsp_i2c_lock(200)) {
        ESP_LOGW(TAG, "codec probe skipped: i2c bus busy");
        return;
    }

    for (uint8_t addr : candidates) {
        esp_err_t err = i2c_master_probe(i2c_bus, addr, 100);
        if (err == ESP_OK) {
            s_codec_seen = true;
            ESP_LOGI(TAG, "ES8311 candidate responded at 0x%02x", addr);
        }
    }
    bsp_i2c_unlock();

    if (!s_codec_seen) {
        ESP_LOGW(TAG, "ES8311 not found at 0x18/0x19 yet");
    }
}

static esp_err_t init_i2s_duplex(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_sample_rate_hz);
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    std_cfg.gpio_cfg.mclk = PIN_I2S_MCLK;
    std_cfg.gpio_cfg.bclk = PIN_I2S_BCLK;
    std_cfg.gpio_cfg.ws = PIN_I2S_LRCK;
    std_cfg.gpio_cfg.dout = PIN_I2S_DOUT;
    std_cfg.gpio_cfg.din = PIN_I2S_DIN;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv = false;

    err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s TX init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s RX init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
        return err;
    }
    s_tx_enabled = true;
    /* RX stays disabled at boot to avoid mic noise leaking to output.
       It will be enabled on demand when XiaoZhi acquires audio. */

    ESP_LOGI(TAG, "I2S ready mclk=%d bclk=%d lrck=%d dout=%d din=%d slot=32 (RX deferred)",
             PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_DIN);
    return ESP_OK;
}

static esp_err_t open_es8311(i2c_master_bus_handle_t i2c_bus)
{
    if (!i2c_bus || !s_tx_chan || !s_rx_chan || !s_codec_seen) {
        return ESP_ERR_INVALID_STATE;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {};
    i2c_cfg.port = I2C_PORT_NUM;
    i2c_cfg.addr = ES8311_CODEC_DEFAULT_ADDR;
    i2c_cfg.bus_handle = i2c_bus;
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) {
        ESP_LOGE(TAG, "create codec i2c ctrl failed");
        return ESP_FAIL;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {};
    i2s_cfg.port = I2S_NUM_0;
    i2s_cfg.tx_handle = s_tx_chan;
    i2s_cfg.rx_handle = s_rx_chan;
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) {
        ESP_LOGE(TAG, "create codec i2s data failed");
        return ESP_FAIL;
    }

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!gpio_if) {
        ESP_LOGE(TAG, "create codec gpio failed");
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = ctrl_if;
    es8311_cfg.gpio_if = gpio_if;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.master_mode = false;
    es8311_cfg.use_mclk = true;
    es8311_cfg.pa_pin = PIN_AUDIO_PA_EN;
    es8311_cfg.pa_reverted = true;
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    es8311_cfg.mclk_div = 256;
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    if (!codec_if) {
        ESP_LOGE(TAG, "create es8311 codec failed");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {};
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT;
    dev_cfg.codec_if = codec_if;
    dev_cfg.data_if = data_if;
    s_codec_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_codec_dev) {
        ESP_LOGE(TAG, "create codec device failed");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t sample_cfg = {};
    sample_cfg.bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT;
    sample_cfg.channel = 2;
    sample_cfg.channel_mask = 0x03;
    sample_cfg.sample_rate = s_sample_rate_hz;
    if (esp_codec_dev_open(s_codec_dev, &sample_cfg) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "open ES8311 failed");
        return ESP_FAIL;
    }

    if (esp_codec_dev_set_out_vol(s_codec_dev, 75) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "set ES8311 volume failed");
    }
    if (esp_codec_dev_set_out_mute(s_codec_dev, false) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "unmute ES8311 failed");
    }
    if (esp_codec_dev_set_in_gain(s_codec_dev, 30.0) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "set ES8311 mic gain failed");
    }
    app_audio_set_amp_enabled(true);

    s_codec_open = true;
    ESP_LOGI(TAG, "ES8311 codec opened, volume=75, mic_gain=30, sample_rate=%lu, 16-bit duplex, unmuted, amp enabled",
             (unsigned long)s_sample_rate_hz);
    return ESP_OK;
}

esp_err_t app_audio_set_sample_rate(uint32_t sample_rate_hz)
{
    if (!s_codec_dev || !s_codec_open || sample_rate_hz < 8000 || sample_rate_hz > 96000) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sample_rate_hz == s_sample_rate_hz) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "switch audio sample rate %lu -> %lu",
             (unsigned long)s_sample_rate_hz, (unsigned long)sample_rate_hz);
    if (esp_codec_dev_close(s_codec_dev) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "close codec before sample-rate switch failed");
    }

    s_sample_rate_hz = sample_rate_hz;
    if (s_tx_enabled) {
        esp_err_t disable_err = i2s_channel_disable(s_tx_chan);
        if (disable_err != ESP_OK && disable_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "disable TX before sample-rate switch failed: %s", esp_err_to_name(disable_err));
        }
        s_tx_enabled = false;
    }
    if (s_rx_enabled) {
        esp_err_t disable_err = i2s_channel_disable(s_rx_chan);
        if (disable_err != ESP_OK && disable_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "disable RX before sample-rate switch failed: %s", esp_err_to_name(disable_err));
        }
        s_rx_enabled = false;
    }
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_sample_rate_hz);
    esp_err_t err = i2s_channel_reconfig_std_clock(s_tx_chan, &clk_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reconfig TX clock failed: %s", esp_err_to_name(err));
        s_codec_open = false;
        return err;
    }
    err = i2s_channel_reconfig_std_clock(s_rx_chan, &clk_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reconfig RX clock failed: %s", esp_err_to_name(err));
        s_codec_open = false;
        return err;
    }
    err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reenable TX failed: %s", esp_err_to_name(err));
        s_codec_open = false;
        return err;
    }
    s_tx_enabled = true;
    if (s_rx_needed) {
        err = i2s_channel_enable(s_rx_chan);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "reenable RX failed: %s", esp_err_to_name(err));
            s_codec_open = false;
            return err;
        }
        s_rx_enabled = true;
    }

    esp_codec_dev_sample_info_t sample_cfg = {};
    sample_cfg.bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT;
    sample_cfg.channel = 2;
    sample_cfg.channel_mask = 0x03;
    sample_cfg.sample_rate = s_sample_rate_hz;
    if (esp_codec_dev_open(s_codec_dev, &sample_cfg) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "reopen codec at %lu Hz failed", (unsigned long)s_sample_rate_hz);
        s_codec_open = false;
        return ESP_FAIL;
    }
    if (esp_codec_dev_set_out_vol(s_codec_dev, 75) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "restore ES8311 volume failed");
    }
    if (esp_codec_dev_set_out_mute(s_codec_dev, false) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "restore ES8311 unmute failed");
    }
    app_audio_set_amp_enabled(true);
    s_codec_open = true;
    return ESP_OK;
}

bool app_audio_acquire(app_audio_owner_t owner, uint32_t sample_rate_hz)
{
    if (owner == APP_AUDIO_OWNER_NONE || !app_audio_is_ready() || !s_audio_mutex) {
        return false;
    }
    if (xSemaphoreTake(s_audio_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    if (s_audio_owner != APP_AUDIO_OWNER_NONE && s_audio_owner != owner) {
        xSemaphoreGive(s_audio_mutex);
        return false;
    }
    /* Enable RX for owners that need the microphone (XiaoZhi) */
    if (owner == APP_AUDIO_OWNER_XIAOZHI) {
        s_rx_needed = true;
        if (!s_rx_enabled && s_rx_chan) {
            if (i2s_channel_enable(s_rx_chan) == ESP_OK) {
                s_rx_enabled = true;
                ESP_LOGI(TAG, "I2S RX enabled for mic capture");
            }
        }
    }
    if (app_audio_set_sample_rate(sample_rate_hz) != ESP_OK) {
        xSemaphoreGive(s_audio_mutex);
        return false;
    }
    s_audio_owner = owner;
    xSemaphoreGive(s_audio_mutex);
    return true;
}

void app_audio_release(app_audio_owner_t owner)
{
    if (!s_audio_mutex) {
        return;
    }
    if (xSemaphoreTake(s_audio_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (s_audio_owner == owner) {
            s_audio_owner = APP_AUDIO_OWNER_NONE;
            if (owner == APP_AUDIO_OWNER_XIAOZHI) {
                app_audio_set_sample_rate(AUDIO_SAMPLE_RATE_HZ);
                /* Disable RX to stop mic DMA and eliminate noise */
                s_rx_needed = false;
                if (s_rx_enabled && s_rx_chan) {
                    i2s_channel_disable(s_rx_chan);
                    s_rx_enabled = false;
                    ESP_LOGI(TAG, "I2S RX disabled after XiaoZhi release");
                }
            }
        }
        xSemaphoreGive(s_audio_mutex);
    }
}

app_audio_owner_t app_audio_owner(void)
{
    return s_audio_owner;
}

void app_audio_start(i2c_master_bus_handle_t i2c_bus)
{
    if (s_audio_started) {
        return;
    }
    s_audio_started = true;
    s_audio_mutex = xSemaphoreCreateMutex();

    init_amp_gpio();
    probe_codec(i2c_bus);
    if (init_i2s_duplex() == ESP_OK) {
        esp_err_t err = open_es8311(i2c_bus);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "audio foundation ready");
        } else {
            app_audio_set_amp_enabled(false);
            ESP_LOGW(TAG, "audio foundation ready without codec open: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t app_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_codec_dev || !s_codec_open || !samples || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_stop_requested) {
        return ESP_ERR_INVALID_STATE;
    }

    // Write in smaller chunks to allow stop checking
    static const size_t CHUNK_SIZE = 512; // samples per chunk
    size_t remaining = sample_count;
    const int16_t *ptr = samples;

    while (remaining > 0) {
        if (s_stop_requested) {
            return ESP_ERR_INVALID_STATE;
        }

        size_t to_write = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
        const int ret = esp_codec_dev_write(s_codec_dev, (void *)ptr, to_write * sizeof(int16_t));
        if (ret != ESP_CODEC_DEV_OK) {
            return ESP_FAIL;
        }

        ptr += to_write;
        remaining -= to_write;
    }

    return ESP_OK;
}

esp_err_t app_audio_read_mono(int16_t *samples, size_t frame_count, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_codec_dev || !s_codec_open || !samples || frame_count == 0 || !s_rx_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    static int16_t stereo_buf[512 * 2];
    size_t remaining = frame_count;
    int16_t *out = samples;
    while (remaining > 0) {
        const size_t frames = remaining > 512 ? 512 : remaining;
        const int ret = esp_codec_dev_read(s_codec_dev, stereo_buf, frames * 2 * sizeof(int16_t));
        if (ret != ESP_CODEC_DEV_OK) {
            return ESP_FAIL;
        }
        for (size_t i = 0; i < frames; ++i) {
            out[i] = stereo_buf[i * 2];
        }
        out += frames;
        remaining -= frames;
    }
    return ESP_OK;
}

esp_err_t app_audio_play_test_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
    if (!app_audio_is_ready() || frequency_hz == 0 || duration_ms == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    static constexpr size_t FRAMES_PER_CHUNK = 256;
    static constexpr int16_t AMPLITUDE = 9000;
    int16_t samples[FRAMES_PER_CHUNK * 2];
    uint32_t phase = 0;
    const uint32_t phase_step = (frequency_hz << 16) / s_sample_rate_hz;
    uint32_t frames_left = (s_sample_rate_hz * duration_ms) / 1000;

    ESP_LOGI(TAG, "play test tone %lu Hz %lu ms", (unsigned long)frequency_hz, (unsigned long)duration_ms);
    while (frames_left > 0) {
        const uint32_t frames = frames_left > FRAMES_PER_CHUNK ? FRAMES_PER_CHUNK : frames_left;
        for (uint32_t i = 0; i < frames; ++i) {
            const int16_t sample = (phase & 0x8000) ? AMPLITUDE : -AMPLITUDE;
            samples[i * 2] = sample;
            samples[i * 2 + 1] = sample;
            phase += phase_step;
        }

        esp_err_t err = app_audio_write_pcm(samples, frames * 2, 200);
        if (err != ESP_OK) {
            return err;
        }
        frames_left -= frames;
        vTaskDelay(1);
    }
    return ESP_OK;
}

bool app_audio_is_ready(void)
{
    return s_audio_started && s_tx_chan != NULL && s_rx_chan != NULL && s_codec_seen && s_codec_open;
}

void app_audio_set_stop_requested(bool stop)
{
    s_stop_requested = stop;
}

bool app_audio_is_stop_requested(void)
{
    return s_stop_requested;
}

void app_audio_mute_output(void)
{
    if (s_codec_dev && s_codec_open) {
        esp_codec_dev_set_out_mute(s_codec_dev, true);
    }
    app_audio_set_amp_enabled(false);
}

void app_audio_unmute_output(void)
{
    if (s_codec_dev && s_codec_open) {
        esp_codec_dev_set_out_mute(s_codec_dev, false);
        esp_codec_dev_set_out_vol(s_codec_dev, 75);
    }
    app_audio_set_amp_enabled(true);
}
