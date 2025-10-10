#include "wifi_manager.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <cstring>

#include "settings.h"

namespace {

constexpr const char *TAG = "wifi_mgr";

EventGroupHandle_t s_wifi_event_group = nullptr;

constexpr int WIFI_CONNECTED_BIT = BIT0;
constexpr int WIFI_FAIL_BIT = BIT1;

int s_retry_count = 0;
static bool s_sta_connected = false;

void handle_sta_events(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < 5) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "Retry Wi-Fi connection (%d)", s_retry_count);
        } else if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        s_sta_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

esp_err_t start_ap_mode() {
    ESP_LOGI(TAG, "Starting Wi-Fi in AP mode");

    s_sta_connected = false;

    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {};
    std::strncpy(reinterpret_cast<char *>(ap_config.ap.ssid), WIFI_AP_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = std::strlen(WIFI_AP_SSID);
    std::strncpy(reinterpret_cast<char *>(ap_config.ap.password), WIFI_AP_PASSWORD, sizeof(ap_config.ap.password));
    ap_config.ap.channel = WIFI_AP_CHANNEL;
    ap_config.ap.max_connection = WIFI_AP_MAX_CONN;
    if (std::strlen(WIFI_AP_PASSWORD) < 8) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    }
    ap_config.ap.ssid_hidden = 0;
    ap_config.ap.beacon_interval = 100;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "set AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start AP");

    ESP_LOGI(TAG, "AP started: SSID='%s' password='%s' channel=%d", WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL);
    return ESP_OK;
}

esp_err_t start_sta_mode() {
    ESP_LOGI(TAG, "Starting Wi-Fi in STA mode, connecting to '%s'", WIFI_STA_SSID);

    s_sta_connected = false;

    esp_netif_create_default_wifi_sta();

    wifi_config_t sta_config = {};
    std::strncpy(reinterpret_cast<char *>(sta_config.sta.ssid), WIFI_STA_SSID, sizeof(sta_config.sta.ssid));
    std::strncpy(reinterpret_cast<char *>(sta_config.sta.password), WIFI_STA_PASSWORD, sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "set STA config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start STA");

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(WIFI_STA_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID '%s'", WIFI_STA_SSID);
        s_sta_connected = true;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Failed to connect to SSID '%s'", WIFI_STA_SSID);
    ESP_ERROR_CHECK(esp_wifi_stop());
    s_sta_connected = false;
    return ESP_FAIL;
}

}  // namespace

esp_err_t wifi_manager_start(void) {
    static bool s_initialized = false;
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &handle_sta_events, nullptr), TAG, "register wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &handle_sta_events, nullptr), TAG, "register ip handler");

    esp_err_t err = ESP_FAIL;

#if KEYER_WIFI_MODE == KEYER_WIFI_MODE_AP
    err = start_ap_mode();
    s_initialized = (err == ESP_OK);
    return err;
#else
    err = start_sta_mode();
    if (err == ESP_OK) {
        s_initialized = true;
        return ESP_OK;
    }

#if WIFI_STA_FALLBACK_TO_AP
    ESP_LOGW(TAG, "Falling back to AP mode");
    err = start_ap_mode();
    s_initialized = (err == ESP_OK);
    s_sta_connected = false;
    return err;
#else
    ESP_LOGE(TAG, "STA mode failed and fallback disabled");
    return err;
#endif
#endif
}

bool wifi_manager_sta_connected(void) {
    return s_sta_connected;
}
