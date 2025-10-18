#include "web_server.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "config.h"
#include "config_store.h"
#include "keyer_logic.h"
#include "morse_decoder.h"
#include "timeline_buffer.h"
#include "tone_generator.h"
#include "cwnet_client.h"

namespace {

constexpr const char *TAG = "web_server";
constexpr const char *kBasePath = "/www";
constexpr TickType_t kWsTaskDelayMs = pdMS_TO_TICKS(100);

struct WebServerContext {
    httpd_handle_t server = nullptr;
   KeyerLogic *keyer = nullptr;
    tone_generator_t *tone = nullptr;
    MorseDecoder *decoder = nullptr;
    TimelineBuffer *timeline = nullptr;
    TaskHandle_t ws_task = nullptr;
    std::vector<int> clients;
    SemaphoreHandle_t clients_mutex = nullptr;
};

WebServerContext s_ctx;

#if ENABLE_HTTPD_LOG_SUPPRESSION
static void httpd_event_swallow(void *handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)handler_arg;
    (void)event_base;
    (void)event_id;
    (void)event_data;
}

void tune_httpd_log_levels() {
    static const char *kHttpdTags[] = {
        "httpd",
        "httpd_parse",
        "httpd_uri",
        "httpd_txrx",
        "httpd_sess",
        "httpd_ws"
    };
    for (const char *tag : kHttpdTags) {
        esp_log_level_set(tag, ESP_LOG_WARN);
    }
}

void install_httpd_event_handler() {
    static bool s_registered = false;
    if (s_registered) {
        return;
    }
    esp_err_t err = esp_event_handler_register(ESP_HTTP_SERVER_EVENT, ESP_EVENT_ANY_ID,
                                               &httpd_event_swallow, nullptr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register HTTP server event handler: %s", esp_err_to_name(err));
        return;
    }
    s_registered = true;
}
#else
inline void tune_httpd_log_levels() {}
inline void install_httpd_event_handler() {}
#endif

std::string get_content_type(const std::string &path) {
    auto ends_with = [](const std::string &value, const char *suffix) {
        size_t len = std::strlen(suffix);
        return value.length() >= len && value.compare(value.length() - len, len, suffix) == 0;
    };

    if (ends_with(path, ".html")) return "text/html";
    if (ends_with(path, ".css")) return "text/css";
    if (ends_with(path, ".js")) return "application/javascript";
    if (ends_with(path, ".json")) return "application/json";
    if (ends_with(path, ".png")) return "image/png";
    if (ends_with(path, ".svg")) return "image/svg+xml";
    if (ends_with(path, ".ico")) return "image/x-icon";
    return "text/plain";
}

void lock_clients() {
    if (s_ctx.clients_mutex) {
        xSemaphoreTake(s_ctx.clients_mutex, portMAX_DELAY);
    }
}

void unlock_clients() {
    if (s_ctx.clients_mutex) {
        xSemaphoreGive(s_ctx.clients_mutex);
    }
}

void add_client(int fd) {
    lock_clients();
    if (std::find(s_ctx.clients.begin(), s_ctx.clients.end(), fd) == s_ctx.clients.end()) {
        s_ctx.clients.push_back(fd);
        ESP_LOGI(TAG, "WebSocket client %d connected (total %zu)", fd, s_ctx.clients.size());
    }
    unlock_clients();
}

void remove_client(int fd) {
    lock_clients();
    auto it = std::remove(s_ctx.clients.begin(), s_ctx.clients.end(), fd);
    if (it != s_ctx.clients.end()) {
        s_ctx.clients.erase(it, s_ctx.clients.end());
        ESP_LOGI(TAG, "WebSocket client %d disconnected (total %zu)", fd, s_ctx.clients.size());
    }
    unlock_clients();
}

esp_err_t send_json(httpd_req_t *req, cJSON *root) {
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON error");
    }
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON print error");
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
    free(payload);
    return err;
}

esp_err_t send_error_json(httpd_req_t *req, int status, const char *message) {
    const char *status_str = HTTPD_500;
    switch (status) {
        case 400: status_str = HTTPD_400; break;
        case 404: status_str = HTTPD_404; break;
        case 500: status_str = HTTPD_500; break;
        default: break;
    }
    httpd_resp_set_status(req, status_str);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "message", message);
    return send_json(req, root);
}

const char *state_to_string(KeyerState_t state) {
    switch (state) {
        case KEYER_IDLE: return "IDLE";
        case KEYER_DOT_ACTIVE: return "DOT_ACTIVE";
        case KEYER_DASH_ACTIVE: return "DASH_ACTIVE";
        case KEYER_INTER_ELEMENT: return "INTER_ELEMENT";
        case KEYER_INTER_CHAR: return "INTER_CHAR";
        default: return "UNKNOWN";
    }
}

