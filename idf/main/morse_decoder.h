#ifndef MORSE_DECODER_H
#define MORSE_DECODER_H

#include <cstdint>
#include <map>
#include <string>

#include "timeline_buffer.h"
#include "settings.h"

class MorseDecoder {
public:
    explicit MorseDecoder(TimelineBuffer* timeline);

    void setWebSocketTimeline(TimelineBuffer* websocket_tl) {
        _timeline_websocket = websocket_tl;
    }
    void setUsbTimeline(TimelineBuffer* usb_tl) {
        _timeline_usb = usb_tl;
    }

    bool begin();

    void setCharSpaceToleranceDots(uint16_t tenths);
    void setWordSpaceToleranceDots(uint16_t tenths);
    void setDotDuration(uint32_t dot_duration_us);

    uint16_t getCharSpaceToleranceDots() const { return _char_space_tolerance_tenths; }
    uint16_t getWordSpaceToleranceDots() const { return _word_space_tolerance_tenths; }

    void process();
    void checkTimeout();
    void reset();

    uint32_t getCharSpacesDetected() const { return _char_spaces_detected; }
    uint32_t getWordSpacesDetected() const { return _word_spaces_detected; }

private:
    TimelineBuffer* _timeline;
    TimelineBuffer* _timeline_websocket;
    TimelineBuffer* _timeline_usb;

    uint16_t _char_space_tolerance_tenths;
    uint16_t _word_space_tolerance_tenths;

    uint32_t _last_key_off_us;
    bool _key_is_down;
    uint32_t _dot_duration_us;
    bool _timeout_decoded;

    std::string _current_char_pattern;
    uint32_t _last_element_start_us;

    std::map<std::string, char> _morse_table;

    uint32_t _char_spaces_detected;
    uint32_t _word_spaces_detected;

    void detectSpace(uint32_t pause_duration_us, uint32_t timestamp_us);

    void initMorseTable();
    char decodePattern(const std::string& pattern) const;
};

#endif // MORSE_DECODER_H
