#include "app_radio.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "app_audio.h"
#include "app_net.h"
#include "app_ui.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "mp3dec.h"

static const char *TAG = "app_radio";

static constexpr int RADIO_MP3_READ_BUF_SIZE = 16 * 1024;
static constexpr int RADIO_MP3_READ_TARGET = 8 * 1024;
static constexpr int RADIO_MP3_READ_CHUNK = 1024;
static constexpr int RADIO_MP3_REFILL_THRESHOLD = MAINBUF_SIZE;
static constexpr int RADIO_STREAM_BUFFER_SIZE = 24 * 1024;
static constexpr int RADIO_STREAM_START_BYTES = 8 * 1024;
static constexpr int RADIO_PCM_MAX_SAMPLES = MAX_NCHAN * MAX_NGRAN * MAX_NSAMP;

enum RadioCommand {
    RADIO_CMD_PLAY_PAUSE,
    RADIO_CMD_STOP,
    RADIO_CMD_NEXT,
    RADIO_CMD_PREV,
};

struct RadioStation {
    const char *name;
    const char *urls[3];
    const char *codec;
    int bitrate_kbps;
};

struct RadioReaderContext {
    esp_http_client_handle_t client;
    StreamBufferHandle_t stream;
    volatile bool stop;
    volatile bool failed;
    volatile bool done;
    volatile size_t total_bytes;
};

static const RadioStation RADIO_STATIONS[] = {
    {"CNR中国之声", {"https://lhttp.qtfm.cn/live/15318317/64k.mp3", "https://lhttp-hw.qtfm.cn/live/15318317/64k.mp3", NULL}, "MP3", 64},
    {"广州新闻资讯", {"http://lhttp.qingting.fm/live/4848/64k.mp3", "https://lhttp.qtfm.cn/live/4848/64k.mp3", NULL}, "MP3", 64},
    {"广州交通经济", {"http://lhttp.qingting.fm/live/4955/64k.mp3", "https://lhttp.qtfm.cn/live/4955/64k.mp3", NULL}, "MP3", 64},
    {"珠江经济电台", {"http://lhttp.qingting.fm/live/1259/64k.mp3", "https://lhttp.qtfm.cn/live/1259/64k.mp3", NULL}, "MP3", 64},
    {"广东音乐之声", {"http://lhttp.qingting.fm/live/1260/64k.mp3", "https://lhttp.qtfm.cn/live/1260/64k.mp3", NULL}, "MP3", 64},
    {"广东文体广播", {"https://lhttp.qtfm.cn/live/471/64k.mp3", "https://lhttp-hw.qtfm.cn/live/471/64k.mp3", NULL}, "MP3", 64},
    {"深圳飞扬971", {"http://lhttp.qingting.fm/live/1271/64k.mp3", "https://lhttp.qtfm.cn/live/1271/64k.mp3", NULL}, "MP3", 64},
    {"亚洲粤语", {"https://lhttp.qingting.fm/live/15318569/64k.mp3", "https://lhttp.qtfm.cn/live/15318569/64k.mp3", NULL}, "MP3", 64},
    {"500首华语经典", {"https://lhttp.qtfm.cn/live/5022308/64k.mp3", "https://lhttp-hw.qtfm.cn/live/5022308/64k.mp3", "http://lhttp.qingting.fm/live/5022308/64k.mp3"}, "MP3", 64},
    {"清晨音乐台", {"http://lhttp.qingting.fm/live/4915/64k.mp3", "https://lhttp.qtfm.cn/live/4915/64k.mp3", NULL}, "MP3", 64},
    {"怀旧好声音", {"http://lhttp.qingting.fm/live/1223/64k.mp3", "https://lhttp.qtfm.cn/live/1223/64k.mp3", NULL}, "MP3", 64},
    {"动听音乐台", {"http://lhttp.qingting.fm/live/5022107/64k.mp3", "https://lhttp.qtfm.cn/live/5022107/64k.mp3", NULL}, "MP3", 64},
    {"江苏经典流行音乐", {"http://lhttp.qingting.fm/live/4938/64k.mp3", "https://lhttp.qtfm.cn/live/4938/64k.mp3", NULL}, "MP3", 64},
};

static QueueHandle_t s_radio_queue;
static bool s_radio_started;
static bool s_play_requested;
static int s_station_index;

static void post_command(RadioCommand command)
{
    if (!s_radio_queue) {
        return;
    }
    xQueueSend(s_radio_queue, &command, 0);
}