cJSON *build_status_json() {
    cJSON *root = cJSON_CreateObject();
    if (!root || !s_ctx.keyer) {
        return root;
    }

    cJSON_AddNumberToObject(root, "wpm", s_ctx.keyer->getWPM());
    cJSON_AddNumberToObject(root, "mode", s_ctx.keyer->getMode());
    cJSON_AddStringToObject(root, "state", state_to_string(s_ctx.keyer->getState()));
    cJSON_AddBoolToObject(root, "keying", s_ctx.keyer->isKeying());
    cJSON_AddBoolToObject(root, "dot_pressed", s_ctx.keyer->getDotPressed());
    cJSON_AddBoolToObject(root, "dash_pressed", s_ctx.keyer->getDashPressed());
    cJSON_AddNumberToObject(root, "dot_isr_count", s_ctx.keyer->getDotISRCount());
    cJSON_AddNumberToObject(root, "dash_isr_count", s_ctx.keyer->getDashISRCount());

    if (s_ctx.decoder) {
        cJSON *decoder = cJSON_AddObjectToObject(root, "decoder");
        cJSON_AddNumberToObject(decoder, "char_spaces", s_ctx.decoder->getCharSpacesDetected());
        cJSON_AddNumberToObject(decoder, "word_spaces", s_ctx.decoder->getWordSpacesDetected());
    }

    if (s_ctx.timeline) {
        cJSON *stats = cJSON_AddObjectToObject(root, "timeline");
        cJSON_AddNumberToObject(stats, "available", s_ctx.timeline->available());
        cJSON_AddNumberToObject(stats, "total_pushed", s_ctx.timeline->getTotalPushed());
        cJSON_AddNumberToObject(stats, "total_dropped", s_ctx.timeline->getTotalDropped());
        cJSON_AddNumberToObject(stats, "overruns", s_ctx.timeline->getOverruns());
    }

    if (s_ctx.tone) {
        cJSON *tone = cJSON_AddObjectToObject(root, "tone");
        cJSON_AddNumberToObject(tone, "frequency", tone_generator_get_frequency(s_ctx.tone));
        cJSON_AddNumberToObject(tone, "volume", tone_generator_get_volume(s_ctx.tone));
    }

    return root;
}

cJSON *build_config_json() {
    cJSON *root = cJSON_CreateObject();
    if (!root || !s_ctx.keyer || !s_ctx.tone) {
        return root;
    }

    cJSON_AddNumberToObject(root, "wpm", s_ctx.keyer->getWPM());
    cJSON_AddNumberToObject(root, "mode", s_ctx.keyer->getMode());
    uint8_t up = 0, down = 0;
    s_ctx.keyer->getMemoryWindow(&up, &down);
    cJSON_AddNumberToObject(root, "window_up", up);
    cJSON_AddNumberToObject(root, "window_down", down);
    cJSON_AddNumberToObject(root, "debounce", s_ctx.keyer->getDebounce());
    cJSON_AddNumberToObject(root, "volume", tone_generator_get_volume(s_ctx.tone));
    cJSON_AddNumberToObject(root, "frequency", tone_generator_get_frequency(s_ctx.tone));
    cJSON_AddNumberToObject(root, "fade_in_ms", tone_generator_get_fade_in_ms(s_ctx.tone));
    cJSON_AddNumberToObject(root, "fade_out_ms", tone_generator_get_fade_out_ms(s_ctx.tone));

    if (s_ctx.decoder) {
        uint16_t char_extra_tenths = s_ctx.decoder->getCharSpaceToleranceDots();
        uint16_t word_extra_tenths = s_ctx.decoder->getWordSpaceToleranceDots();
        cJSON_AddNumberToObject(root, "char_space_tolerance_dots",
                                 static_cast<int>(char_extra_tenths / 10));
        cJSON_AddNumberToObject(root, "word_space_tolerance_dots",
                                 static_cast<int>(word_extra_tenths / 10));
    }

    return root;
}

esp_err_t handle_get_status(httpd_req_t *req) {
    return send_json(req, build_status_json());
}

esp_err_t handle_get_config(httpd_req_t *req) {
    return send_json(req, build_config_json());
}

