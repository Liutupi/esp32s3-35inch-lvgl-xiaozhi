#include "app_xiaozhi.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "app_audio.h"
#include "app_net.h"
#include "app_radio.h"
#include "app_ui.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_http.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "opus_decoder.h"
#include "opus_encoder.h"
#include "tcp_transport.h"
#include "tls_transport.h"
#include "web_socket.h"

static const char *TAG = "app_xiaozhi";

static constexpr const char *OTA_URL = "https://api.tenclass.net/xiaozhi/ota/";
static constexpr const char *BOARD_NAME = "esp32-s3-touch-lcd-3.5";
static constexpr int CLIENT_PROTOCOL_VERSION = 3;
static constexpr int AUDIO_SAMPLE_RATE = 16000;
static constexpr int AUDIO_FRAME_MS = 60;
static constexpr int AUDIO_FRAME_SAMPLES = AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS / 1000;
static constexpr EventBits_t WS_HELLO_BIT = BIT0;
static constexpr EventBits_t WS_CLOSED_BIT = BIT1;

struct XiaozhiConfig {
    std::string url;
    std::string token;
    int version = CLIENT_PROTOCOL_VERSION;
};

static TaskHandle_t s_task;
static EventGroupHandle_t s_events;
static WebSocket *s_ws;
static OpusEncoderWrapper *s_encoder;
static OpusDecoderWrapper *s_decoder;
static volatile bool s_started;
static volatile bool s_listening;
static volatile bool s_send_mic;
static volatile app_xiaozhi_state_t s_state = APP_XIAOZHI_STATE_IDLE;
static TickType_t s_last_toggle_tick;
static std::string s_session_id;
static int s_server_sample_rate = 24000;
static int s_server_frame_ms = 60;

extern "C" void app_ui_set_xiaozhi(const char *state, const char *message, const char *emotion);

static void set_state(app_xiaozhi_state_t state, const char *message, const char *emotion)
{
    s_state = state;
    const char *state_text = "Standby";
    switch (state) {
    case APP_XIAOZHI_STATE_CONFIGURING:
        state_text = "Configuring";
        break;
    case APP_XIAOZHI_STATE_CONNECTING:
        state_text = "Connecting";
        break;
    case APP_XIAOZHI_STATE_LISTENING:
        state_text = "Listening";
        break;
    case APP_XIAOZHI_STATE_SPEAKING:
        state_text = "Speaking";
        break;
    case APP_XIAOZHI_STATE_ERROR:
        state_text = "Error";
        break;
    default:
        break;
    }
    app_ui_set_xiaozhi(state_text, message ? message : "", emotion ? emotion : "neutral");
}

