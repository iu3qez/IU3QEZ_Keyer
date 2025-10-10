#include "keyer_logic.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "usb_debug.h"

static const char *TAG = "keyer_logic";

static inline void timelineBroadcastPush(TimelineBuffer* tl_decoder, TimelineBuffer* tl_websocket, TimelineBuffer* tl_usb,
                                         TimelineEventType type, TimelineEventFlags flags, uint32_t timestamp_us) {
    bool pushed_usb = false;
    if (tl_decoder) {
        tl_decoder->push(type, flags, timestamp_us);
    }
    if (tl_websocket) {
        tl_websocket->push(type, flags, timestamp_us);
    }
    if (tl_usb) {
        tl_usb->push(type, flags, timestamp_us);
        pushed_usb = true;
    }
    if (pushed_usb) {
        usb_debug_notify_new_timeline_data();
    }
}

static inline void timelineBroadcastPush(TimelineBuffer* tl_decoder, TimelineBuffer* tl_websocket, TimelineBuffer* tl_usb,
                                         TimelineEventType type, TimelineEventFlags flags = FLAG_NONE) {
    timelineBroadcastPush(tl_decoder, tl_websocket, tl_usb, type, flags,
                          static_cast<uint32_t>(esp_timer_get_time()));
}

static inline void timelineBroadcastPushExtended(TimelineBuffer* tl_decoder, TimelineBuffer* tl_websocket, TimelineBuffer* tl_usb,
                                                 TimelineEventTypeExtended type_extended, uint8_t payload = 0) {
    uint32_t ts = static_cast<uint32_t>(esp_timer_get_time());
    if (tl_decoder) {
        tl_decoder->pushExtended(type_extended, payload, ts);
    }
    if (tl_websocket) {
        tl_websocket->pushExtended(type_extended, payload, ts);
    }
    if (tl_usb) {
        tl_usb->pushExtended(type_extended, payload, ts);
        usb_debug_notify_new_timeline_data();
    }
}

KeyerLogic::KeyerLogic()
    : _wpm(KEYER_WPM_DEFAULT),
      _dot_duration_us(0),
      _dash_duration_us(0),
      _element_space_us(0),
      _char_space_us(0),
      _window_up_percent(KEYER_MEMORY_WINDOW_UP),
      _window_down_percent(KEYER_MEMORY_WINDOW_DOWN),
      _mode(KEYER_MODE_DEFAULT),
      _debounce_us(PADDLE_DEBOUNCE_US),
      _state(KEYER_IDLE),
      _keying(false),
      _memory_latch(ELEMENT_NONE),
      _current_element(ELEMENT_NONE),
      _element_start_us(0),
      _element_duration_us(0),
      _dot_pressed(false),
      _dash_pressed(false),
      _dot_last_change_us(0),
      _dash_last_change_us(0),
      _dot_press_start_us(0),
      _dash_press_start_us(0),
      _dot_isr_count(0),
      _dash_isr_count(0),
      _timer(nullptr),
      _callback(nullptr),
      _timeline_decoder(nullptr),
      _timeline_websocket(nullptr),
      _timeline_usb(nullptr) {
    calculateTimings();
}

bool KeyerLogic::begin(KeyerCallback keyingCallback) {
    _callback = keyingCallback;

    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << DOT_PIN) | (1ULL << DASH_PIN);
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    calculateTimings();

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &KeyerLogic::timerISR;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "keyer_tick";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(_timer, 1000));  // 1 ms tick

    ESP_LOGI(TAG, "Keyer initialized: %d WPM, mode %d", _wpm, _mode);
    ESP_LOGI(TAG, "Timing: DOT=%lu us, DASH=%lu us", _dot_duration_us, _dash_duration_us);
    return true;
}

void KeyerLogic::setWPM(uint8_t wpm) {
    if (wpm < KEYER_WPM_MIN) {
        wpm = KEYER_WPM_MIN;
    } else if (wpm > KEYER_WPM_MAX) {
        wpm = KEYER_WPM_MAX;
    }
    _wpm = wpm;
    calculateTimings();
    ESP_LOGI(TAG, "WPM set to %u (DOT=%lu us)", _wpm, _dot_duration_us);
}

void KeyerLogic::setMode(uint8_t mode) {
    if (mode <= KEYER_MODE_ULTIMATIC) {
        _mode = mode;
        ESP_LOGI(TAG, "Mode set to %u", _mode);
    }
}

void KeyerLogic::setMemoryWindow(uint8_t up_percent, uint8_t down_percent) {
    if (up_percent <= 100 && down_percent <= 100) {
        _window_up_percent = up_percent;
        _window_down_percent = down_percent;
        ESP_LOGI(TAG, "Memory window U=%u%% D=%u%%", up_percent, down_percent);
    }
}

void KeyerLogic::getMemoryWindow(uint8_t* up_percent, uint8_t* down_percent) const {
    if (up_percent) {
        *up_percent = _window_up_percent;
    }
    if (down_percent) {
        *down_percent = _window_down_percent;
    }
}

void KeyerLogic::setDebounce(uint32_t debounce_us) {
    if (debounce_us < 100) {
        debounce_us = 100;
    } else if (debounce_us > 10000) {
        debounce_us = 10000;
    }
    _debounce_us = debounce_us;
    ESP_LOGI(TAG, "Debounce set to %u us", (unsigned)_debounce_us);
}

void KeyerLogic::calculateTimings() {
    _dot_duration_us = 1200000UL / _wpm;
    _dash_duration_us = 3 * _dot_duration_us;
    _element_space_us = _dot_duration_us;
    _char_space_us = 3 * _dot_duration_us;
}