esp_err_t handle_post_config(httpd_req_t *req) {
    if (!s_ctx.keyer || !s_ctx.tone) {
        return send_error_json(req, 500, "Subsystem not ready");
    }

    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 4096) {
        return send_error_json(req, 400, "Invalid body length");
    }

    std::string body;
    body.resize(total_len);
    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, body.data() + received, total_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return send_error_json(req, 500, "Failed to receive body");
        }
        received += ret;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) {
        return send_error_json(req, 400, "Invalid JSON");
    }

    bool modified = false;
    double raw_char_tol = -1.0;
    double raw_word_tol = -1.0;
    double raw_fade_in = -1.0;
    double raw_fade_out = -1.0;

    auto get_number = [&](const char *name) -> cJSON * {
        cJSON *item = cJSON_GetObjectItem(root, name);
        return (item && cJSON_IsNumber(item)) ? item : nullptr;
    };

    if (cJSON *item = get_number("wpm")) {
        uint8_t wpm = static_cast<uint8_t>(item->valuedouble);
        s_ctx.keyer->setWPM(wpm);
        modified = true;
    }
    if (cJSON *item = get_number("mode")) {
        uint8_t mode = static_cast<uint8_t>(item->valuedouble);
        s_ctx.keyer->setMode(mode);
        modified = true;
    }
    if (cJSON *up = get_number("window_up")) {
        uint8_t up_val = static_cast<uint8_t>(up->valuedouble);
        uint8_t down_val = 0;
        s_ctx.keyer->getMemoryWindow(nullptr, &down_val);
        if (cJSON *down = get_number("window_down")) {
            down_val = static_cast<uint8_t>(down->valuedouble);
        }
        s_ctx.keyer->setMemoryWindow(up_val, down_val);
        modified = true;
    } else if (cJSON *down = get_number("window_down")) {
        uint8_t up_val = 0;
        s_ctx.keyer->getMemoryWindow(&up_val, nullptr);
        s_ctx.keyer->setMemoryWindow(up_val, static_cast<uint8_t>(down->valuedouble));
        modified = true;
    }
    if (cJSON *item = get_number("debounce")) {
        s_ctx.keyer->setDebounce(static_cast<uint32_t>(item->valuedouble));
        modified = true;
    }
    uint16_t new_fade_in = tone_generator_get_fade_in_ms(s_ctx.tone);
    uint16_t new_fade_out = tone_generator_get_fade_out_ms(s_ctx.tone);
    bool fade_changed = false;

    if (cJSON *item = get_number("volume")) {
        tone_generator_set_volume(s_ctx.tone, static_cast<uint8_t>(item->valuedouble));
        modified = true;
    }
    if (cJSON *item = get_number("frequency")) {
        tone_generator_set_frequency(s_ctx.tone, static_cast<uint16_t>(item->valuedouble));
        modified = true;
    }
    if (cJSON *item = get_number("fade_in_ms")) {
        new_fade_in = static_cast<uint16_t>(item->valuedouble);
        fade_changed = true;
        raw_fade_in = item->valuedouble;
    }
    if (cJSON *item = get_number("fade_out_ms")) {
        new_fade_out = static_cast<uint16_t>(item->valuedouble);
        fade_changed = true;
        raw_fade_out = item->valuedouble;
    }
    if (s_ctx.decoder) {
        if (cJSON *item = get_number("char_space_tolerance_dots")) {
            int extra_dots = static_cast<int>(std::lround(item->valuedouble));
            if (extra_dots < 0) {
                extra_dots = 0;
            }
            uint16_t tenths = static_cast<uint16_t>(extra_dots * 10);
            s_ctx.decoder->setCharSpaceToleranceDots(tenths);
            modified = true;
            raw_char_tol = item->valuedouble;
        }
        if (cJSON *item = get_number("word_space_tolerance_dots")) {
            int extra_dots = static_cast<int>(std::lround(item->valuedouble));
            if (extra_dots < 0) {
                extra_dots = 0;
            }
            uint16_t tenths = static_cast<uint16_t>(extra_dots * 10);
            s_ctx.decoder->setWordSpaceToleranceDots(tenths);
            modified = true;
            raw_word_tol = item->valuedouble;
        }
    }

    if (fade_changed) {
        tone_generator_set_fade(s_ctx.tone, new_fade_in, new_fade_out);
        modified = true;
        ESP_LOGI(TAG, "POST config: fade_in=%.1f fade_out=%.1f (applied %u/%u)", raw_fade_in, raw_fade_out, new_fade_in, new_fade_out);
    }
    if (raw_char_tol >= 0.0 || raw_word_tol >= 0.0) {
        ESP_LOGI(TAG, "POST config: char_tol=+%d dots, word_tol=+%d dots",
                 raw_char_tol >= 0.0 ? static_cast<int>(std::lround(raw_char_tol)) : -1,
                 raw_word_tol >= 0.0 ? static_cast<int>(std::lround(raw_word_tol)) : -1);
    }

    cJSON_Delete(root);

    if (modified && s_ctx.decoder && s_ctx.keyer) {
        s_ctx.decoder->setDotDuration(s_ctx.keyer->getDotDuration());
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", modified);
    cJSON_AddStringToObject(response, "message", modified ? "Configuration updated" : "No valid parameters");
    return send_json(req, response);
}

esp_err_t handle_post_save(httpd_req_t *req) {
    persistent_config_t snapshot;
    config_store_snapshot_from_runtime(&snapshot, s_ctx.keyer, s_ctx.tone, s_ctx.decoder);
    esp_err_t err = config_store_save(&snapshot);
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", err == ESP_OK);
    cJSON_AddStringToObject(response, "message", err == ESP_OK ? "Configuration saved" : esp_err_to_name(err));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(err));
    }
    return send_json(req, response);
}

esp_err_t handle_post_reset(httpd_req_t *req) {
    persistent_config_t defaults;
    config_store_set_defaults(&defaults);
    config_store_apply_to_runtime(&defaults, s_ctx.keyer, s_ctx.tone, s_ctx.decoder);
    if (s_ctx.decoder && s_ctx.keyer) {
        s_ctx.decoder->setDotDuration(s_ctx.keyer->getDotDuration());
    }
    esp_err_t err = config_store_save(&defaults);
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", err == ESP_OK);
    cJSON_AddStringToObject(response, "message", err == ESP_OK ? "Configuration reset" : esp_err_to_name(err));
    return send_json(req, response);
}