static std::string mac_string(void)
{
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

static std::string load_or_create_uuid(void)
{
    nvs_handle_t nvs;
    char uuid[37] = {};
    size_t len = sizeof(uuid);
    if (nvs_open("board", NVS_READWRITE, &nvs) == ESP_OK) {
        if (nvs_get_str(nvs, "uuid", uuid, &len) == ESP_OK && uuid[0]) {
            nvs_close(nvs);
            return std::string(uuid);
        }
        uint8_t raw[16];
        esp_fill_random(raw, sizeof(raw));
        raw[6] = (raw[6] & 0x0F) | 0x40;
        raw[8] = (raw[8] & 0x3F) | 0x80;
        snprintf(uuid, sizeof(uuid),
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
                 raw[8], raw[9], raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);
        nvs_set_str(nvs, "uuid", uuid);
        nvs_commit(nvs);
        nvs_close(nvs);
        return std::string(uuid);
    }
    return mac_string();
}

static std::string nvs_get_string(const char *ns, const char *key)
{
    nvs_handle_t nvs;
    char value[192] = {};
    size_t len = sizeof(value);
    if (nvs_open(ns, NVS_READONLY, &nvs) != ESP_OK) {
        return {};
    }
    esp_err_t err = nvs_get_str(nvs, key, value, &len);
    nvs_close(nvs);
    return err == ESP_OK ? std::string(value) : std::string();
}

static std::string board_json(const std::string &uuid)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);

    std::string json = "{";
    json += "\"version\":2,";
    json += "\"language\":\"zh-CN\",";
    json += "\"flash_size\":" + std::to_string(flash_size) + ",";
    json += "\"minimum_free_heap_size\":" + std::to_string(esp_get_minimum_free_heap_size()) + ",";
    json += "\"mac_address\":\"" + mac_string() + "\",";
    json += "\"uuid\":\"" + uuid + "\",";
    json += "\"chip_model_name\":\"esp32s3\",";
    json += "\"chip_info\":{\"model\":" + std::to_string(chip.model) +
            ",\"cores\":" + std::to_string(chip.cores) +
            ",\"revision\":" + std::to_string(chip.revision) +
            ",\"features\":" + std::to_string(chip.features) + "},";
    json += "\"application\":{\"name\":\"" + std::string(app->project_name) +
            "\",\"version\":\"" + std::string(app->version) +
            "\",\"compile_time\":\"" + std::string(app->date) + "T" + std::string(app->time) +
            "Z\",\"idf_version\":\"" + std::string(app->idf_ver) + "\"},";
    json += "\"ota\":{\"label\":\"factory\"},";
    json += "\"board\":{\"type\":\"" + std::string(BOARD_NAME) + "\",\"name\":\"" + std::string(BOARD_NAME) + "\",\"features\":{\"wifi\":true,\"screen\":true,\"speaker\":true,\"mic\":true}}";
    json += "}";
    return json;
}

static bool fetch_config(XiaozhiConfig &config, std::string &activation)
{
    const std::string uuid = load_or_create_uuid();
    config.url = nvs_get_string("xiaozhi", "ws_url");
    config.token = nvs_get_string("xiaozhi", "token");
    if (!config.url.empty()) {
        ESP_LOGI(TAG, "using manual XiaoZhi websocket url");
        return true;
    }

    std::string ota_url = nvs_get_string("xiaozhi", "ota_url");
    if (ota_url.empty()) {
        ota_url = OTA_URL;
    }
    EspHttp http;
    http.SetTimeout(8000);
    http.SetHeader("Activation-Version", "1");
    http.SetHeader("Device-Id", mac_string());
    http.SetHeader("Client-Id", uuid);
    http.SetHeader("User-Agent", std::string(BOARD_NAME) + "/1.0.0");
    http.SetHeader("Accept-Language", "zh-CN");
    http.SetHeader("Content-Type", "application/json");
    http.SetContent(board_json(uuid));

    if (!http.Open("POST", ota_url)) {
        ESP_LOGE(TAG, "open OTA config failed");
        return false;
    }
    const int status = http.GetStatusCode();
    if (status != 200) {
        ESP_LOGE(TAG, "OTA config status=%d", status);
        http.Close();
        return false;
    }
    std::string body = http.ReadAll();
    http.Close();

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) {
        ESP_LOGE(TAG, "parse OTA config failed");
        return false;
    }

    cJSON *activation_obj = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation_obj)) {
        cJSON *code = cJSON_GetObjectItem(activation_obj, "code");
        cJSON *message = cJSON_GetObjectItem(activation_obj, "message");
        if (cJSON_IsString(code)) {
            activation = std::string("Activation: ") + code->valuestring;
            if (cJSON_IsString(message)) {
                activation += " ";
                activation += message->valuestring;
            }
        }
    }

    cJSON *ws = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(ws)) {
        cJSON *url = cJSON_GetObjectItem(ws, "url");
        cJSON *token = cJSON_GetObjectItem(ws, "token");
        cJSON *version = cJSON_GetObjectItem(ws, "version");
        if (cJSON_IsString(url)) {
            config.url = url->valuestring;
        }
        if (cJSON_IsString(token)) {
            config.token = token->valuestring;
        }
        if (cJSON_IsNumber(version)) {
            config.version = version->valueint;
        }
    }
    cJSON_Delete(root);
    return !config.url.empty();
}

