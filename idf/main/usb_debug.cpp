#include "usb_debug.h"

#include <cstdio>
#include <cstring>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

#include "timeline_buffer.h"

namespace {

constexpr const char *TAG = "usb_debug";
constexpr size_t kUsbTaskStackSize = 4096;
constexpr UBaseType_t kUsbTaskPriority = 5;
constexpr BaseType_t kUsbTaskCore = tskNO_AFFINITY;

constexpr tinyusb_cdcacm_itf_t kMessagePort = TINYUSB_CDC_ACM_0;
constexpr tinyusb_cdcacm_itf_t kTimelinePort = TINYUSB_CDC_ACM_1;

TaskHandle_t s_usb_task_handle = nullptr;
bool s_cdc_ready[TINYUSB_CDC_ACM_MAX] = {false};

size_t boundedStrLen(const char *str, size_t max_len) {
    if (!str) {
        return 0;
    }
    size_t len = 0;
    while (len < max_len && str[len] != '\0') {
        ++len;
    }
    return len;
}

const char *eventTypeToString(TimelineEventType type) {
    switch (type) {
        case EVENT_DOT_PRESS:    return "DOT_PRESS";
        case EVENT_DOT_RELEASE:  return "DOT_RELEASE";
        case EVENT_DASH_PRESS:   return "DASH_PRESS";
        case EVENT_DASH_RELEASE: return "DASH_RELEASE";
        case EVENT_KEY_ON:       return "KEY_ON";
        case EVENT_KEY_OFF:      return "KEY_OFF";
        case EVENT_SPACE_CHAR:   return "SPACE_CHAR";
        case EVENT_SPACE_WORD:   return "SPACE_WORD";
        default:                 return "NONE";
    }
}

size_t formatFlags(TimelineEventFlags flags, char *out, size_t out_size) {
    if (out_size == 0) {
        return 0;
    }
    size_t len = 0;
    bool first = true;
    auto append_token = [&](const char *token) {
        if (len >= out_size) {
            return;
        }
        if (!first) {
            if (len + 1 < out_size) {
                out[len++] = '|';
            } else {
                len = out_size;
                return;
            }
        }
        size_t token_len = boundedStrLen(token, out_size - len);
        if (token_len > out_size - len) {
            token_len = out_size - len;
        }
        std::memcpy(out + len, token, token_len);
        len += token_len;
        first = false;
    };

    if (flags & FLAG_IAMBIC) {
        append_token("IAMBIC");
    }
    if (flags & FLAG_MEMORY_LATCH) {
        append_token("MEM");
    }
    if (flags & FLAG_DEBOUNCE_SKIP) {
        append_token("DEBOUNCE");
    }

    if (first) {
        append_token("-");
    }

    if (len >= out_size) {
        len = out_size - 1;
    }
    out[len] = '\0';
    return len;
}

size_t formatExtended(uint8_t type_ext, uint8_t payload, char *out, size_t out_size) {
    if (out_size == 0) {
        return 0;
    }
    size_t len = 0;
    bool first = true;
    auto append_token = [&](const char *token) {
        if (len >= out_size) {
            return;
        }
        if (!first) {
            if (len + 1 < out_size) {
                out[len++] = '|';
            } else {
                len = out_size;
                return;
            }
        }
        size_t token_len = boundedStrLen(token, out_size - len);
        if (token_len > out_size - len) {
            token_len = out_size - len;
        }
        std::memcpy(out + len, token, token_len);
        len += token_len;
        first = false;
    };

    if (type_ext & EVENT_ELEMENT_DOT) {
        append_token("ELEMENT_DOT");
    }
    if (type_ext & EVENT_ELEMENT_DASH) {
        append_token("ELEMENT_DASH");
    }
    if (type_ext & EVENT_DECODED_CHAR) {
        append_token("DECODED_CHAR");
    }

    if (first) {
        append_token("-");
    }

    if ((type_ext & EVENT_DECODED_CHAR) && payload != 0) {
        char payload_buf[8];
        std::snprintf(payload_buf, sizeof(payload_buf), "('%c')", static_cast<char>(payload));
        append_token(payload_buf);
    }

    if (len >= out_size) {
        len = out_size - 1;
    }
    out[len] = '\0';
    return len;
}

size_t formatEventLine(const TimelineEvent &event, char *line, size_t line_size) {
    if (line_size == 0) {
        return 0;
    }
    char flags_buf[32];
    char ext_buf[48];
    formatFlags(event.flags, flags_buf, sizeof(flags_buf));
    formatExtended(event.type_extended, event.payload, ext_buf, sizeof(ext_buf));

    int written = 0;
    if (event.type != 0) {
        written = std::snprintf(
            line,
            line_size,
            "[%10u] %-12s flags=%-16s ext=%s payload=%u\r\n",
            static_cast<unsigned>(event.timestamp_us),
            eventTypeToString(event.type),
            flags_buf,
            ext_buf,
            static_cast<unsigned>(event.payload));
    } else {
        written = std::snprintf(
            line,
            line_size,
            "[%10u] EXT %-16s payload=%u\r\n",
            static_cast<unsigned>(event.timestamp_us),
            ext_buf,
            static_cast<unsigned>(event.payload));
    }

    if (written < 0) {
        return 0;
    }
    if (static_cast<size_t>(written) >= line_size) {
        return line_size - 1;
    }
    return static_cast<size_t>(written);
}

void usb_timeline_task(void *arg) {
    auto *timeline = static_cast<TimelineBuffer *>(arg);
    TimelineEvent events[32];
    char line[128];

    while (true) {
        size_t count = timeline->read(events, 32);
        if (count == 0) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
            continue;
        }

        size_t total_bytes = 0;
        if (s_cdc_ready[kTimelinePort]) {
            for (size_t i = 0; i < count; ++i) {
                size_t line_len = formatEventLine(events[i], line, sizeof(line));
                if (line_len == 0) {
                    continue;
                }
                size_t written = tinyusb_cdcacm_write_queue(kTimelinePort,
                                                            reinterpret_cast<const uint8_t *>(line),
                                                            line_len);
                total_bytes += written;
            }
            if (total_bytes > 0) {
                (void)tinyusb_cdcacm_write_flush(kTimelinePort, 0);
            }
        }
    }
}