// Forward declaration - defined in app_main.cpp
extern "C" {
    extern cwnet_client_t g_remotecw_client;
    extern bool g_remotecw_enabled;
}

// RemoteCW configuration endpoints
esp_err_t handle_get_remotecw_config(httpd_req_t *req) {
    persistent_config_t cfg;
    bool loaded = false;
    config_store_load(&cfg, &loaded);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", cfg.remotecw_enabled);
    cJSON_AddStringToObject(root, "server_ip", cfg.remotecw_server_ip);
    cJSON_AddNumberToObject(root, "server_port", cfg.remotecw_server_port);
    cJSON_AddStringToObject(root, "username", cfg.remotecw_username);
    cJSON_AddStringToObject(root, "callsign", cfg.remotecw_callsign);

    // Add runtime status if enabled
    if (g_remotecw_enabled) {
        cJSON *status = cJSON_AddObjectToObject(root, "status");
        cwnet_state_t state = cwnet_client_get_state(&g_remotecw_client);
        const char *state_str = "UNKNOWN";
        switch (state) {
            case CWNET_STATE_DISCONNECTED: state_str = "DISCONNECTED"; break;
            case CWNET_STATE_CONNECTING: state_str = "CONNECTING"; break;
            case CWNET_STATE_CONNECTED: state_str = "CONNECTED"; break;
            case CWNET_STATE_LOGIN_SENT: state_str = "LOGIN_SENT"; break;
            case CWNET_STATE_LOGIN_CONFIRMED: state_str = "LOGIN_CONFIRMED"; break;
            case CWNET_STATE_ERROR: state_str = "ERROR"; break;
        }
        cJSON_AddStringToObject(status, "state", state_str);
        cJSON_AddBoolToObject(status, "can_transmit", cwnet_client_can_transmit(&g_remotecw_client));
        int latency = cwnet_client_get_latency_ms(&g_remotecw_client);
        if (latency >= 0) {
            cJSON_AddNumberToObject(status, "latency_ms", latency);
        }
    }

    return send_json(req, root);
}

esp_err_t handle_post_remotecw_config(httpd_req_t *req) {
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 2048) {
        return send_error_json(req, 400, "Invalid body length");
    }

    std::string body;
    body.resize(total_len);
    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, body.data() + received, total_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return send_error_json(req, 500, "Failed to receive body");
        }
        received += ret;
    }

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) {
        return send_error_json(req, 400, "Invalid JSON");
    }

    // Load current config
    persistent_config_t cfg;
    bool loaded = false;
    config_store_load(&cfg, &loaded);

    bool modified = false;

    // Update RemoteCW fields from JSON
    cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    if (enabled && cJSON_IsBool(enabled)) {
        cfg.remotecw_enabled = cJSON_IsTrue(enabled);
        modified = true;
    }

    cJSON *server_ip = cJSON_GetObjectItem(root, "server_ip");
    if (server_ip && cJSON_IsString(server_ip)) {
        strncpy(cfg.remotecw_server_ip, server_ip->valuestring, sizeof(cfg.remotecw_server_ip) - 1);
        cfg.remotecw_server_ip[sizeof(cfg.remotecw_server_ip) - 1] = '\0';
        modified = true;
    }

    cJSON *server_port = cJSON_GetObjectItem(root, "server_port");
    if (server_port && cJSON_IsNumber(server_port)) {
        cfg.remotecw_server_port = static_cast<uint16_t>(server_port->valuedouble);
        modified = true;
    }

    cJSON *username = cJSON_GetObjectItem(root, "username");
    if (username && cJSON_IsString(username)) {
        strncpy(cfg.remotecw_username, username->valuestring, sizeof(cfg.remotecw_username) - 1);
        cfg.remotecw_username[sizeof(cfg.remotecw_username) - 1] = '\0';
        modified = true;
    }

    cJSON *callsign = cJSON_GetObjectItem(root, "callsign");
    if (callsign && cJSON_IsString(callsign)) {
        strncpy(cfg.remotecw_callsign, callsign->valuestring, sizeof(cfg.remotecw_callsign) - 1);
        cfg.remotecw_callsign[sizeof(cfg.remotecw_callsign) - 1] = '\0';
        modified = true;
    }

    cJSON_Delete(root);

    // Save configuration
    esp_err_t err = ESP_OK;
    if (modified) {
        err = config_store_save(&cfg);
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", modified && err == ESP_OK);
    cJSON_AddStringToObject(response, "message",
        modified ? (err == ESP_OK ? "RemoteCW configuration saved" : esp_err_to_name(err))
                 : "No valid parameters");

    ESP_LOGI(TAG, "RemoteCW config updated: enabled=%d, ip=%s, port=%u",
             cfg.remotecw_enabled, cfg.remotecw_server_ip, cfg.remotecw_server_port);

    return send_json(req, response);
}

