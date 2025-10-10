#ifndef KEYER_LOGIC_H
#define KEYER_LOGIC_H

#include <stdint.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_timer.h"

#include "settings.h"
#include "timeline_buffer.h"

enum KeyerState_t {
    KEYER_IDLE,
    KEYER_DOT_ACTIVE,
    KEYER_DASH_ACTIVE,
    KEYER_INTER_ELEMENT,
    KEYER_INTER_CHAR
};

enum Element_t {
    ELEMENT_NONE = 0,
    ELEMENT_DOT  = 1,
    ELEMENT_DASH = 2
};

typedef void (*KeyerCallback)(bool keying);

class KeyerLogic {
public:
    KeyerLogic();

    bool begin(KeyerCallback keyingCallback);

    void setWPM(uint8_t wpm);
    uint8_t getWPM() const { return _wpm; }
    uint32_t getDotDuration() const { return _dot_duration_us; }

    void setMode(uint8_t mode);
    uint8_t getMode() const { return _mode; }

    void setMemoryWindow(uint8_t up_percent, uint8_t down_percent);
    void getMemoryWindow(uint8_t* up_percent, uint8_t* down_percent) const;
    void setDebounce(uint32_t debounce_us);
    uint32_t getDebounce() const { return _debounce_us; }

    KeyerState_t getState() const { return _state; }
    bool isKeying() const { return _keying; }

    bool getDotPressed() const { return _dot_pressed; }
    bool getDashPressed() const { return _dash_pressed; }
    uint32_t getDotISRCount() const { return _dot_isr_count; }
    uint32_t getDashISRCount() const { return _dash_isr_count; }

    void setTimelineTargets(TimelineBuffer* decoder_tl, TimelineBuffer* websocket_tl, TimelineBuffer* usb_tl = nullptr) {
        _timeline_decoder = decoder_tl;
        _timeline_websocket = websocket_tl;
        _timeline_usb = usb_tl;
    }

    TimelineBuffer* getTimelineBuffer() { return _timeline_decoder; }

private:
    void calculateTimings();
    bool isInMemoryWindow(uint32_t elapsed_us);
    void startElement(Element_t element, uint32_t now_us);
    void processNextElement(uint32_t now_us);
    void handleTimerTick(uint32_t now_us);

    static void timerISR(void* arg);

    uint8_t _wpm;
    uint32_t _dot_duration_us;
    uint32_t _dash_duration_us;
    uint32_t _element_space_us;
    uint32_t _char_space_us;

    uint8_t _window_up_percent;
    uint8_t _window_down_percent;
    uint8_t _mode;
    uint32_t _debounce_us;

    volatile KeyerState_t _state;
    volatile bool _keying;
    volatile Element_t _memory_latch;
    volatile Element_t _current_element;
    volatile uint32_t _element_start_us;
    volatile uint32_t _element_duration_us;
    volatile bool _dot_pressed;
    volatile bool _dash_pressed;
    volatile uint32_t _dot_last_change_us;
    volatile uint32_t _dash_last_change_us;
    volatile uint32_t _dot_press_start_us;
    volatile uint32_t _dash_press_start_us;
    volatile uint32_t _dot_isr_count;
    volatile uint32_t _dash_isr_count;

    esp_timer_handle_t _timer;
    KeyerCallback _callback;

    TimelineBuffer* _timeline_decoder;
    TimelineBuffer* _timeline_websocket;
    TimelineBuffer* _timeline_usb;
};

#endif // KEYER_LOGIC_H