static std::string hello_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", CLIENT_PROTOCOL_VERSION);
    cJSON_AddItemToObject(root, "features", cJSON_CreateObject());
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON *audio = cJSON_CreateObject();
    cJSON_AddStringToObject(audio, "format", "opus");
    cJSON_AddNumberToObject(audio, "sample_rate", AUDIO_SAMPLE_RATE);
    cJSON_AddNumberToObject(audio, "channels", 1);
    cJSON_AddNumberToObject(audio, "frame_duration", AUDIO_FRAME_MS);
    cJSON_AddItemToObject(root, "audio_params", audio);
    char *text = cJSON_PrintUnformatted(root);
    std::string out(text ? text : "{}");
    cJSON_free(text);
    cJSON_Delete(root);
    return out;
}

static void send_text(const std::string &text)
{
    if (s_ws && s_ws->IsConnected()) {
        s_ws->Send(text);
    }
}

static void cleanup_websocket()
{
    if (s_ws) {
        s_ws->Close();
        delete s_ws;
        s_ws = nullptr;
    }
}

static void send_listen_state(const char *state, const char *mode)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", s_session_id.c_str());
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", state);
    if (mode) {
        cJSON_AddStringToObject(root, "mode", mode);
    }
    char *text = cJSON_PrintUnformatted(root);
    if (text) {
        send_text(text);
        cJSON_free(text);
    }
    cJSON_Delete(root);
}

static void handle_json(const char *data, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        return;
    }
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "hello") == 0) {
        cJSON *session = cJSON_GetObjectItem(root, "session_id");
        if (cJSON_IsString(session)) {
            s_session_id = session->valuestring;
        }
        cJSON *audio = cJSON_GetObjectItem(root, "audio_params");
        if (cJSON_IsObject(audio)) {
            cJSON *rate = cJSON_GetObjectItem(audio, "sample_rate");
            cJSON *frame = cJSON_GetObjectItem(audio, "frame_duration");
            if (cJSON_IsNumber(rate)) {
                s_server_sample_rate = rate->valueint;
            }
            if (cJSON_IsNumber(frame)) {
                s_server_frame_ms = frame->valueint;
            }
        }
        set_state(APP_XIAOZHI_STATE_CONNECTING, "服务器已握手", "happy");
        xEventGroupSetBits(s_events, WS_HELLO_BIT);
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "tts") == 0) {
        cJSON *state = cJSON_GetObjectItem(root, "state");
        if (cJSON_IsString(state) && strcmp(state->valuestring, "start") == 0) {
            s_send_mic = false;
            app_audio_set_sample_rate(s_server_sample_rate);
            set_state(APP_XIAOZHI_STATE_SPEAKING, "小智正在回答", "happy");
        } else if (cJSON_IsString(state) && strcmp(state->valuestring, "stop") == 0) {
            app_audio_set_sample_rate(AUDIO_SAMPLE_RATE);
            s_send_mic = s_listening;
            set_state(s_listening ? APP_XIAOZHI_STATE_LISTENING : APP_XIAOZHI_STATE_IDLE, "按住/点击开始对话", "neutral");
        }
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "stt") == 0) {
        cJSON *text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
            set_state(APP_XIAOZHI_STATE_LISTENING, text->valuestring, "thinking");
        }
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "llm") == 0) {
        cJSON *emotion = cJSON_GetObjectItem(root, "emotion");
        cJSON *text = cJSON_GetObjectItem(root, "text");
        set_state(APP_XIAOZHI_STATE_SPEAKING,
                  cJSON_IsString(text) ? text->valuestring : "小智思考中",
                  cJSON_IsString(emotion) ? emotion->valuestring : "thinking");
    }
    cJSON_Delete(root);
}