esp_err_t handle_post_remotecw_test(httpd_req_t *req) {
    int total_len = req->content_len;
    ESP_LOGI(TAG, "RemoteCW test: content_len=%d", total_len);

    if (total_len <= 0 || total_len > 2048) {
        ESP_LOGE(TAG, "RemoteCW test: invalid body length %d", total_len);
        return send_error_json(req, 400, "Invalid body length");
    }

    std::string body;
    body.resize(total_len);
    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, body.data() + received, total_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "RemoteCW test: failed to receive body");
            return send_error_json(req, 500, "Failed to receive body");
        }
        received += ret;
    }

    ESP_LOGI(TAG, "RemoteCW test: received JSON: %s", body.c_str());

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) {
        ESP_LOGE(TAG, "RemoteCW test: invalid JSON");
        return send_error_json(req, 400, "Invalid JSON");
    }

    // Extract server IP and port from JSON
    cJSON *server_ip_json = cJSON_GetObjectItem(root, "server_ip");
    cJSON *server_port_json = cJSON_GetObjectItem(root, "server_port");

    ESP_LOGI(TAG, "RemoteCW test: server_ip=%p, server_port=%p", server_ip_json, server_port_json);

    if (!server_ip_json || !cJSON_IsString(server_ip_json)) {
        ESP_LOGE(TAG, "RemoteCW test: invalid server_ip");
        cJSON_Delete(root);
        return send_error_json(req, 400, "Missing or invalid server_ip");
    }

    if (!server_port_json || !cJSON_IsNumber(server_port_json)) {
        ESP_LOGE(TAG, "RemoteCW test: invalid server_port");
        cJSON_Delete(root);
        return send_error_json(req, 400, "Missing or invalid server_port");
    }

    // Copy strings before deleting JSON object
    std::string server_ip = server_ip_json->valuestring;
    uint16_t server_port = static_cast<uint16_t>(server_port_json->valuedouble);

    cJSON_Delete(root);

    // Attempt TCP connection to RemoteCW server
    ESP_LOGI(TAG, "Testing connection to %s:%u", server_ip.c_str(), server_port);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return send_error_json(req, 500, "Failed to create socket");
    }

    // Set connection timeout (5 seconds)
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(server_port);

    int ret = inet_pton(AF_INET, server_ip.c_str(), &dest_addr.sin_addr);
    if (ret != 1) {
        close(sock);
        ESP_LOGE(TAG, "Invalid IP address: %s", server_ip.c_str());
        return send_error_json(req, 400, "Invalid IP address format");
    }

    // Try to connect
    int64_t start_time = esp_timer_get_time();
    ret = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    int64_t connect_time = (esp_timer_get_time() - start_time) / 1000; // Convert to ms

    cJSON *response = cJSON_CreateObject();

    if (ret == 0) {
        // Connection successful
        ESP_LOGI(TAG, "Connection successful to %s:%u (time: %lld ms)", server_ip.c_str(), server_port, connect_time);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "Connection successful");
        cJSON_AddNumberToObject(response, "connect_time_ms", connect_time);
        close(sock);
    } else {
        // Connection failed
        ESP_LOGW(TAG, "Connection failed to %s:%u: errno %d", server_ip.c_str(), server_port, errno);
        const char *error_msg;
        switch (errno) {
            case ETIMEDOUT:
                error_msg = "Connection timed out - server not responding";
                break;
            case ECONNREFUSED:
                error_msg = "Connection refused - server not accepting connections";
                break;
            case EHOSTUNREACH:
                error_msg = "Host unreachable - check IP address and network";
                break;
            case ENETUNREACH:
                error_msg = "Network unreachable";
                break;
            default:
                error_msg = "Connection failed";
                break;
        }
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", error_msg);
        cJSON_AddNumberToObject(response, "errno", errno);
        close(sock);
    }

    return send_json(req, response);
}

