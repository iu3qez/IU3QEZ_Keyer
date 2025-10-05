#include "timeline_buffer.h"

TimelineBuffer::TimelineBuffer()
    : _head(0), _tail(0),
      _total_pushed(0), _total_dropped(0), _overruns(0) {
    // Inizializza buffer a zero
    memset(_buffer, 0, sizeof(_buffer));
}

void IRAM_ATTR TimelineBuffer::push(TimelineEventType type, TimelineEventFlags flags) {
    push(type, flags, micros());
}

void IRAM_ATTR TimelineBuffer::push(TimelineEventType type, TimelineEventFlags flags, uint32_t timestamp_us) {
    // Lockfree push: singolo writer (ISR), singolo reader (task)
    uint32_t head = _head;
    uint32_t next_head = wrapIndex(head + 1);

    // Check se buffer pieno (head raggiunge tail)
    if (next_head == _tail) {
        // Buffer pieno: overwrite policy (sovrascrive evento più vecchio)
        _tail = wrapIndex(_tail + 1);  // Avanza tail
        _overruns++;
        _total_dropped++;
    }

    // Scrivi evento
    _buffer[head].timestamp_us = timestamp_us;
    _buffer[head].type = type;
    _buffer[head].flags = flags;

    // Avanza head (memory barrier implicito su ESP32)
    _head = next_head;
    _total_pushed++;
}

size_t TimelineBuffer::read(TimelineEvent* buffer, size_t max_events) {
    if (!buffer || max_events == 0) return 0;

    size_t count = 0;
    uint32_t tail = _tail;
    uint32_t head = _head;

    // Leggi fino a max_events o fino a svuotare buffer
    while (tail != head && count < max_events) {
        buffer[count++] = _buffer[tail];
        tail = wrapIndex(tail + 1);
    }

    // Aggiorna tail (consuma eventi)
    _tail = tail;

    return count;
}

size_t TimelineBuffer::peek(TimelineEvent* buffer, size_t max_events) {
    if (!buffer || max_events == 0) return 0;

    size_t count = 0;
    uint32_t tail = _tail;
    uint32_t head = _head;

    // Leggi senza consumare
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
    } else {
        return TIMELINE_BUFFER_SIZE - (tail - head);
    }
}

size_t TimelineBuffer::freeSpace() {
    return TIMELINE_BUFFER_SIZE - available() - 1;  // -1 per distinguere full/empty
}

void TimelineBuffer::clear() {
    _tail = _head;  // Reset: tail raggiunge head
}

bool TimelineBuffer::isEmpty() {
    return _head == _tail;
}

bool TimelineBuffer::isFull() {
    uint32_t next_head = wrapIndex(_head + 1);
    return next_head == _tail;
}
