#include "app_net.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "app_ui.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "app_net";
static const char *PORTAL_SSID = "xiaozhi-setup";
static const char *PORTAL_IP = "192.168.10.1";
static const int CONNECTED_BIT = BIT0;
static const int MAX_WIFI_CREDENTIALS = 3;

struct WifiCredential {
    char ssid[33];
    char password[65];
};

static EventGroupHandle_t s_wifi_events;
static httpd_handle_t s_httpd;
static esp_netif_t *s_ap_netif;
static WifiCredential s_credentials[MAX_WIFI_CREDENTIALS];
static int s_credential_count;
static int s_active_credential;
static int s_retry_count;
static bool s_wifi_started;
static bool s_apsta_mode;
static int s_dns_log_count;

const char *app_net_portal_ssid(void)
{
    return PORTAL_SSID;
}

static void url_decode(char *dst, size_t dst_len, const char *src)
{
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 1 < dst_len; ++si) {
        if (src[si] == '%' && isxdigit((unsigned char)src[si + 1]) && isxdigit((unsigned char)src[si + 2])) {
            char hex[3] = {src[si + 1], src[si + 2], 0};
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else if (src[si] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = 0;
}

static bool form_value(const char *body, const char *key, char *out, size_t out_len)
{
    const size_t key_len = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *v = p + key_len + 1;
            const char *end = strchr(v, '&');
            char tmp[256] = {};
            size_t len = end ? (size_t)(end - v) : strlen(v);
            if (len >= sizeof(tmp)) {
                len = sizeof(tmp) - 1;
            }
            memcpy(tmp, v, len);
            url_decode(out, out_len, tmp);
            return true;
        }
        p = strchr(p, '&');
        if (p) {
            ++p;
        }
    }
    return false;
}

static void html_escape(char *dst, size_t dst_len, const char *src)
{
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 1 < dst_len; ++si) {
        const char *entity = NULL;
        if (src[si] == '&') entity = "&amp;";
        else if (src[si] == '<') entity = "&lt;";
        else if (src[si] == '>') entity = "&gt;";
        else if (src[si] == '"') entity = "&quot;";

        if (entity) {
            size_t len = strlen(entity);
            if (di + len >= dst_len) {
                break;
            }
            memcpy(dst + di, entity, len);
            di += len;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = 0;
}

static void configure_portal_dhcp_options(void)
{
    if (!s_ap_netif) {
        return;
    }

    esp_netif_dns_info_t dns = {};
    dns.ip.u_addr.ip4.addr = inet_addr(PORTAL_IP);
    dns.ip.type = IPADDR_TYPE_V4;

    uint8_t dhcps_offer_dns = 0x02;
    esp_netif_ip_info_t ip_info = {};
    ip_info.ip.addr = inet_addr(PORTAL_IP);
    ip_info.gw.addr = inet_addr(PORTAL_IP);
    ip_info.netmask.addr = inet_addr("255.255.255.0");

    static char portal_uri[32];
    snprintf(portal_uri, sizeof(portal_uri), "http://%s/", PORTAL_IP);

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(s_ap_netif));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_ip_info(s_ap_netif, &ip_info));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(
        s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
        &dhcps_offer_dns, sizeof(dhcps_offer_dns)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(
        s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
        (void *)portal_uri, strlen(portal_uri)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(s_ap_netif));
    ESP_LOGI(TAG, "DHCP portal options: dns=%s uri=%s", PORTAL_IP, portal_uri);
}

static esp_err_t ensure_scan_mode(void)
{
    if (!s_apsta_mode) {
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            return err;
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
        s_apsta_mode = true;
        ESP_LOGI(TAG, "enabled APSTA mode for WiFi scan");
    }
    return ESP_OK;
}