// RemoteCW configuration HTML page (embedded)
esp_err_t handle_remotecw_html(httpd_req_t *req) {
    const char *html = R"HTML(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RemoteCW Configuration - IU3QEZ Keyer</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 800px; margin: 20px auto; padding: 20px; background: #f5f5f5; }
        .container { background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h1 { color: #333; margin-top: 0; }
        .form-group { margin-bottom: 20px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; color: #555; }
        input[type="text"], input[type="number"] { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; font-size: 14px; }
        input[type="checkbox"] { width: 20px; height: 20px; cursor: pointer; }
        .checkbox-group { display: flex; align-items: center; gap: 10px; }
        .button-group { display: flex; gap: 10px; margin-top: 30px; }
        button { padding: 12px 24px; border: none; border-radius: 4px; cursor: pointer; font-size: 14px; font-weight: bold; }
        .btn-primary { background: #007bff; color: white; }
        .btn-primary:hover { background: #0056b3; }
        .btn-secondary { background: #6c757d; color: white; }
        .btn-secondary:hover { background: #545b62; }
        .btn-success { background: #28a745; color: white; }
        .btn-success:hover { background: #218838; }
        .message { padding: 12px; margin-top: 20px; border-radius: 4px; display: none; }
        .message.success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .message.error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
        .info-box { background: #e7f3ff; padding: 15px; border-left: 4px solid #007bff; margin-bottom: 20px; }
        .status { font-size: 12px; color: #666; margin-top: 5px; }
        .back-link { display: inline-block; margin-bottom: 20px; color: #007bff; text-decoration: none; }
        .back-link:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <div class="container">
        <a href="/" class="back-link">&larr; Back to Main</a>
        <h1>RemoteCW Network Configuration</h1>

        <div class="info-box">
            <strong>RemoteCW Protocol</strong><br>
            Connect this keyer to a RemoteCW server (DL4YHF protocol, port 7355) to transmit morse code remotely over TCP/IP.
            Requires network connectivity and a running RemoteCW server.
        </div>

        <form id="configForm">
            <div class="form-group">
                <div class="checkbox-group">
                    <input type="checkbox" id="enabled" name="enabled">
                    <label for="enabled">Enable RemoteCW Client</label>
                </div>
                <div class="status" id="statusText">Client disabled</div>
            </div>

            <div class="form-group">
                <label for="server_ip">Server IP Address</label>
                <input type="text" id="server_ip" name="server_ip" placeholder="192.168.1.100" required>
            </div>

            <div class="form-group">
                <label for="server_port">Server Port</label>
                <input type="number" id="server_port" name="server_port" min="1" max="65535" value="7355" required>
            </div>

            <div class="form-group">
                <label for="username">Username</label>
                <input type="text" id="username" name="username" placeholder="IU3QEZ_ESP32" maxlength="80" required>
            </div>

            <div class="form-group">
                <label for="callsign">Callsign</label>
                <input type="text" id="callsign" name="callsign" placeholder="IU3QEZ" maxlength="80" required>
            </div>

            <div class="button-group">
                <button type="submit" class="btn-primary">Save Configuration</button>
                <button type="button" class="btn-secondary" onclick="loadConfig()">Reload</button>
                <button type="button" class="btn-success" onclick="testConnection()">Test Connection</button>
            </div>
        </form>

        <div id="message" class="message"></div>
    </div>

    <script>
        function showMessage(text, isError = false) {
            const msg = document.getElementById('message');
            msg.textContent = text;
            msg.className = 'message ' + (isError ? 'error' : 'success');
            msg.style.display = 'block';
            setTimeout(() => { msg.style.display = 'none'; }, 5000);
        }

        function updateStatus() {
            const enabled = document.getElementById('enabled').checked;
            const statusText = document.getElementById('statusText');
            statusText.textContent = enabled ? 'Client enabled - will connect on next boot' : 'Client disabled';
            statusText.style.color = enabled ? '#28a745' : '#666';
        }

        async function loadConfig() {
            try {
                const response = await fetch('/api/remotecw');
                if (!response.ok) throw new Error('Failed to load configuration');

                const config = await response.json();
                document.getElementById('enabled').checked = config.enabled;
                document.getElementById('server_ip').value = config.server_ip;
                document.getElementById('server_port').value = config.server_port;
                document.getElementById('username').value = config.username;
                document.getElementById('callsign').value = config.callsign;

                // Update status with runtime info if available
                const statusText = document.getElementById('statusText');
                if (config.status) {
                    const state = config.status.state;
                    const canTx = config.status.can_transmit;
                    const latency = config.status.latency_ms;
                    let statusMsg = `State: ${state}`;
                    if (canTx) statusMsg += ' ✓ TX';
                    if (latency) statusMsg += ` (${latency}ms)`;
                    statusText.textContent = statusMsg;
                    statusText.style.color = canTx ? '#28a745' : '#dc3545';
                } else {
                    updateStatus();
                }

                showMessage('Configuration loaded successfully');
            } catch (error) {
                showMessage('Error loading configuration: ' + error.message, true);
            }
        }

        async function testConnection() {
            const ip = document.getElementById('server_ip').value;
            const port = parseInt(document.getElementById('server_port').value);

            if (!ip || !port) {
                showMessage('Please enter server IP and port first', true);
                return;
            }

            showMessage(`Testing connection to ${ip}:${port}...`, false);

            try {
                const response = await fetch('/api/remotecw/test', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ server_ip: ip, server_port: port })
                });

                const result = await response.json();

                if (result.success) {
                    const time = result.connect_time_ms !== undefined ? ` (${result.connect_time_ms} ms)` : '';
                    showMessage(`✓ ${result.message}${time}`, false);
                } else {
                    showMessage(`✗ ${result.message}`, true);
                }
            } catch (error) {
                showMessage('Error testing connection: ' + error.message, true);
            }
        }

        document.getElementById('configForm').addEventListener('submit', async (e) => {
            e.preventDefault();

            const config = {
                enabled: document.getElementById('enabled').checked,
                server_ip: document.getElementById('server_ip').value,
                server_port: parseInt(document.getElementById('server_port').value),
                username: document.getElementById('username').value,
                callsign: document.getElementById('callsign').value
            };

            try {
                const response = await fetch('/api/remotecw', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(config)
                });

                const result = await response.json();

                if (result.success) {
                    showMessage('Configuration saved! Restart required to apply changes.');
                    updateStatus();
                } else {
                    showMessage('Error: ' + result.message, true);
                }
            } catch (error) {
                showMessage('Error saving configuration: ' + error.message, true);
            }
        });

        document.getElementById('enabled').addEventListener('change', updateStatus);

        // Load configuration on page load
        window.addEventListener('load', loadConfig);
    </script>
</body>
</html>)HTML";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t static_file_handler(httpd_req_t *req) {
    std::string uri = req->uri;
    if (uri == "/") {
        uri = "/index.html";
    }

    std::string path = std::string(kBasePath) + uri;

    struct stat st;
    if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
    }

    FILE *file = fopen(path.c_str(), "rb");
    if (!file) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
    }

    httpd_resp_set_type(req, get_content_type(path).c_str());
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=600");

    char buffer[512];
    size_t read_bytes = 0;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        esp_err_t err = httpd_resp_send_chunk(req, buffer, read_bytes);
        if (err != ESP_OK) {
            fclose(file);
            return err;
        }
    }
    fclose(file);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t websocket_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }
    if (frame.len) {
        std::vector<uint8_t> buffer(frame.len + 1);
        frame.payload = buffer.data();
        err = httpd_ws_recv_frame(req, &frame, frame.len);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        remove_client(httpd_req_to_sockfd(req));
    }
    return ESP_OK;
}

void broadcast_payload(const char *payload, size_t length) {
    if (!payload || length == 0 || !s_ctx.server) {
        return;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(payload));
    frame.len = length;

    lock_clients();
    for (auto it = s_ctx.clients.begin(); it != s_ctx.clients.end();) {
        esp_err_t err = httpd_ws_send_frame_async(s_ctx.server, *it, &frame);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Removing WS client %d due to send error %s", *it, esp_err_to_name(err));
            it = s_ctx.clients.erase(it);
        } else {
            ++it;
        }
    }
    unlock_clients();
}

cJSON *event_to_json(const TimelineEvent &event) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return nullptr;
    }
    cJSON_AddNumberToObject(obj, "ts", event.timestamp_us);

    if (event.type_extended != 0) {
        if (event.type_extended & EVENT_ELEMENT_DOT) {
            cJSON_AddStringToObject(obj, "type", "ELEMENT_DOT");
        } else if (event.type_extended & EVENT_ELEMENT_DASH) {
            cJSON_AddStringToObject(obj, "type", "ELEMENT_DASH");
        } else if (event.type_extended & EVENT_DECODED_CHAR) {
            cJSON_AddStringToObject(obj, "type", "DECODED_CHAR");
            char ch[2] = { static_cast<char>(event.payload), '\0' };
            cJSON_AddStringToObject(obj, "char", ch);
        }
        return obj;
    }

    const char *type_str = "UNKNOWN";
    switch (event.type) {
        case EVENT_DOT_PRESS: type_str = "DOT_PRESS"; break;
        case EVENT_DOT_RELEASE: type_str = "DOT_RELEASE"; break;
        case EVENT_DASH_PRESS: type_str = "DASH_PRESS"; break;
        case EVENT_DASH_RELEASE: type_str = "DASH_RELEASE"; break;
        case EVENT_KEY_ON: type_str = "KEY_ON"; break;
        case EVENT_KEY_OFF: type_str = "KEY_OFF"; break;
        case EVENT_SPACE_CHAR: type_str = "SPACE_CHAR"; break;
        case EVENT_SPACE_WORD: type_str = "SPACE_WORD"; break;
        default: break;
    }
    cJSON_AddStringToObject(obj, "type", type_str);

    if (event.flags != FLAG_NONE) {
        cJSON *flags = cJSON_AddArrayToObject(obj, "flags");
        if (event.flags & FLAG_IAMBIC) {
            cJSON_AddItemToArray(flags, cJSON_CreateString("IAMBIC"));
        }
        if (event.flags & FLAG_MEMORY_LATCH) {
            cJSON_AddItemToArray(flags, cJSON_CreateString("MEMORY_LATCH"));
        }
        if (event.flags & FLAG_DEBOUNCE_SKIP) {
            cJSON_AddItemToArray(flags, cJSON_CreateString("DEBOUNCE_SKIP"));
        }
    }
    return obj;
}