bool KeyerLogic::isInMemoryWindow(uint32_t elapsed_us) {
    if (_element_duration_us == 0) {
        return false;
    }
    uint32_t progress = (elapsed_us * 100) / _element_duration_us;
    uint32_t window_close = 100 - _window_down_percent;
    return (progress >= _window_up_percent && progress <= window_close);
}

void KeyerLogic::startElement(Element_t element, uint32_t now_us) {
    _current_element = element;
    _element_start_us = now_us;

    if (element == ELEMENT_DOT) {
        _state = KEYER_DOT_ACTIVE;
        _element_duration_us = _dot_duration_us;
    } else {
        _state = KEYER_DASH_ACTIVE;
        _element_duration_us = _dash_duration_us;
    }

    _keying = true;
    timelineBroadcastPush(_timeline_decoder, _timeline_websocket, _timeline_usb, EVENT_KEY_ON, FLAG_NONE, now_us);
    if (_callback) {
        _callback(true);
    }
}

void KeyerLogic::processNextElement(uint32_t now_us) {
    Element_t next_element = ELEMENT_NONE;

    if (_mode == KEYER_MODE_IAMBIC_B) {
        if (_memory_latch != ELEMENT_NONE) {
            next_element = _memory_latch;
            _memory_latch = ELEMENT_NONE;
        } else if (_dot_pressed && !_dash_pressed) {
            next_element = ELEMENT_DOT;
        } else if (_dash_pressed && !_dot_pressed) {
            next_element = ELEMENT_DASH;
        } else if (_dot_pressed && _dash_pressed) {
            next_element = (_current_element == ELEMENT_DOT) ? ELEMENT_DASH : ELEMENT_DOT;
        }
    }

    if (next_element != ELEMENT_NONE) {
        startElement(next_element, now_us);
    } else {
        _state = KEYER_IDLE;
        _current_element = ELEMENT_NONE;
        _keying = false;
    }
}

void KeyerLogic::handleTimerTick(uint32_t now_us) {
    bool dot_gpio = (gpio_get_level(static_cast<gpio_num_t>(DOT_PIN)) == 0);
    bool dash_gpio = (gpio_get_level(static_cast<gpio_num_t>(DASH_PIN)) == 0);

    if (dot_gpio != _dot_pressed) {
        if ((now_us - _dot_last_change_us) >= _debounce_us) {
            _dot_pressed = dot_gpio;
            _dot_last_change_us = now_us;
            TimelineEventFlags flags = (_dot_pressed && _dash_pressed) ? FLAG_IAMBIC : FLAG_NONE;
            timelineBroadcastPush(_timeline_decoder, _timeline_websocket, _timeline_usb,
                                  dot_gpio ? EVENT_DOT_PRESS : EVENT_DOT_RELEASE, flags, now_us);
            if (dot_gpio) {
                _dot_press_start_us = now_us;
            }
        }
    }

    if (dash_gpio != _dash_pressed) {
        if ((now_us - _dash_last_change_us) >= _debounce_us) {
            _dash_pressed = dash_gpio;
            _dash_last_change_us = now_us;
            TimelineEventFlags flags = (_dot_pressed && _dash_pressed) ? FLAG_IAMBIC : FLAG_NONE;
            timelineBroadcastPush(_timeline_decoder, _timeline_websocket, _timeline_usb,
                                  dash_gpio ? EVENT_DASH_PRESS : EVENT_DASH_RELEASE, flags, now_us);
            if (dash_gpio) {
                _dash_press_start_us = now_us;
            }
        }
    }

    switch (_state) {
        case KEYER_IDLE:
            if (_dot_pressed && !_dash_pressed) {
                startElement(ELEMENT_DOT, now_us);
            } else if (_dash_pressed && !_dot_pressed) {
                startElement(ELEMENT_DASH, now_us);
            } else if (_dot_pressed && _dash_pressed) {
                startElement(ELEMENT_DOT, now_us);
            }
            break;

        case KEYER_DOT_ACTIVE:
        case KEYER_DASH_ACTIVE: {
            uint32_t elapsed = now_us - _element_start_us;
            if (_mode == KEYER_MODE_IAMBIC_B && _memory_latch == ELEMENT_NONE) {
                if (isInMemoryWindow(elapsed)) {
                    if (_current_element == ELEMENT_DOT && _dash_pressed) {
                        _memory_latch = ELEMENT_DASH;
                    } else if (_current_element == ELEMENT_DASH && _dot_pressed) {
                        _memory_latch = ELEMENT_DOT;
                    }
                }
            }

            if (elapsed >= _element_duration_us) {
                if (_current_element == ELEMENT_DOT) {
                    timelineBroadcastPushExtended(_timeline_decoder, _timeline_websocket, _timeline_usb, EVENT_ELEMENT_DOT);
                } else if (_current_element == ELEMENT_DASH) {
                    timelineBroadcastPushExtended(_timeline_decoder, _timeline_websocket, _timeline_usb, EVENT_ELEMENT_DASH);
                }

                _keying = false;
                timelineBroadcastPush(_timeline_decoder, _timeline_websocket, _timeline_usb, EVENT_KEY_OFF, FLAG_NONE, now_us);
                if (_callback) {
                    _callback(false);
                }

                _state = KEYER_INTER_ELEMENT;
                _element_start_us = now_us;
                _element_duration_us = _element_space_us;
            }
            break;
        }

        case KEYER_INTER_ELEMENT: {
            uint32_t elapsed = now_us - _element_start_us;
            if (elapsed >= _element_duration_us) {
                processNextElement(now_us);
            }
            break;
        }

        case KEYER_INTER_CHAR:
            processNextElement(now_us);
            break;
    }
}

void KeyerLogic::timerISR(void* arg) {
    KeyerLogic* instance = static_cast<KeyerLogic*>(arg);
    if (!instance) {
        return;
    }
    uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
    instance->handleTimerTick(now);
}