static esp_err_t load_credentials(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    s_credential_count = 0;
    for (int i = 0; i < MAX_WIFI_CREDENTIALS; ++i) {
        char ssid_key[8];
        char pass_key[8];
        snprintf(ssid_key, sizeof(ssid_key), "ssid%d", i);
        snprintf(pass_key, sizeof(pass_key), "pass%d", i);
        size_t ssid_len = sizeof(s_credentials[i].ssid);
        size_t pass_len = sizeof(s_credentials[i].password);
        err = nvs_get_str(nvs, ssid_key, s_credentials[i].ssid, &ssid_len);
        if (err != ESP_OK || s_credentials[i].ssid[0] == 0) {
            continue;
        }
        err = nvs_get_str(nvs, pass_key, s_credentials[i].password, &pass_len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            s_credentials[i].password[0] = 0;
        }
        ++s_credential_count;
    }

    if (s_credential_count == 0) {
        size_t ssid_len = sizeof(s_credentials[0].ssid);
        size_t pass_len = sizeof(s_credentials[0].password);
        err = nvs_get_str(nvs, "ssid", s_credentials[0].ssid, &ssid_len);
        if (err == ESP_OK && s_credentials[0].ssid[0] != 0) {
            if (nvs_get_str(nvs, "pass", s_credentials[0].password, &pass_len) == ESP_ERR_NVS_NOT_FOUND) {
                s_credentials[0].password[0] = 0;
            }
            s_credential_count = 1;
        }
    }
    nvs_close(nvs);
    s_active_credential = 0;
    ESP_LOGI(TAG, "loaded %d saved WiFi credential(s)", s_credential_count);
    return s_credential_count > 0 ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

static esp_err_t save_weather_settings(const char *city, const char *latitude, const char *longitude)
{
    bool has_location = latitude && latitude[0] && longitude && longitude[0];
    bool has_city = city && city[0];
    if (!has_location && !has_city) {
        return ESP_OK;
    }

    if (has_location) {
        char *end = NULL;
        double lat = strtod(latitude, &end);
        bool lat_ok = end && *end == 0 && lat >= -90.0 && lat <= 90.0;
        end = NULL;
        double lon = strtod(longitude, &end);
        bool lon_ok = end && *end == 0 && lon >= -180.0 && lon <= 180.0;
        if (!lat_ok || !lon_ok) {
            ESP_LOGW(TAG, "ignore invalid weather location lat=%s lon=%s", latitude, longitude);
            return ESP_OK;
        }
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("weather_cfg", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    if (has_city) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, "city", city));
    }
    if (has_location) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, "lat", latitude));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, "lon", longitude));
    }
    err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved weather location city=%s lat=%s lon=%s",
                 has_city ? city : "(unchanged)",
                 has_location ? latitude : "(unchanged)",
                 has_location ? longitude : "(unchanged)");
    }
    return err;
}

esp_err_t app_net_save_credentials(const char *ssid, const char *password)
{
    WifiCredential updated[MAX_WIFI_CREDENTIALS] = {};
    strlcpy(updated[0].ssid, ssid, sizeof(updated[0].ssid));
    strlcpy(updated[0].password, password ? password : "", sizeof(updated[0].password));

    int out = 1;
    for (int i = 0; i < s_credential_count && out < MAX_WIFI_CREDENTIALS; ++i) {
        if (strcmp(s_credentials[i].ssid, ssid) == 0) {
            continue;
        }
        updated[out++] = s_credentials[i];
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < MAX_WIFI_CREDENTIALS; ++i) {
        char ssid_key[8];
        char pass_key[8];
        snprintf(ssid_key, sizeof(ssid_key), "ssid%d", i);
        snprintf(pass_key, sizeof(pass_key), "pass%d", i);
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_erase_key(nvs, ssid_key));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_erase_key(nvs, pass_key));
        if (i < out) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, ssid_key, updated[i].ssid));
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, pass_key, updated[i].password));
        }
    }
    err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) {
        memcpy(s_credentials, updated, sizeof(s_credentials));
        s_credential_count = out;
        s_active_credential = 0;
        s_retry_count = 0;
        ESP_LOGI(TAG, "saved WiFi credential '%s', total=%d", ssid, s_credential_count);
    }
    return err;
}