static void set_station_ui(const char *state, const char *detail)
{
    const RadioStation *station = &RADIO_STATIONS[s_station_index];
    char meta[96];
    snprintf(meta, sizeof(meta), "%s %d kbps  %s", station->codec, station->bitrate_kbps, detail ? detail : "");
    app_ui_set_radio(station->name, state, meta);
}

static void next_station(int delta)
{
    const int count = sizeof(RADIO_STATIONS) / sizeof(RADIO_STATIONS[0]);
    s_station_index = (s_station_index + delta + count) % count;
}

static bool consume_command(RadioCommand *command)
{
    return s_radio_queue && xQueueReceive(s_radio_queue, command, 0) == pdTRUE;
}

static void handle_command(RadioCommand command)
{
    switch (command) {
    case RADIO_CMD_PLAY_PAUSE:
        s_play_requested = !s_play_requested;
        set_station_ui(s_play_requested ? "Connecting" : "Paused", s_play_requested ? "Opening stream" : "Stopped");
        break;
    case RADIO_CMD_STOP:
        s_play_requested = false;
        set_station_ui("Stopped", "Ready");
        break;
    case RADIO_CMD_NEXT:
        next_station(1);
        s_play_requested = true;
        set_station_ui("Connecting", "Next station");
        break;
    case RADIO_CMD_PREV:
        next_station(-1);
        s_play_requested = true;
        set_station_ui("Connecting", "Previous station");
        break;
    default:
        break;
    }
}

static bool check_interrupt_commands(void)
{
    RadioCommand command;
    bool interrupted = false;
    while (consume_command(&command)) {
        interrupted = true;
        handle_command(command);
    }
    return interrupted;
}