void websocket_task(void *param) {
    const size_t kMaxEvents = 64;
    TimelineEvent events[kMaxEvents];

    while (true) {
        if (!s_ctx.timeline) {
            vTaskDelay(kWsTaskDelayMs);
            continue;
        }

        lock_clients();
        bool has_clients = !s_ctx.clients.empty();
        unlock_clients();
        if (!has_clients) {
            vTaskDelay(kWsTaskDelayMs);
            continue;
        }

        size_t count = s_ctx.timeline->read(events, kMaxEvents);
        if (count == 0) {
            vTaskDelay(kWsTaskDelayMs);
            continue;
        }

        cJSON *root = cJSON_CreateObject();
        cJSON *array = cJSON_AddArrayToObject(root, "events");
        for (size_t i = 0; i < count; ++i) {
            cJSON *evt = event_to_json(events[i]);
            if (evt) {
                cJSON_AddItemToArray(array, evt);
            }
        }
        if (s_ctx.keyer) {
            cJSON_AddNumberToObject(root, "wpm", s_ctx.keyer->getWPM());
        }
        cJSON *stats = cJSON_AddObjectToObject(root, "buffer_stats");
        cJSON_AddNumberToObject(stats, "total_pushed", s_ctx.timeline->getTotalPushed());
        cJSON_AddNumberToObject(stats, "total_dropped", s_ctx.timeline->getTotalDropped());
        cJSON_AddNumberToObject(stats, "overruns", s_ctx.timeline->getOverruns());
        cJSON_AddNumberToObject(stats, "available", s_ctx.timeline->available());

        char *payload = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!payload) {
            continue;
        }
        broadcast_payload(payload, std::strlen(payload));
        free(payload);
    }
}

