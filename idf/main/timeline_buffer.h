#ifndef TIMELINE_BUFFER_H
#define TIMELINE_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

// Tipi di eventi timeline
enum TimelineEventType : uint8_t {
    EVENT_DOT_PRESS     = 0x01,
    EVENT_DOT_RELEASE   = 0x02,
    EVENT_DASH_PRESS    = 0x04,
    EVENT_DASH_RELEASE  = 0x08,
    EVENT_KEY_ON        = 0x10,
    EVENT_KEY_OFF       = 0x20,
    EVENT_SPACE_CHAR    = 0x40,
    EVENT_SPACE_WORD    = 0x80,
};

// Eventi estesi
enum TimelineEventTypeExtended : uint8_t {
    EVENT_ELEMENT_DOT   = 0x01,
    EVENT_ELEMENT_DASH  = 0x02,
    EVENT_DECODED_CHAR  = 0x04,
};

// Flags aggiuntivi
enum TimelineEventFlags : uint8_t {
    FLAG_NONE           = 0x00,
    FLAG_IAMBIC         = 0x01,
    FLAG_MEMORY_LATCH   = 0x02,
    FLAG_DEBOUNCE_SKIP  = 0x04,
};

struct TimelineEvent {
    uint32_t timestamp_us;
    TimelineEventType type;
    TimelineEventFlags flags;
    uint8_t type_extended;
    uint8_t payload;
};

#define TIMELINE_BUFFER_SIZE 1024

class TimelineBuffer {
public:
    TimelineBuffer();

    void IRAM_ATTR push(TimelineEventType type, TimelineEventFlags flags = FLAG_NONE);
    void IRAM_ATTR push(TimelineEventType type, TimelineEventFlags flags, uint32_t timestamp_us);
    void IRAM_ATTR pushExtended(TimelineEventTypeExtended type_ext, uint8_t payload = 0);
    void IRAM_ATTR pushExtended(TimelineEventTypeExtended type_ext, uint8_t payload, uint32_t timestamp_us);

    size_t read(TimelineEvent* buffer, size_t max_events);
    size_t peek(TimelineEvent* buffer, size_t max_events);

    size_t available();
    size_t freeSpace();
    void clear();
    bool isEmpty();
    bool isFull();

    uint32_t getTotalPushed() { return _total_pushed; }
    uint32_t getTotalDropped() { return _total_dropped; }
    uint32_t getOverruns() { return _overruns; }

private:
    TimelineEvent _buffer[TIMELINE_BUFFER_SIZE];
    volatile uint32_t _head;
    volatile uint32_t _tail;
    volatile uint32_t _total_pushed;
    volatile uint32_t _total_dropped;
    volatile uint32_t _overruns;
    portMUX_TYPE _spinlock;  // Protezione ISR-safe per accessi concorrenti

    inline uint32_t wrapIndex(uint32_t index) {
        return index & (TIMELINE_BUFFER_SIZE - 1);
    }
};

#endif // TIMELINE_BUFFER_H