static void websocket_data(const char *data, size_t len, bool binary)
{
    if (!binary) {
        handle_json(data, len);
        return;
    }
    if (!s_decoder || len == 0) {
        return;
    }
    const uint8_t *payload = reinterpret_cast<const uint8_t *>(data);
    size_t payload_len = len;
    if (len >= 4 && s_server_frame_ms > 0) {
        uint16_t encoded_len = (static_cast<uint16_t>(payload[2]) << 8) | payload[3];
        if (encoded_len > 0 && encoded_len + 4 <= len) {
            payload += 4;
            payload_len = encoded_len;
        }
    }

    std::vector<uint8_t> opus(payload, payload + payload_len);
    std::vector<int16_t> pcm;
    if (s_decoder->Decode(std::move(opus), pcm) && !pcm.empty()) {
        app_audio_set_sample_rate(s_server_sample_rate);
        app_audio_unmute_output();
        app_audio_write_pcm(pcm.data(), pcm.size(), 1000);
    }
}

static bool open_websocket(const XiaozhiConfig &config)
{
    delete s_ws;
    s_ws = nullptr;

    Transport *transport = nullptr;
    if (config.url.rfind("wss://", 0) == 0) {
        transport = new TlsTransport();
    } else {
        transport = new TcpTransport();
    }
    s_ws = new WebSocket(transport);
    s_ws->SetReceiveBufferSize(4096);
    if (!config.token.empty()) {
        std::string bearer = config.token.find(' ') == std::string::npos ? "Bearer " + config.token : config.token;
        s_ws->SetHeader("Authorization", bearer.c_str());
    }
    s_ws->SetHeader("Protocol-Version", std::to_string(config.version).c_str());
    const std::string mac = mac_string();
    const std::string uuid = load_or_create_uuid();
    s_ws->SetHeader("Device-Id", mac.c_str());
    s_ws->SetHeader("Client-Id", uuid.c_str());
    s_ws->OnData(websocket_data);
    s_ws->OnDisconnected([]() {
        xEventGroupSetBits(s_events, WS_CLOSED_BIT);
        set_state(APP_XIAOZHI_STATE_IDLE, "小智连接已断开", "sad");
    });
    if (!s_ws->Connect(config.url.c_str())) {
        return false;
    }
    xEventGroupClearBits(s_events, WS_HELLO_BIT | WS_CLOSED_BIT);
    s_ws->Send(hello_json());
    EventBits_t bits = xEventGroupWaitBits(s_events, WS_HELLO_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    return (bits & WS_HELLO_BIT) != 0;
}

static void send_audio_loop(void *)
{
    std::vector<int16_t> pcm(AUDIO_FRAME_SAMPLES);
    while (s_started) {
        if (!s_listening || !s_send_mic || !s_ws || !s_ws->IsConnected() || !s_encoder) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (app_audio_read_mono(pcm.data(), pcm.size(), 200) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        std::vector<int16_t> frame = pcm;
        s_encoder->Encode(std::move(frame), [](std::vector<uint8_t> &&opus) {
            if (!s_ws || !s_ws->IsConnected() || opus.empty()) {
                return;
            }
            uint8_t header[4] = {0, 0, static_cast<uint8_t>((opus.size() >> 8) & 0xff), static_cast<uint8_t>(opus.size() & 0xff)};
            std::vector<uint8_t> packet;
            packet.reserve(sizeof(header) + opus.size());
            packet.insert(packet.end(), header, header + sizeof(header));
            packet.insert(packet.end(), opus.begin(), opus.end());
            s_ws->Send(packet.data(), packet.size(), true);
        });
    }
    vTaskDelete(nullptr);
}

static void xiaozhi_task(void *)
{
    while (true) {
        if (!s_started) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!app_net_wait_connected(30000)) {
            set_state(APP_XIAOZHI_STATE_ERROR, "WiFi 未连接", "sad");
            s_started = false;
            continue;
        }
        set_state(APP_XIAOZHI_STATE_CONFIGURING, "正在获取小智配置", "thinking");
        XiaozhiConfig config;
        std::string activation;
        if (!fetch_config(config, activation)) {
            set_state(APP_XIAOZHI_STATE_ERROR, activation.empty() ? "小智配置获取失败" : activation.c_str(), "sad");
            s_started = false;
            continue;
        }
        if (!app_audio_is_ready()) {
            set_state(APP_XIAOZHI_STATE_ERROR, "音频还没有准备好", "sad");
            s_started = false;
            continue;
        }
        app_radio_stop();
        bool audio_acquired = false;
        for (int i = 0; i < 15; ++i) {
            if (app_audio_acquire(APP_AUDIO_OWNER_XIAOZHI, AUDIO_SAMPLE_RATE)) {
                audio_acquired = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (!audio_acquired) {
            set_state(APP_XIAOZHI_STATE_ERROR, "音频正被其他功能占用", "sad");
            s_started = false;
            continue;
        }

        set_state(APP_XIAOZHI_STATE_CONNECTING, "正在连接小智", "thinking");
        delete s_encoder;
        delete s_decoder;
        s_decoder = nullptr;
        s_encoder = new OpusEncoderWrapper(AUDIO_SAMPLE_RATE, 1, AUDIO_FRAME_MS);
        s_encoder->SetComplexity(3);
        s_encoder->SetDtx(false);

        if (!open_websocket(config)) {
            set_state(APP_XIAOZHI_STATE_ERROR, "小智服务器连接失败", "sad");
            cleanup_websocket();
            app_audio_release(APP_AUDIO_OWNER_XIAOZHI);
            s_started = false;
            continue;
        }
        s_decoder = new OpusDecoderWrapper(s_server_sample_rate, 1, s_server_frame_ms);
        s_listening = true;
        s_send_mic = true;
        app_audio_mute_output();
        send_listen_state("start", "manual");
        set_state(APP_XIAOZHI_STATE_LISTENING, "我在听", "neutral");
        xTaskCreate(send_audio_loop, "xiaozhi_audio_tx", 8192, nullptr, 5, nullptr);

        while (s_started && s_ws && s_ws->IsConnected()) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        s_listening = false;
        s_send_mic = false;
        cleanup_websocket();
        app_audio_unmute_output();
        app_audio_release(APP_AUDIO_OWNER_XIAOZHI);
        set_state(APP_XIAOZHI_STATE_IDLE, "按住/点击开始对话", "neutral");
    }
}

void app_xiaozhi_start(void)
{
    if (!s_events) {
        s_events = xEventGroupCreate();
    }
    if (!s_task) {
        xTaskCreate(xiaozhi_task, "xiaozhi", 12288, nullptr, 4, &s_task);
    }
    set_state(APP_XIAOZHI_STATE_IDLE, "按住/点击开始对话", "neutral");
}

void app_xiaozhi_toggle(void)
{
    TickType_t now = xTaskGetTickCount();
    if (now - s_last_toggle_tick < pdMS_TO_TICKS(1500)) {
        return;
    }
    s_last_toggle_tick = now;
    if (!s_started) {
        s_started = true;
        set_state(APP_XIAOZHI_STATE_CONNECTING, "准备连接", "thinking");
    } else {
        app_xiaozhi_stop_session();
    }
}

void app_xiaozhi_stop_session(void)
{
    if (s_ws && s_ws->IsConnected()) {
        send_listen_state("stop", nullptr);
    }
    s_started = false;
    s_listening = false;
    s_send_mic = false;
    cleanup_websocket();
    app_audio_unmute_output();
    app_audio_release(APP_AUDIO_OWNER_XIAOZHI);
    set_state(APP_XIAOZHI_STATE_IDLE, "按住/点击开始对话", "neutral");
}

bool app_xiaozhi_is_active(void)
{
    return s_started;
}

app_xiaozhi_state_t app_xiaozhi_state(void)
{
    return s_state;
}
