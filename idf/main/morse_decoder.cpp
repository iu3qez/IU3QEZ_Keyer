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
    bool pushed = false;
    if (tl_websocket) {
        tl_websocket->push(type, flags, timestamp_us);
        pushed = true;
    }
    if (tl_usb) {
        tl_usb->push(type, flags, timestamp_us);
        pushed = true;
    }
    if (pushed) {
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
    }
    usb_debug_notify_new_timeline_data();
}

MorseDecoder::MorseDecoder(TimelineBuffer* timeline)
    : _timeline(timeline),
      _timeline_websocket(nullptr),
      _timeline_usb(nullptr),
      _char_space_tolerance(DECODER_CHAR_SPACE_TOLERANCE),
      _word_space_tolerance(DECODER_WORD_SPACE_TOLERANCE),
      _last_key_off_us(0),
      _key_is_down(false),
      _dot_duration_us(0),
      _timeout_decoded(false),
      _current_char_pattern(),
      _last_element_start_us(0),
      _char_spaces_detected(0),
      _word_spaces_detected(0) {}

bool MorseDecoder::begin() {
    ESP_LOGI(TAG, "Initialising Morse Decoder (char tol=%u%% word tol=%u%%)",
             _char_space_tolerance, _word_space_tolerance);
    initMorseTable();
    ESP_LOGI(TAG, "Morse table loaded with %zu entries", _morse_table.size());
    reset();
    return true;
}

void MorseDecoder::setCharSpaceTolerance(uint8_t percent) {
    if (percent > 100) {
        percent = 100;
    }
    _char_space_tolerance = percent;
    ESP_LOGI(TAG, "Char space tolerance set to %u%%", _char_space_tolerance);
}

void MorseDecoder::setWordSpaceTolerance(uint8_t percent) {
    if (percent > 100) {
        percent = 100;
    }
    _word_space_tolerance = percent;
    ESP_LOGI(TAG, "Word space tolerance set to %u%%", _word_space_tolerance);
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

    if (elapsed_us >= char_space_target) {
        ESP_LOGD(TAG, "Timeout space detected (%lu us)", (unsigned long)elapsed_us);
        if (elapsed_us >= word_space_target) {
            char decoded_char = decodePattern(_current_char_pattern);
            if (decoded_char != '\0') {
                ESP_LOGD(TAG, "Timeout decoded char '%c'", decoded_char);
            }
        }

        _current_char_pattern.clear();
        _timeout_decoded = true;
    }
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

    if (pause_duration_us > 2000000) {
        return;
    }

    if (isInRange(pause_duration_us, word_space_target, _word_space_tolerance)) {
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
    } else if (isInRange(pause_duration_us, char_space_target, _char_space_tolerance)) {
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
    } else if (pause_duration_us > (word_space_target * 2)) {
        if (!_current_char_pattern.empty()) {
            char decoded_char = decodePattern(_current_char_pattern);
            if (decoded_char != '\0') {
                decoderBroadcastPushExtended(_timeline_websocket, _timeline_usb, EVENT_DECODED_CHAR,
                                             static_cast<uint8_t>(decoded_char), timestamp_us);
            }
        }
        _current_char_pattern.clear();
    } else {
        // inter-element space: no action
    }
}

bool MorseDecoder::isInRange(uint32_t value, uint32_t target, uint8_t tolerance_percent) const {
    uint32_t delta = (target * tolerance_percent) / 100;
    uint32_t min_value = (target > delta) ? target - delta : 0;
    uint32_t max_value = target + delta;
    return (value >= min_value && value <= max_value);
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
