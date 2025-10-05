#ifndef TIMELINE_BUFFER_H
#define TIMELINE_BUFFER_H

#include <Arduino.h>

// ============================================================================
// TIMELINE EVENT BUFFER - LOCKFREE CIRCULAR BUFFER
// ============================================================================
//
// Buffer circolare lockfree per cattura eventi paddle/output da ISR.
// Thread-safe: scrittura da ISR (Core 0), lettura da WebSocket task (Core 1)
//
// Design:
// - Circular buffer con head/tail atomici
// - Nessun mutex/semaphore (lockfree per ISR safety)
// - Overwrite policy: eventi vecchi vengono sovrascritti se buffer pieno
// - Timestamp in microsecondi per ricostruzione timeline precisa
//
// Uso futuro:
// - Decoder Morse (analisi pattern temporali)
// - Analisi statistiche (timing precision, iambic detection)
// - Recording/replay sessioni
// - Debug paddle bouncing
// ============================================================================

// Tipi di eventi timeline
enum TimelineEventType : uint8_t {
    EVENT_DOT_PRESS     = 0x01,   // DOT paddle premuto
    EVENT_DOT_RELEASE   = 0x02,   // DOT paddle rilasciato
    EVENT_DASH_PRESS    = 0x04,   // DASH paddle premuto
    EVENT_DASH_RELEASE  = 0x08,   // DASH paddle rilasciato
    EVENT_KEY_ON        = 0x10,   // Output KEY attivato
    EVENT_KEY_OFF       = 0x20,   // Output KEY disattivato
};

// Flags aggiuntivi per eventi
enum TimelineEventFlags : uint8_t {
    FLAG_NONE           = 0x00,
    FLAG_IAMBIC         = 0x01,   // Entrambi paddle premuti (squeeze)
    FLAG_MEMORY_LATCH   = 0x02,   // Memory latch triggerato
    FLAG_DEBOUNCE_SKIP  = 0x04,   // Evento scartato per debouncing
};

// Struttura evento timeline (8 bytes - ottimizzata per cache)
struct TimelineEvent {
    uint32_t timestamp_us;        // Timestamp in microsecondi (wraps ogni ~71 min)
    TimelineEventType type;       // Tipo evento
    TimelineEventFlags flags;     // Flags aggiuntivi
    uint8_t reserved[2];          // Padding per allineamento a 8 bytes
};

// Dimensione buffer (power of 2 per ottimizzazione modulo)
#define TIMELINE_BUFFER_SIZE 1024   // 1024 eventi * 8 bytes = 8 KB

class TimelineBuffer {
public:
    TimelineBuffer();

    // Scrittura evento (chiamata da ISR - IRAM_ATTR)
    void IRAM_ATTR push(TimelineEventType type, TimelineEventFlags flags = FLAG_NONE);
    void IRAM_ATTR push(TimelineEventType type, TimelineEventFlags flags, uint32_t timestamp_us);

    // Lettura eventi (chiamata da WebSocket task)
    // Ritorna numero di eventi letti
    size_t read(TimelineEvent* buffer, size_t max_events);

    // Peek senza consumare (per statistiche)
    size_t peek(TimelineEvent* buffer, size_t max_events);

    // Utility
    size_t available();           // Eventi disponibili per lettura
    size_t freeSpace();           // Spazio libero nel buffer
    void clear();                 // Svuota buffer
    bool isEmpty();
    bool isFull();

    // Statistiche
    uint32_t getTotalPushed() { return _total_pushed; }
    uint32_t getTotalDropped() { return _total_dropped; }
    uint32_t getOverruns() { return _overruns; }

private:
    TimelineEvent _buffer[TIMELINE_BUFFER_SIZE];

    // Indici atomici (volatile per visibilità cross-core)
    volatile uint32_t _head;      // Prossima posizione scrittura (ISR)
    volatile uint32_t _tail;      // Prossima posizione lettura (task)

    // Statistiche
    volatile uint32_t _total_pushed;
    volatile uint32_t _total_dropped;
    volatile uint32_t _overruns;

    // Helper inline per modulo power-of-2
    inline uint32_t wrapIndex(uint32_t index) {
        return index & (TIMELINE_BUFFER_SIZE - 1);
    }
};

#endif // TIMELINE_BUFFER_H