void cdc_line_state_changed_callback(int itf, cdcacm_event_t *event) {
    if (!event) {
        return;
    }
    bool dtr = event->line_state_changed_data.dtr;
    bool rts = event->line_state_changed_data.rts;
    bool connected = dtr && rts;

    if (itf >= 0 && itf < TINYUSB_CDC_ACM_MAX) {
        s_cdc_ready[itf] = connected;
    }

    if (connected && itf == kTimelinePort) {
        static const char banner[] = "\r\n=== Keyer timeline stream ready ===\r\n";
        tinyusb_cdcacm_write_queue(kTimelinePort,
                                   reinterpret_cast<const uint8_t *>(banner),
                                   sizeof(banner) - 1);
        tinyusb_cdcacm_write_flush(kTimelinePort, 0);
    }
}

}  // namespace

esp_err_t usb_debug_init(TimelineBuffer *timeline) {
    if (!timeline) {
        return ESP_ERR_INVALID_ARG;
    }

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = nullptr,
        .string_descriptor = nullptr,
        .external_phy = false,
        .configuration_descriptor = nullptr,
    };

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "TinyUSB install failed: %s", esp_err_to_name(err));
        return err;
    }

    tinyusb_config_cdcacm_t cdc_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = kMessagePort,
        .rx_unread_buf_sz = 64,
        .callback_rx = nullptr,
        .callback_rx_wanted_char = nullptr,
        .callback_line_state_changed = &cdc_line_state_changed_callback,
        .callback_line_coding_changed = nullptr,
    };

    err = tusb_cdc_acm_init(&cdc_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "CDC0 init failed: %s", esp_err_to_name(err));
        return err;
    }

    cdc_cfg.cdc_port = kTimelinePort;
    err = tusb_cdc_acm_init(&cdc_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "CDC1 init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_usb_task_handle) {
        BaseType_t created = xTaskCreatePinnedToCore(
            usb_timeline_task,
            "usb_timeline",
            kUsbTaskStackSize,
            timeline,
            kUsbTaskPriority,
            &s_usb_task_handle,
            kUsbTaskCore);
        if (created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create USB timeline task");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "TinyUSB dual CDC initialised");
    return ESP_OK;
}

void usb_debug_notify_new_timeline_data(void) {
    if (!s_usb_task_handle) {
        return;
    }
    if (xPortInIsrContext()) {
        BaseType_t higher_priority_woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_usb_task_handle, &higher_priority_woken);
        if (higher_priority_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        xTaskNotifyGive(s_usb_task_handle);
    }
}