static void connect_sta(void)
{
    if (s_credential_count <= 0 || s_active_credential >= s_credential_count) {
        return;
    }

    if (!s_apsta_mode) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_APSTA));
        s_apsta_mode = true;
    }

    const WifiCredential *cred = &s_credentials[s_active_credential];
    wifi_config_t sta_config = {};
    strlcpy((char *)sta_config.sta.ssid, cred->ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, cred->password, sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());

    char msg[128];
    snprintf(msg, sizeof(msg), "Connecting WiFi %d/%d: %s", s_active_credential + 1, s_credential_count, cred->ssid);
    app_ui_set_network_status(msg);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, CONNECTED_BIT);
        if (s_credential_count > 0 && s_retry_count < 4) {
            ++s_retry_count;
            ESP_LOGW(TAG, "STA disconnected, retry %d on slot %d", s_retry_count, s_active_credential);
            esp_wifi_connect();
        } else if (s_credential_count > 1 && s_active_credential + 1 < s_credential_count) {
            ++s_active_credential;
            s_retry_count = 0;
            ESP_LOGW(TAG, "switch to saved WiFi slot %d", s_active_credential);
            connect_sta();
        } else {
            s_retry_count = 0;
            s_active_credential = 0;
            app_ui_set_network_status("WiFi setup: join xiaozhi-setup, open 192.168.10.1");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "phone joined setup AP mac=" MACSTR " aid=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = (const wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGW(TAG, "phone left setup AP mac=" MACSTR " aid=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_events, CONNECTED_BIT);
        char msg[128];
        snprintf(msg, sizeof(msg), "WiFi OK  IP " IPSTR, IP2STR(&event->ip_info.ip));
        app_ui_set_network_status(msg);
        ESP_LOGI(TAG, "%s", msg);
    }
}

static esp_err_t send_wifi_options(httpd_req_t *req, bool scan)
{
    if (!scan) {
        httpd_resp_send_chunk(req, "<option value=\"\">Tap Scan WiFi first</option>", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    esp_err_t scan_err = ensure_scan_mode();
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    if (scan_err == ESP_OK) {
        scan_err = esp_wifi_scan_start(&scan_config, true);
    }
    if (scan_err != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "<option value=\"\">Scan failed: %s</option>", esp_err_to_name(scan_err));
        httpd_resp_send_chunk(req, msg, HTTPD_RESP_USE_STRLEN);
        return scan_err;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) {
        ap_count = 20;
    }
    if (ap_count == 0) {
        httpd_resp_send_chunk(req, "<option value=\"\">No WiFi found</option>", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    wifi_ap_record_t records[20] = {};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_get_ap_records(&ap_count, records));
    for (uint16_t i = 0; i < ap_count; ++i) {
        if (records[i].ssid[0] == 0) {
            continue;
        }
        char ssid[33] = {};
        char escaped[96] = {};
        char rssi_text[24] = {};
        strlcpy(ssid, (const char *)records[i].ssid, sizeof(ssid));
        html_escape(escaped, sizeof(escaped), ssid);
        snprintf(rssi_text, sizeof(rssi_text), " (%d dBm)</option>", records[i].rssi);
        httpd_resp_send_chunk(req, "<option value=\"", HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, escaped, HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, "\">", HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, escaped, HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, rssi_text, HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

static esp_err_t save_xiaozhi_settings(const char *ota_url, const char *ws_url, const char *token)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("xiaozhi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    if (ota_url && ota_url[0]) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, "ota_url", ota_url));
    }
    if (ws_url && ws_url[0]) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, "ws_url", ws_url));
    }
    if (token && token[0]) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, "token", token));
    }
    err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static esp_err_t serve_setup_page(httpd_req_t *req, bool scan)
{
    ESP_LOGI(TAG, "HTTP %s scan=%d", req->uri, scan ? 1 : 0);
    const char *head =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32S3 WiFi</title><style>body{font-family:sans-serif;background:#050505;color:#fff;padding:24px}"
        "input,select,button{box-sizing:border-box;width:100%;font-size:18px;margin:10px 0;padding:12px;border-radius:8px;border:0}"
        "label{display:block;margin-top:16px;color:#ffbd55}.muted{color:#aaa;font-size:14px}.secondary{background:#333;color:#fff;text-decoration:none;display:block;text-align:center}"
        "button{background:#ffbd55;color:#111;font-weight:700}</style></head><body>"
        "<h2>ESP32S3 WiFi Setup</h2><p>Select your WiFi, then enter password.</p>"
        "<a class='secondary' href='/scan'>Scan WiFi</a>"
        "<form method='post' action='/save'><label>Nearby WiFi</label><select name='ssid'>";
    const char *tail =
        "</select><label>Or type SSID manually</label><input name='manual_ssid' placeholder='WiFi SSID'>"
        "<label>Password</label><input name='pass' placeholder='WiFi password' type='password'>"
        "<label>Weather city</label><input name='city' placeholder='Shanghai'>"
        "<label>Latitude</label><input name='lat' placeholder='31.2304' inputmode='decimal'>"
        "<label>Longitude</label><input name='lon' placeholder='121.4737' inputmode='decimal'>"
        "<label>XiaoZhi OTA URL</label><input name='xz_ota' placeholder='https://api.tenclass.net/xiaozhi/ota/'>"
        "<label>XiaoZhi WebSocket URL</label><input name='xz_ws' placeholder='wss://... optional'>"
        "<label>XiaoZhi Token</label><input name='xz_token' placeholder='optional'>"
        "<button>Save and Connect</button></form>"
        "<p class='muted'>The board remembers the latest 3 networks and reconnects automatically.</p>"
        "<p class='muted'>Setup AP: xiaozhi-setup, no password. Manual URL: http://192.168.10.1/</p></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);
    send_wifi_options(req, scan);
    httpd_resp_send_chunk(req, tail, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t page_get_handler(httpd_req_t *req)
{
    return serve_setup_page(req, false);
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    return serve_setup_page(req, true);
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[1024] = {};
    int remaining = req->content_len;
    int offset = 0;
    while (remaining > 0 && offset < (int)sizeof(body) - 1) {
        int got = httpd_req_recv(req, body + offset, sizeof(body) - 1 - offset);
        if (got <= 0) {
            return ESP_FAIL;
        }
        offset += got;
        remaining -= got;
    }

    char ssid[33] = {};
    char manual_ssid[33] = {};
    char pass[65] = {};
    char city[32] = {};
    char latitude[24] = {};
    char longitude[24] = {};
    char xz_ota[128] = {};
    char xz_ws[160] = {};
    char xz_token[160] = {};
    form_value(body, "ssid", ssid, sizeof(ssid));
    form_value(body, "manual_ssid", manual_ssid, sizeof(manual_ssid));
    if (manual_ssid[0] != 0) {
        strlcpy(ssid, manual_ssid, sizeof(ssid));
    }
    if (ssid[0] == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID missing");
        return ESP_OK;
    }
    form_value(body, "pass", pass, sizeof(pass));
    form_value(body, "city", city, sizeof(city));
    form_value(body, "lat", latitude, sizeof(latitude));
    form_value(body, "lon", longitude, sizeof(longitude));
    form_value(body, "xz_ota", xz_ota, sizeof(xz_ota));
    form_value(body, "xz_ws", xz_ws, sizeof(xz_ws));
    form_value(body, "xz_token", xz_token, sizeof(xz_token));

    esp_err_t err = app_net_save_credentials(ssid, pass);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS save failed");
        return ESP_OK;
    }
    err = save_weather_settings(city, latitude, longitude);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Weather save failed");
        return ESP_OK;
    }
    err = save_xiaozhi_settings(xz_ota, xz_ws, xz_token);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "XiaoZhi save failed");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, "<html><body><h2>Saved. Connecting...</h2><p>You can return to the device screen.</p></body></html>");
    connect_sta();
    return ESP_OK;
}

