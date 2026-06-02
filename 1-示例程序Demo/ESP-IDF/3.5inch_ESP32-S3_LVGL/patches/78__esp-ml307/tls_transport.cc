#include "tls_transport.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_crt_bundle.h>
#include <cstring>

#define TAG "TlsTransport"

TlsTransport::TlsTransport() {
    tls_client_ = esp_tls_init();
}

TlsTransport::~TlsTransport() {
    if (tls_client_) {
        esp_tls_conn_destroy(tls_client_);
    }
}

bool TlsTransport::Connect(const char* host, int port) {
    /* Destroy any previous TLS context to avoid reuse after failure */
    if (tls_client_) {
        esp_tls_conn_destroy(tls_client_);
        tls_client_ = nullptr;
    }
    tls_client_ = esp_tls_init();
    if (!tls_client_) {
        ESP_LOGE(TAG, "Failed to init TLS context");
        return false;
    }

    esp_tls_cfg_t cfg = {};
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    int ret = esp_tls_conn_new_sync(host, strlen(host), port, &cfg, tls_client_);
    if (ret != 1) {
        ESP_LOGE(TAG, "Failed to connect to %s:%d ret=%d", host, port, ret);
        esp_tls_conn_destroy(tls_client_);
        tls_client_ = nullptr;
        return false;
    }

    connected_ = true;
    return true;
}

void TlsTransport::Disconnect() {
    if (tls_client_) {
        esp_tls_conn_destroy(tls_client_);
        tls_client_ = nullptr;
    }
    connected_ = false;
}

int TlsTransport::Send(const char* data, size_t length) {
    if (!tls_client_ || !connected_) {
        return -1;
    }
    int ret = esp_tls_conn_write(tls_client_, data, length);
    if (ret == ESP_TLS_ERR_SSL_WANT_WRITE) {
        vTaskDelay(1);
        return 0;
    }
    if (ret <= 0) {
        connected_ = false;
        ESP_LOGE(TAG, "TLS发送失败: %d", ret);
    }
    return ret;
}

int TlsTransport::Receive(char* buffer, size_t bufferSize) {
    if (!tls_client_ || !connected_) {
        return -1;
    }
    int ret = 0;
    do {
        if (!tls_client_ || !connected_) {
            return -1;
        }
        ret = esp_tls_conn_read(tls_client_, buffer, bufferSize);
        if (ret == ESP_TLS_ERR_SSL_WANT_READ) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } while (ret == ESP_TLS_ERR_SSL_WANT_READ);

    if (ret == 0) {
        connected_ = false;
    } else if (ret < 0) {
        ESP_LOGE(TAG, "TLS读取失败: %d", ret);
        connected_ = false;
    }
    return ret;
}
