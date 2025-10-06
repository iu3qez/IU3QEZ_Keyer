#ifndef KEYER_LOGIC_H
#define KEYER_LOGIC_H

#include <Arduino.h>
#include "settings.h"
#include "timeline_buffer.h"

// Stati del keyer (state machine)
enum KeyerState_t {
    KEYER_IDLE,              // Nessuna attività
    KEYER_DOT_ACTIVE,        // Generando DOT (key down)
    KEYER_DASH_ACTIVE,       // Generando DASH (key down)
    KEYER_INTER_ELEMENT,     // Pausa tra elementi (key up)
    KEYER_INTER_CHAR         // Pausa tra caratteri (key up)
};

// Elementi morse
enum Element_t {
    ELEMENT_NONE = 0,
    ELEMENT_DOT  = 1,
    ELEMENT_DASH = 2
};

// Callback per eventi keyer
typedef void (*KeyerCallback)(bool keying);

class KeyerLogic {
public:
    KeyerLogic();

    // Inizializzazione
    bool begin(KeyerCallback keyingCallback);

    // Configurazione
    void setWPM(uint8_t wpm);
    uint8_t getWPM() { return _wpm; }
    uint32_t getDotDuration() { return _dot_duration_us; }  // Per decoder

    void setMode(uint8_t mode);
    uint8_t getMode() { return _mode; }

    void setMemoryWindow(uint8_t up_percent, uint8_t down_percent);
    void getMemoryWindow(uint8_t* up, uint8_t* down) {
        *up = _window_up_percent;
        *down = _window_down_percent;
    }

    void setDebounce(uint32_t debounce_us);
    uint32_t getDebounce() { return _debounce_us; }

    // Stato
    KeyerState_t getState() { return _state; }
    bool isKeying() { return _keying; }

    // Debug
    void printStatus();
    bool getDotPressed() { return _dot_pressed; }
    bool getDashPressed() { return _dash_pressed; }
    uint32_t getDotISRCount() { return _dot_isr_count; }
    uint32_t getDashISRCount() { return _dash_isr_count; }

    // Timeline buffer access (legacy, per decoder)
    TimelineBuffer* getTimelineBuffer() { return _timeline_decoder; }

    // Configura timeline targets (decoder + websocket)
    void setTimelineTargets(TimelineBuffer* decoder_tl, TimelineBuffer* websocket_tl) {
        _timeline_decoder = decoder_tl;
        _timeline_websocket = websocket_tl;
    }

private:
    // Timing (calcolato da WPM)
    uint8_t _wpm;
    uint32_t _dot_duration_us;      // Durata DOT in microsecondi
    uint32_t _dash_duration_us;     // Durata DASH in microsecondi
    uint32_t _element_space_us;     // Pausa inter-element
    uint32_t _char_space_us;        // Pausa inter-character

    // Finestra memoria Curtis Mode B
    uint8_t _window_up_percent;     // U% - apertura finestra
    uint8_t _window_down_percent;   // D% - chiusura finestra (da fine)

    // Mode
    uint8_t _mode;

    // Debounce
    uint32_t _debounce_us;

    // State machine
    volatile KeyerState_t _state;
    volatile bool _keying;           // true = key down, false = key up

    // Memory latch (Curtis Mode B)
    volatile Element_t _memory_latch;     // Elemento memorizzato
    volatile Element_t _current_element;  // Elemento in corso

    // Timing corrente
    volatile uint32_t _element_start_us;  // Inizio elemento corrente
    volatile uint32_t _element_duration_us; // Durata elemento corrente

    // Paddle state (aggiornato da polling in timer ISR)
    volatile bool _dot_pressed;
    volatile bool _dash_pressed;
    volatile uint32_t _dot_last_change_us;
    volatile uint32_t _dash_last_change_us;

    // Tracking tempo di pressione (per memory latch stabile)
    volatile uint32_t _dot_press_start_us;   // Quando DOT è stato premuto
    volatile uint32_t _dash_press_start_us;  // Quando DASH è stato premuto

    // DEBUG: contatori ISR (legacy, non più usati)
    volatile uint32_t _dot_isr_count;
    volatile uint32_t _dash_isr_count;

    // Timer hardware
    hw_timer_t* _timer;
    KeyerCallback _callback;

    // Timeline buffers (puntatori esterni, broadcast a decoder + websocket)
    TimelineBuffer* _timeline_decoder;
    TimelineBuffer* _timeline_websocket;

    // Metodi privati
    void calculateTimings();
    bool isInMemoryWindow(uint32_t elapsed_us);
    void startElement(Element_t element);
    void processNextElement();

    // ISR (devono essere statiche per attachInterrupt)
    static void IRAM_ATTR timerISR();
    static void IRAM_ATTR dotPaddleISR();
    static void IRAM_ATTR dashPaddleISR();

    // Istanza singleton per ISR
    static KeyerLogic* _instance;
};

#endif // KEYER_LOGIC_H