static esp_err_t portal_404_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    ESP_LOGI(TAG, "serve captive portal 404 request: %s", req->uri);
    return serve_setup_page(req, false);
}

static void start_http_server(void)
{
    if (s_httpd) {
        return;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;
    if (httpd_start(&s_httpd, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
        return;
    }

    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = page_get_handler, .user_ctx = NULL};
    httpd_uri_t scan = {.uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler, .user_ctx = NULL};
    httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post_handler, .user_ctx = NULL};
    httpd_uri_t gen204 = {.uri = "/generate_204", .method = HTTP_GET, .handler = page_get_handler, .user_ctx = NULL};
    httpd_uri_t hot = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = page_get_handler, .user_ctx = NULL};
    httpd_uri_t connecttest = {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = page_get_handler, .user_ctx = NULL};
    httpd_uri_t ncsi = {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = page_get_handler, .user_ctx = NULL};
    httpd_uri_t success = {.uri = "/success.txt", .method = HTTP_GET, .handler = page_get_handler, .user_ctx = NULL};
    httpd_uri_t canonical = {.uri = "/canonical.html", .method = HTTP_GET, .handler = page_get_handler, .user_ctx = NULL};
    httpd_uri_t fwlink = {.uri = "/fwlink", .method = HTTP_GET, .handler = page_get_handler, .user_ctx = NULL};
    httpd_uri_t library = {.uri = "/library/test/success.html", .method = HTTP_GET, .handler = page_get_handler, .user_ctx = NULL};
    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &scan);
    httpd_register_uri_handler(s_httpd, &save);
    httpd_register_uri_handler(s_httpd, &gen204);
    httpd_register_uri_handler(s_httpd, &hot);
    httpd_register_uri_handler(s_httpd, &connecttest);
    httpd_register_uri_handler(s_httpd, &ncsi);
    httpd_register_uri_handler(s_httpd, &success);
    httpd_register_uri_handler(s_httpd, &canonical);
    httpd_register_uri_handler(s_httpd, &fwlink);
    httpd_register_uri_handler(s_httpd, &library);
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, portal_404_handler);
}

