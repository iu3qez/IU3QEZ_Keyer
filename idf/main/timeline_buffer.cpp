#include "timeline_buffer.h"

#include <cstring>

#include "esp_timer.h"

static inline uint32_t timeline_now_us() {
    return (uint32_t)(esp_timer_get_time() & 0xFFFFFFFFUL);
}

TimelineBuffer::TimelineBuffer()
    : _head(0), _tail(0),
      _total_pushed(0), _total_dropped(0), _overruns(0),
      _spinlock(portMUX_INITIALIZER_UNLOCKED) {
    std::memset(_buffer, 0, sizeof(_buffer));
}

void TimelineBuffer::push(TimelineEventType type, TimelineEventFlags flags) {
    push(type, flags, timeline_now_us());
}

void TimelineBuffer::pushExtended(TimelineEventTypeExtended type_ext, uint8_t payload) {
    pushExtended(type_ext, payload, timeline_now_us());
}

void TimelineBuffer::pushExtended(TimelineEventTypeExtended type_ext, uint8_t payload, uint32_t timestamp_us) {
    portENTER_CRITICAL_SAFE(&_spinlock);  // ISR-safe: funziona sia da ISR che da task

    uint32_t head = _head;
    uint32_t next_head = wrapIndex(head + 1);

    if (next_head == _tail) {
        _tail = wrapIndex(_tail + 1);
        _overruns++;
        _total_dropped++;
    }

    _buffer[head].timestamp_us = timestamp_us;
    _buffer[head].type = (TimelineEventType)0;
    _buffer[head].flags = FLAG_NONE;
    _buffer[head].type_extended = type_ext;
    _buffer[head].payload = payload;

    _head = next_head;
    _total_pushed++;

    portEXIT_CRITICAL_SAFE(&_spinlock);
}

void TimelineBuffer::push(TimelineEventType type, TimelineEventFlags flags, uint32_t timestamp_us) {
    portENTER_CRITICAL_SAFE(&_spinlock);  // ISR-safe: funziona sia da ISR che da task

    uint32_t head = _head;
    uint32_t next_head = wrapIndex(head + 1);

    if (next_head == _tail) {
        _tail = wrapIndex(_tail + 1);
        _overruns++;
        _total_dropped++;
    }

    _buffer[head].timestamp_us = timestamp_us;
    _buffer[head].type = type;
    _buffer[head].flags = flags;
    _buffer[head].type_extended = 0;
    _buffer[head].payload = 0;

    _head = next_head;
    _total_pushed++;

    portEXIT_CRITICAL_SAFE(&_spinlock);
}

size_t TimelineBuffer::read(TimelineEvent* buffer, size_t max_events) {
    if (!buffer || max_events == 0) {
        return 0;
    }

    portENTER_CRITICAL(&_spinlock);  // Protezione da task context

    size_t count = 0;
    uint32_t tail = _tail;
    uint32_t head = _head;

    while (tail != head && count < max_events) {
        buffer[count++] = _buffer[tail];
        tail = wrapIndex(tail + 1);
    }

    _tail = tail;

    portEXIT_CRITICAL(&_spinlock);
    return count;
}

size_t TimelineBuffer::peek(TimelineEvent* buffer, size_t max_events) {
    if (!buffer || max_events == 0) {
        return 0;
    }

    size_t count = 0;
    uint32_t tail = _tail;
    uint32_t head = _head;

    while (tail != head && count < max_events) {
        buffer[count++] = _buffer[tail];
        tail = wrapIndex(tail + 1);
    }

    return count;
}

size_t TimelineBuffer::available() {
    uint32_t head = _head;
    uint32_t tail = _tail;

    if (head >= tail) {
        return head - tail;
    }
    return TIMELINE_BUFFER_SIZE - (tail - head);
}

size_t TimelineBuffer::freeSpace() {
    return TIMELINE_BUFFER_SIZE - available() - 1;
}

void TimelineBuffer::clear() {
    _tail = _head;
}

bool TimelineBuffer::isEmpty() {
    return _head == _tail;
}

bool TimelineBuffer::isFull() {
    uint32_t next_head = wrapIndex(_head + 1);
    return next_head == _tail;
}