esp_err_t register_uri(httpd_handle_t server, const httpd_uri_t &uri) {
    esp_err_t err = httpd_register_uri_handler(server, &uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI %s: %s", uri.uri, esp_err_to_name(err));
    }
    return err;
}

esp_err_t start_http_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 11;  // Increase from default 8 to accommodate all endpoints
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_RETURN_ON_ERROR(httpd_start(&s_ctx.server, &config), TAG, "start httpd");

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = handle_get_status,
        .user_ctx = nullptr,
    };
    register_uri(s_ctx.server, status_uri);

    httpd_uri_t config_get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = handle_get_config,
        .user_ctx = nullptr,
    };
    register_uri(s_ctx.server, config_get_uri);

    httpd_uri_t config_post_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = handle_post_config,
        .user_ctx = nullptr,
    };
    register_uri(s_ctx.server, config_post_uri);

    httpd_uri_t save_uri = {
        .uri = "/api/config/save",
        .method = HTTP_POST,
        .handler = handle_post_save,
        .user_ctx = nullptr,
    };
    register_uri(s_ctx.server, save_uri);

    httpd_uri_t reset_uri = {
        .uri = "/api/config/reset",
        .method = HTTP_POST,
        .handler = handle_post_reset,
        .user_ctx = nullptr,
    };
    register_uri(s_ctx.server, reset_uri);

    httpd_uri_t remotecw_get_uri = {
        .uri = "/api/remotecw",
        .method = HTTP_GET,
        .handler = handle_get_remotecw_config,
        .user_ctx = nullptr,
    };
    register_uri(s_ctx.server, remotecw_get_uri);

    httpd_uri_t remotecw_post_uri = {
        .uri = "/api/remotecw",
        .method = HTTP_POST,
        .handler = handle_post_remotecw_config,
        .user_ctx = nullptr,
    };
    register_uri(s_ctx.server, remotecw_post_uri);

    httpd_uri_t remotecw_test_uri = {
        .uri = "/api/remotecw/test",
        .method = HTTP_POST,
        .handler = handle_post_remotecw_test,
        .user_ctx = nullptr,
    };
    register_uri(s_ctx.server, remotecw_test_uri);

    httpd_uri_t remotecw_html_uri = {
        .uri = "/remotecw.html",
        .method = HTTP_GET,
        .handler = handle_remotecw_html,
        .user_ctx = nullptr,
    };
    register_uri(s_ctx.server, remotecw_html_uri);

    httpd_uri_t ws_uri = {
        .uri = "/ws/timeline",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = nullptr,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    register_uri(s_ctx.server, ws_uri);

    httpd_uri_t static_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = nullptr,
    };
    register_uri(s_ctx.server, static_uri);

    return ESP_OK;
}

esp_err_t mount_spiffs() {
    static bool mounted = false;
    if (mounted) {
        return ESP_OK;
    }
    esp_vfs_spiffs_conf_t conf = {
        .base_path = kBasePath,
        .partition_label = "spiffs",
        .max_files = 16,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(err));
        return err;
    }
    mounted = true;
    size_t total = 0, used = 0;
    if (esp_spiffs_info(conf.partition_label, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted, size=%u, used=%u", static_cast<unsigned>(total), static_cast<unsigned>(used));
    }
    return ESP_OK;
}

}  // namespace

esp_err_t web_server_start(const web_server_config_t &config) {
    if (s_ctx.server) {
        ESP_LOGW(TAG, "Web server already started");
        return ESP_OK;
    }

    tune_httpd_log_levels();
    install_httpd_event_handler();

    s_ctx.keyer = config.keyer;
    s_ctx.tone = config.tone;
    s_ctx.decoder = config.decoder;
    s_ctx.timeline = config.timeline;
    s_ctx.clients_mutex = xSemaphoreCreateMutex();

    ESP_RETURN_ON_ERROR(mount_spiffs(), TAG, "mount SPIFFS");
    ESP_RETURN_ON_ERROR(start_http_server(), TAG, "start httpd");

    if (!s_ctx.ws_task) {
        BaseType_t created = xTaskCreatePinnedToCore(websocket_task, "ws_broadcast", 4096, nullptr, 4, &s_ctx.ws_task, tskNO_AFFINITY);
        if (created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create WebSocket task");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Web server started on port 80");
    return ESP_OK;
}