static void dns_server_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "DNS bind failed errno=%d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t rx[256];
    uint8_t tx[320];
    sockaddr_in source = {};
    socklen_t socklen = sizeof(source);
    const uint32_t portal_ip = inet_addr(PORTAL_IP);
    while (true) {
        int len = recvfrom(sock, rx, sizeof(rx), 0, (sockaddr *)&source, &socklen);
        if (len < 12) {
            continue;
        }
        if (s_dns_log_count < 20) {
            char domain[96] = {};
            int p = 12;
            int di = 0;
            while (p < len && rx[p] != 0 && di < (int)sizeof(domain) - 2) {
                int part_len = rx[p++];
                if (part_len <= 0 || p + part_len > len) {
                    break;
                }
                if (di > 0) {
                    domain[di++] = '.';
                }
                for (int i = 0; i < part_len && di < (int)sizeof(domain) - 1; ++i) {
                    domain[di++] = (char)rx[p++];
                }
            }
            domain[di] = 0;
            ESP_LOGI(TAG, "DNS query from %s: %s", inet_ntoa(source.sin_addr), domain);
            ++s_dns_log_count;
        }
        memcpy(tx, rx, len);
        tx[2] = 0x81;
        tx[3] = 0x80;
        tx[6] = 0x00;
        tx[7] = 0x01;
        tx[8] = tx[9] = tx[10] = tx[11] = 0;

        int q_end = 12;
        while (q_end < len && rx[q_end] != 0) {
            q_end += rx[q_end] + 1;
        }
        if (q_end + 5 > len || q_end + 16 > (int)sizeof(tx)) {
            continue;
        }
        int pos = len;
        tx[pos++] = 0xc0;
        tx[pos++] = 0x0c;
        tx[pos++] = 0x00;
        tx[pos++] = 0x01;
        tx[pos++] = 0x00;
        tx[pos++] = 0x01;
        tx[pos++] = 0x00;
        tx[pos++] = 0x00;
        tx[pos++] = 0x00;
        tx[pos++] = 0x3c;
        tx[pos++] = 0x00;
        tx[pos++] = 0x04;
        memcpy(&tx[pos], &portal_ip, 4);
        pos += 4;
        sendto(sock, tx, pos, 0, (sockaddr *)&source, socklen);
    }
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
}

void app_net_start(void)
{
    if (s_wifi_started) {
        return;
    }
    s_wifi_started = true;
    init_nvs();
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    configure_portal_dhcp_options();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));
    wifi_country_t country = {
        .cc = "CN",
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 84,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_country(&country));

    wifi_config_t ap_config = {};
    strlcpy((char *)ap_config.ap.ssid, PORTAL_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(PORTAL_SSID);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.ssid_hidden = 0;
    ap_config.ap.beacon_interval = 100;
    ap_config.ap.pmf_cfg.required = false;

    load_credentials();
    s_apsta_mode = s_credential_count > 0;
    ESP_ERROR_CHECK(esp_wifi_set_mode(s_apsta_mode ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));
    if (s_apsta_mode) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_max_tx_power(84));
    ESP_LOGI(TAG, "setup AP started: SSID=%s auth=open channel=%d ip=%s", PORTAL_SSID, ap_config.ap.channel, PORTAL_IP);

    start_http_server();
    xTaskCreate(dns_server_task, "dns_portal", 4096, NULL, 4, NULL);

    if (s_credential_count > 0) {
        connect_sta();
    } else {
        app_ui_set_network_status("WiFi setup: join xiaozhi-setup, open 192.168.10.1");
    }
}

bool app_net_is_connected(void)
{
    return s_wifi_events && (xEventGroupGetBits(s_wifi_events) & CONNECTED_BIT);
}

bool app_net_wait_connected(uint32_t timeout_ms)
{
    if (!s_wifi_events) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return bits & CONNECTED_BIT;
}
