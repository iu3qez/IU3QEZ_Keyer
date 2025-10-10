#include "morse_decoder.h"

#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"

#include "usb_debug.h"

static const char *TAG = "morse_decoder";

static inline void decoderBroadcastPush(TimelineBuffer* tl_websocket,
                                        TimelineBuffer* tl_usb,
                                        TimelineEventType type,
                                        TimelineEventFlags flags,
                                        uint32_t timestamp_us) {
    bool pushed_usb = false;
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

static inline void decoderBroadcastPushExtended(TimelineBuffer* tl_websocket,
                                                TimelineBuffer* tl_usb,
                                                TimelineEventTypeExtended type_extended,
                                                uint8_t payload,
                                                uint32_t timestamp_us) {
    if (tl_websocket) {
        tl_websocket->pushExtended(type_extended, payload, timestamp_us);
    }
    if (tl_usb) {
        tl_usb->pushExtended(type_extended, payload, timestamp_us);
        usb_debug_notify_new_timeline_data();
    }
}

MorseDecoder::MorseDecoder(TimelineBuffer* timeline)
    : _timeline(timeline),
      _timeline_websocket(nullptr),
      _timeline_usb(nullptr),
      _char_space_tolerance_tenths(DECODER_CHAR_SPACE_TOLERANCE_TENTHS),
      _word_space_tolerance_tenths(DECODER_WORD_SPACE_TOLERANCE_TENTHS),
      _last_key_off_us(0),
      _key_is_down(false),
      _dot_duration_us(0),
      _timeout_decoded(false),
      _current_char_pattern(),
      _last_element_start_us(0),
      _char_spaces_detected(0),
      _word_spaces_detected(0) {}

bool MorseDecoder::begin() {
    ESP_LOGI(TAG, "Initialising Morse Decoder (char tol=%.1f dots word tol=%.1f dots)",
             _char_space_tolerance_tenths / 10.0f,
             _word_space_tolerance_tenths / 10.0f);
    initMorseTable();
    ESP_LOGI(TAG, "Morse table loaded with %zu entries", _morse_table.size());
    reset();
    return true;
}

void MorseDecoder::setCharSpaceToleranceDots(uint16_t tenths) {
    if (tenths > 50) {  // clamp to ±5 dots
        tenths = 50;
    }
    _char_space_tolerance_tenths = tenths;
    ESP_LOGI(TAG, "Char space tolerance set to %.1f dots", tenths / 10.0f);
}

void MorseDecoder::setWordSpaceToleranceDots(uint16_t tenths) {
    if (tenths > 70) {  // clamp to ±7 dots
        tenths = 70;
    }
    _word_space_tolerance_tenths = tenths;
    ESP_LOGI(TAG, "Word space tolerance set to %.1f dots", tenths / 10.0f);
}

void MorseDecoder::setDotDuration(uint32_t dot_duration_us) {
    _dot_duration_us = dot_duration_us;
    ESP_LOGI(TAG, "Dot duration updated to %lu us", (unsigned long)_dot_duration_us);
}

void MorseDecoder::reset() {
    _last_key_off_us = 0;
    _key_is_down = false;
    _current_char_pattern.clear();
    _last_element_start_us = 0;
    _timeout_decoded = false;
}

void MorseDecoder::checkTimeout() {
    if (_dot_duration_us == 0 || _last_key_off_us == 0 || _key_is_down) {
        return;
    }

    if (_current_char_pattern.empty() || _timeout_decoded) {
        return;
    }

    uint32_t now_us = static_cast<uint32_t>(esp_timer_get_time());
    uint32_t elapsed_us = now_us - _last_key_off_us;

    uint32_t char_space_target = _dot_duration_us * 3;
    uint32_t word_space_target = _dot_duration_us * 7;
    uint32_t char_tol_us = (_char_space_tolerance_tenths * _dot_duration_us) / 10;
    uint32_t word_tol_us = (_word_space_tolerance_tenths * _dot_duration_us) / 10;

    auto diff = [](uint32_t value, uint32_t target) -> uint32_t {
        return (value > target) ? (value - target) : (target - value);
    };

    bool within_char = (diff(elapsed_us, char_space_target) <= char_tol_us);
    bool within_word = (diff(elapsed_us, word_space_target) <= word_tol_us);

    if (!within_char && !within_word) {
        if (elapsed_us > word_space_target + word_tol_us) {
            within_word = true;
        } else if (elapsed_us > char_space_target + char_tol_us) {
            within_char = true;
        }
    }

    if (!within_char && !within_word) {
        ESP_LOGD(TAG, "Timeout pause %lu us ignored (char diff=%lu word diff=%lu)",
                 (unsigned long)elapsed_us,
                 (unsigned long)diff(elapsed_us, char_space_target),
                 (unsigned long)diff(elapsed_us, word_space_target));
        return;
    }

    ESP_LOGD(TAG, "Timeout space detected (%lu us) [char diff=%lu word diff=%lu]",
             (unsigned long)elapsed_us,
             (unsigned long)diff(elapsed_us, char_space_target),
             (unsigned long)diff(elapsed_us, word_space_target));

    if (!_current_char_pattern.empty()) {
        char decoded_char = decodePattern(_current_char_pattern);
        if (decoded_char != '\0') {
            decoderBroadcastPushExtended(_timeline_websocket, _timeline_usb, EVENT_DECODED_CHAR,
                                         static_cast<uint8_t>(decoded_char), now_us);
        }
    }

    if (within_word) {
        decoderBroadcastPush(_timeline_websocket, _timeline_usb, EVENT_SPACE_WORD, FLAG_NONE, now_us);
        _word_spaces_detected++;
        decoderBroadcastPushExtended(_timeline_websocket, _timeline_usb, EVENT_DECODED_CHAR,
                                     static_cast<uint8_t>(' '), now_us);
        ESP_LOGD(TAG, "Timeout promoted to WORD space (pause=%lu)", (unsigned long)elapsed_us);
    } else if (within_char) {
        decoderBroadcastPush(_timeline_websocket, _timeline_usb, EVENT_SPACE_CHAR, FLAG_NONE, now_us);
        _char_spaces_detected++;
        ESP_LOGD(TAG, "Timeout promoted to CHAR space (pause=%lu)", (unsigned long)elapsed_us);
    }

    _current_char_pattern.clear();
    _timeout_decoded = true;
}

void MorseDecoder::process() {
    if (!_timeline) {
        return;
    }

    TimelineEvent events[32];
    size_t num_events = _timeline->read(events, 32);

    for (size_t i = 0; i < num_events; ++i) {
        TimelineEvent &evt = events[i];

        if (evt.type_extended != 0) {
            if (evt.type_extended & EVENT_ELEMENT_DOT) {
                _current_char_pattern.push_back('.');
                _timeout_decoded = false;
            } else if (evt.type_extended & EVENT_ELEMENT_DASH) {
                _current_char_pattern.push_back('-');
                _timeout_decoded = false;
            } else if (evt.type_extended & EVENT_DECODED_CHAR) {
                // ignore feedback
            }
            continue;
        }

        switch (evt.type) {
            case EVENT_KEY_ON:
                if (!_key_is_down) {
                    _key_is_down = true;
                    if (_last_key_off_us > 0 && !_timeout_decoded) {
                        uint32_t pause_duration_us = evt.timestamp_us - _last_key_off_us;
                        detectSpace(pause_duration_us, evt.timestamp_us);
                    }
                    _last_element_start_us = evt.timestamp_us;
                }
                break;

            case EVENT_KEY_OFF:
                if (_key_is_down) {
                    _key_is_down = false;
                    _last_key_off_us = evt.timestamp_us;
                    _timeout_decoded = false;
                }
                break;

            case EVENT_DOT_PRESS:
            case EVENT_DOT_RELEASE:
            case EVENT_DASH_PRESS:
            case EVENT_DASH_RELEASE:
            case EVENT_SPACE_CHAR:
            case EVENT_SPACE_WORD:
            default:
                break;
        }
    }
}

void MorseDecoder::detectSpace(uint32_t pause_duration_us, uint32_t timestamp_us) {
    if (_dot_duration_us == 0) {
        return;
    }

    uint32_t char_space_target = _dot_duration_us * 3;
    uint32_t word_space_target = _dot_duration_us * 7;
    uint32_t char_tol_us = (_char_space_tolerance_tenths * _dot_duration_us) / 10;
    uint32_t word_tol_us = (_word_space_tolerance_tenths * _dot_duration_us) / 10;

    if (pause_duration_us > 2000000) {
        return;
    }

    auto diff = [](uint32_t value, uint32_t target) -> uint32_t {
        return (value > target) ? (value - target) : (target - value);
    };

    bool within_word = (diff(pause_duration_us, word_space_target) <= word_tol_us);
    bool within_char = (diff(pause_duration_us, char_space_target) <= char_tol_us);

    ESP_LOGD(TAG,
             "detectSpace: pause=%lu char=%lu±%lu word=%lu±%lu withinChar=%d withinWord=%d patternLen=%zu",
             (unsigned long)pause_duration_us,
             (unsigned long)char_space_target, (unsigned long)char_tol_us,
             (unsigned long)word_space_target, (unsigned long)word_tol_us,
             within_char, within_word, _current_char_pattern.size());

    if (!within_word && pause_duration_us > word_space_target + word_tol_us) {
        within_word = true;
        ESP_LOGD(TAG, "Pause %lu us promoted to WORD space (beyond upper tolerance)",
                 (unsigned long)pause_duration_us);
    }

    if (!within_word && !within_char) {
        if (pause_duration_us > word_space_target) {
            within_word = true;
            ESP_LOGD(TAG, "Pause %lu us treated as WORD space (between targets)",
                     (unsigned long)pause_duration_us);
        } else if (pause_duration_us > char_space_target) {
            within_char = true;
            ESP_LOGD(TAG, "Pause %lu us treated as CHAR space (between targets)",
                     (unsigned long)pause_duration_us);
        } else {
            ESP_LOGD(TAG, "Pause %lu us ignored (char_target=%lu±%lu word_target=%lu±%lu)",
                     (unsigned long)pause_duration_us,
                     (unsigned long)char_space_target, (unsigned long)char_tol_us,
                     (unsigned long)word_space_target, (unsigned long)word_tol_us);
            return;
        }
    }

    if (within_word) {
        if (!_current_char_pattern.empty()) {
            char decoded_char = decodePattern(_current_char_pattern);
            if (decoded_char != '\0') {
                decoderBroadcastPushExtended(_timeline_websocket, _timeline_usb, EVENT_DECODED_CHAR,
                                             static_cast<uint8_t>(decoded_char), timestamp_us);
            }
        }

        decoderBroadcastPush(_timeline_websocket, _timeline_usb, EVENT_SPACE_WORD, FLAG_NONE, timestamp_us);
        _word_spaces_detected++;
        decoderBroadcastPushExtended(_timeline_websocket, _timeline_usb, EVENT_DECODED_CHAR,
                                     static_cast<uint8_t>(' '), timestamp_us);
        _current_char_pattern.clear();
        ESP_LOGD(TAG, "Detected WORD space at %lu us (pause=%lu)", (unsigned long)timestamp_us,
                 (unsigned long)pause_duration_us);
        return;
    }

    if (within_char) {
        decoderBroadcastPush(_timeline_websocket, _timeline_usb, EVENT_SPACE_CHAR, FLAG_NONE, timestamp_us);
        _char_spaces_detected++;
        if (!_current_char_pattern.empty()) {
            char decoded_char = decodePattern(_current_char_pattern);
            if (decoded_char != '\0') {
                decoderBroadcastPushExtended(_timeline_websocket, _timeline_usb, EVENT_DECODED_CHAR,
                                             static_cast<uint8_t>(decoded_char), timestamp_us);
            }
        }
        _current_char_pattern.clear();
        ESP_LOGD(TAG, "Detected CHAR space at %lu us (pause=%lu)", (unsigned long)timestamp_us,
                 (unsigned long)pause_duration_us);
    }
}

void MorseDecoder::initMorseTable() {
    _morse_table.clear();
    _morse_table[".-"] = 'A';
    _morse_table["-..."] = 'B';
    _morse_table["-.-."] = 'C';
    _morse_table["-.."] = 'D';
    _morse_table["."] = 'E';
    _morse_table["..-."] = 'F';
    _morse_table["--."] = 'G';
    _morse_table["...."] = 'H';
    _morse_table[".."] = 'I';
    _morse_table[".---"] = 'J';
    _morse_table["-.-"] = 'K';
    _morse_table[".-.."] = 'L';
    _morse_table["--"] = 'M';
    _morse_table["-."] = 'N';
    _morse_table["---"] = 'O';
    _morse_table[".--."] = 'P';
    _morse_table["--.-"] = 'Q';
    _morse_table[".-."] = 'R';
    _morse_table["..."] = 'S';
    _morse_table["-"] = 'T';
    _morse_table["..-"] = 'U';
    _morse_table["...-"] = 'V';
    _morse_table[".--"] = 'W';
    _morse_table["-..-"] = 'X';
    _morse_table["-.--"] = 'Y';
    _morse_table["--.."] = 'Z';

    _morse_table["-----"] = '0';
    _morse_table[".----"] = '1';
    _morse_table["..---"] = '2';
    _morse_table["...--"] = '3';
    _morse_table["....-"] = '4';
    _morse_table["....."] = '5';
    _morse_table["-...."] = '6';
    _morse_table["--..."] = '7';
    _morse_table["---.."] = '8';
    _morse_table["----."] = '9';

    _morse_table[".-.-.-"] = '.';
    _morse_table["--..--"] = ',';
    _morse_table["---..."] = ':';
    _morse_table["..--.."] = '?';
    _morse_table[".----."] = '\'';
    _morse_table["-....-"] = '-';
    _morse_table["-..-."] = '/';
    _morse_table["-.--."] = '(';
    _morse_table["-.--.-"] = ')';
    _morse_table["-...-"] = '=';

    _morse_table[".-..-"] = 'E';  // fallback per È
    _morse_table["---."] = 'O';   // fallback per Ó
    _morse_table["..--"] = 'U';   // fallback per Ü
}

char MorseDecoder::decodePattern(const std::string& pattern) const {
    if (pattern.empty()) {
        return '\0';
    }
    auto it = _morse_table.find(pattern);
    if (it != _morse_table.end()) {
        return it->second;
    }
    return '?';
}