static void radio_reader_task(void *arg)
{
    RadioReaderContext *ctx = (RadioReaderContext *)arg;
    uint8_t chunk[RADIO_MP3_READ_CHUNK];

    while (!ctx->stop && app_net_is_connected()) {
        int read = esp_http_client_read(ctx->client, (char *)chunk, sizeof(chunk));
        if (read > 0) {
            ctx->total_bytes += read;
            size_t sent = 0;
            while (sent < (size_t)read && !ctx->stop) {
                size_t wrote = xStreamBufferSend(ctx->stream, chunk + sent, read - sent, pdMS_TO_TICKS(120));
                if (wrote == 0) {
                    continue;
                }
                sent += wrote;
            }
            continue;
        }

        if (read == 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        ESP_LOGW(TAG, "stream reader failed read=%d", read);
        ctx->failed = true;
        break;
    }

    ctx->done = true;
    vTaskDelete(NULL);
}

static bool wait_start_buffer(RadioReaderContext *ctx)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(6000);
    while (s_play_requested && !ctx->failed && app_net_is_connected()) {
        if (check_interrupt_commands()) {
            return false;
        }
        size_t available = xStreamBufferBytesAvailable(ctx->stream);
        if (available >= RADIO_STREAM_START_BYTES) {
            return true;
        }
        char detail[48];
        snprintf(detail, sizeof(detail), "%u/%u KB",
                 (unsigned)(available / 1024), (unsigned)(RADIO_STREAM_START_BYTES / 1024));
        set_station_ui("Buffering", detail);
        if ((int32_t)(xTaskGetTickCount() - deadline) > 0 && available >= MAINBUF_SIZE * 2) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

static bool fill_stream_buffer(RadioReaderContext *ctx, uint8_t *buffer, uint8_t **read_ptr, int *bytes_left)
{
    if (*read_ptr != buffer && *bytes_left > 0) {
        memmove(buffer, *read_ptr, *bytes_left);
    }
    *read_ptr = buffer;

    while (*bytes_left < RADIO_MP3_READ_TARGET) {
        if (ctx->failed) {
            set_station_ui("Reconnecting", "Read failed");
            return *bytes_left > 0;
        }

        const int room = RADIO_MP3_READ_BUF_SIZE - *bytes_left;
        if (room <= 0) {
            return true;
        }

        size_t read = xStreamBufferReceive(ctx->stream, buffer + *bytes_left, room, pdMS_TO_TICKS(80));
        if (read > 0) {
            *bytes_left += read;
            continue;
        }

        set_station_ui("Buffering", "Waiting for data");
        return *bytes_left > 0;
    }

    return true;
}

static void amplify_pcm(int16_t *pcm, size_t samples)
{
    for (size_t i = 0; i < samples; ++i) {
        int value = (int)pcm[i] * 2;
        if (value > 32767) {
            value = 32767;
        } else if (value < -32768) {
            value = -32768;
        }
        pcm[i] = (int16_t)value;
    }
}

static bool write_frame_pcm(int16_t *pcm, const MP3FrameInfo *info)
{
    if (info->samprate <= 0 || app_audio_set_sample_rate(info->samprate) != ESP_OK) {
        ESP_LOGW(TAG, "unsupported sample rate %d", info->samprate);
        set_station_ui("Unsupported", "Bad sample rate");
        return false;
    }

    size_t samples = 0;
    if (info->nChans == 2) {
        samples = info->outputSamps;
    } else if (info->nChans == 1 && info->outputSamps * 2 <= RADIO_PCM_MAX_SAMPLES) {
        for (int i = info->outputSamps - 1; i >= 0; --i) {
            pcm[i * 2] = pcm[i];
            pcm[i * 2 + 1] = pcm[i];
        }
        samples = info->outputSamps * 2;
    } else {
        ESP_LOGW(TAG, "unsupported channel count %d", info->nChans);
        return false;
    }

    amplify_pcm(pcm, samples);
    if (app_audio_write_pcm(pcm, samples, 700) != ESP_OK) {
        ESP_LOGW(TAG, "PCM write failed samples=%u", (unsigned)samples);
        return false;
    }
    return true;
}

static int pcm_peak(const int16_t *pcm, size_t samples)
{
    int peak = 0;
    for (size_t i = 0; i < samples; ++i) {
        int value = pcm[i];
        if (value < 0) {
            value = -value;
        }
        if (value > peak) {
            peak = value;
        }
    }
    return peak;
}

static bool stream_play_url(const RadioStation *station, const char *url, int url_index)
{
    if (!station || !url) {
        return false;
    }

    char opening_detail[48];
    snprintf(opening_detail, sizeof(opening_detail), url_index == 0 ? "Opening stream" : "Fallback %d", url_index + 1);
    set_station_ui("Connecting", opening_detail);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 4000;
    config.buffer_size = 2048;
    config.buffer_size_tx = 512;
    config.disable_auto_redirect = false;
    config.max_redirection_count = 5;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        set_station_ui("Error", "HTTP init failed");
        return false;
    }

    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0 ESP32 Radio");
    esp_http_client_set_header(client, "Accept", "audio/mpeg,*/*");
    esp_http_client_set_header(client, "Icy-MetaData", "0");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open failed station=%s url=%s err=%s", station->name, url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    char *content_type = NULL;
    esp_http_client_get_header(client, "Content-Type", &content_type);
    if (status < 200 || status >= 400) {
        ESP_LOGW(TAG, "bad stream status station=%s url=%s status=%d len=%d content-type=%s",
                 station->name, url, status, content_length, content_type ? content_type : "(none)");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    ESP_LOGI(TAG, "stream open station=%s url=%s status=%d len=%d content-type=%s",
             station->name, url, status, content_length, content_type ? content_type : "(none)");
    set_station_ui("Buffering", "Filling stream buffer");

    HMP3Decoder decoder = MP3InitDecoder();
    StreamBufferHandle_t stream_buffer = xStreamBufferCreateWithCaps(RADIO_STREAM_BUFFER_SIZE, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *read_buffer = (uint8_t *)heap_caps_malloc(RADIO_MP3_READ_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *pcm_buffer = (int16_t *)heap_caps_malloc(RADIO_PCM_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!decoder || !stream_buffer || !read_buffer || !pcm_buffer) {
        ESP_LOGE(TAG, "MP3 decoder alloc failed");
        set_station_ui("Error", "No decoder memory");
        if (stream_buffer) {
            vStreamBufferDelete(stream_buffer);
        }
        heap_caps_free(read_buffer);
        heap_caps_free(pcm_buffer);
        if (decoder) {
            MP3FreeDecoder(decoder);
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    RadioReaderContext reader = {};
    reader.client = client;
    reader.stream = stream_buffer;
    TaskHandle_t reader_task = NULL;
    if (xTaskCreate(radio_reader_task, "radio_http", 4096, &reader, 5, &reader_task) != pdPASS) {
        ESP_LOGE(TAG, "radio reader task create failed");
        set_station_ui("Error", "No reader task");
        vStreamBufferDelete(stream_buffer);
        MP3FreeDecoder(decoder);
        heap_caps_free(read_buffer);
        heap_caps_free(pcm_buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    if (!wait_start_buffer(&reader)) {
        reader.stop = true;
    }

    uint8_t *read_ptr = read_buffer;
    int bytes_left = 0;
    int decoded_frames = 0;
    int decode_errors = 0;
    while (s_play_requested && !reader.stop) {
        if (check_interrupt_commands()) {
            break;
        }
        if (!app_net_is_connected()) {
            set_station_ui("Reconnecting", "WiFi lost");
            break;
        }

        if (bytes_left < RADIO_MP3_REFILL_THRESHOLD) {
            if (!fill_stream_buffer(&reader, read_buffer, &read_ptr, &bytes_left)) {
                break;
            }
        }

        if (bytes_left <= 0) {
            set_station_ui("Buffering", "Waiting for data");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (offset < 0) {
            bytes_left = 0;
            read_ptr = read_buffer;
            set_station_ui("Buffering", "Finding MP3 sync");
            continue;
        }
        read_ptr += offset;
        bytes_left -= offset;

        int err = MP3Decode(decoder, &read_ptr, &bytes_left, pcm_buffer, 0);
        if (err == ERR_MP3_INDATA_UNDERFLOW) {
            continue;
        }
        if (err == ERR_MP3_MAINDATA_UNDERFLOW) {
            continue;
        }
        if (err != ERR_MP3_NONE) {
            ++decode_errors;
            ESP_LOGW(TAG, "MP3 decode err=%d errors=%d bytes_left=%d", err, decode_errors, bytes_left);
            if (decode_errors > 20) {
                set_station_ui("Reconnecting", "Decode errors");
                break;
            }
            if (bytes_left > 0) {
                ++read_ptr;
                --bytes_left;
            }
            continue;
        }

        decode_errors = 0;
        MP3FrameInfo frame_info = {};
        MP3GetLastFrameInfo(decoder, &frame_info);
        if (!write_frame_pcm(pcm_buffer, &frame_info)) {
            if (frame_info.samprate <= 0) {
                break;
            }
        }
        ++decoded_frames;

        if (decoded_frames == 1 || decoded_frames % 64 == 0) {
            char detail[48];
            snprintf(detail, sizeof(detail), "%d kbps %d Hz %u KB",
                     frame_info.bitrate / 1000, frame_info.samprate, (unsigned)(reader.total_bytes / 1024));
            set_station_ui("Playing", detail);
            ESP_LOGI(TAG, "decoded station=%s frames=%d bitrate=%d sample_rate=%d channels=%d samples=%d peak=%d total=%u KB",
                     station->name, decoded_frames, frame_info.bitrate, frame_info.samprate,
                     frame_info.nChans, frame_info.outputSamps,
                     pcm_peak(pcm_buffer, frame_info.outputSamps), (unsigned)(reader.total_bytes / 1024));
        }
    }

    reader.stop = true;
    for (int i = 0; i < 40 && !reader.done; ++i) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    MP3FreeDecoder(decoder);
    vStreamBufferDelete(stream_buffer);
    heap_caps_free(read_buffer);
    heap_caps_free(pcm_buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return decoded_frames > 0;
}

static void stream_play_mp3(void)
{
    const RadioStation *station = &RADIO_STATIONS[s_station_index];
    bool tried_any = false;
    for (int i = 0; i < 3 && s_play_requested; ++i) {
        const char *url = station->urls[i];
        if (!url) {
            continue;
        }
        tried_any = true;
        if (stream_play_url(station, url, i)) {
            return;
        }
        if (check_interrupt_commands()) {
            return;
        }
        set_station_ui("Connecting", "Trying fallback");
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    if (s_play_requested) {
        set_station_ui("Error", tried_any ? "All sources failed" : "No source");
    }
}

static void radio_task(void *arg)
{
    (void)arg;
    set_station_ui("Ready", "Tap Play");

    while (true) {
        RadioCommand command;
        if (xQueueReceive(s_radio_queue, &command, pdMS_TO_TICKS(250)) == pdTRUE) {
            handle_command(command);
        }

        if (!s_play_requested) {
            continue;
        }

        if (!app_net_wait_connected(1000)) {
            set_station_ui("Waiting WiFi", "Need network");
            continue;
        }

        stream_play_mp3();
        if (s_play_requested) {
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }
}

void app_radio_start(void)
{
    if (s_radio_started) {
        return;
    }
    s_radio_started = true;
    s_radio_queue = xQueueCreate(6, sizeof(RadioCommand));
    xTaskCreate(radio_task, "radio", 8192, NULL, 4, NULL);
}

void app_radio_play_pause(void)
{
    post_command(RADIO_CMD_PLAY_PAUSE);
}

void app_radio_stop(void)
{
    post_command(RADIO_CMD_STOP);
}

void app_radio_next(void)
{
    post_command(RADIO_CMD_NEXT);
}

void app_radio_prev(void)
{
    post_command(RADIO_CMD_PREV);
}
