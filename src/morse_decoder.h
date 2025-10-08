#ifndef MORSE_DECODER_H
#define MORSE_DECODER_H

#include <Arduino.h>
#include <map>
#include "timeline_buffer.h"
#include "settings.h"

// ============================================================================
// MORSE DECODER - SPACE DETECTION & CHARACTER RECOGNITION
// ============================================================================
//
// Analizza timeline eventi per rilevare:
// - Spazi inter-carattere (3 DOT pause)
// - Spazi inter-parola (7 DOT pause)
// - Pattern DOT/DASH → caratteri Morse (future)
//
// Design:
// - Task su Core 1 (non-ISR)
// - Legge timeline buffer popolato da KeyerLogic (Core 0)
// - Emette eventi SPACE_CHAR e SPACE_WORD nella timeline
// - Future: decodifica caratteri e statistiche timing
//
// Space Detection Algorithm:
// - Traccia ultimo EVENT_KEY_OFF timestamp
// - Al prossimo EVENT_KEY_ON: calcola pause duration
// - Classifica pause:
//   * < 3*DOT (con tolleranza) → stesso carattere, nessun evento
//   * ≈ 3*DOT (con tolleranza) → SPACE_CHAR → nuovo carattere
//   * ≈ 7*DOT (con tolleranza) → SPACE_WORD → nuova parola
//   * > range SPACE_WORD → timeout, reset decoder state
//
// ============================================================================

class MorseDecoder {
public:
    MorseDecoder(TimelineBuffer* timeline);

    // Configura timeline targets (per broadcast eventi SPACE e DECODED_CHAR)
    void setWebSocketTimeline(TimelineBuffer* websocket_tl) {
        _timeline_websocket = websocket_tl;
    }

    // Inizializzazione
    bool begin();

    // Configurazione (da config manager)
    void setCharSpaceTolerance(uint8_t percent);  // Tolleranza spazio carattere (%)
    void setWordSpaceTolerance(uint8_t percent);  // Tolleranza spazio parola (%)
    void setCharSpaceDots(uint8_t dots);          // Durata spazio carattere (in DOT, default 3)
    void setWordSpaceDots(uint8_t dots);          // Durata spazio parola (in DOT, default 7)
    void setDotDuration(uint32_t dot_duration_us); // Durata DOT (da KeyerLogic)

    uint8_t getCharSpaceTolerance() { return _char_space_tolerance; }
    uint8_t getWordSpaceTolerance() { return _word_space_tolerance; }
    uint8_t getCharSpaceDots() { return _char_space_dots; }
    uint8_t getWordSpaceDots() { return _word_space_dots; }

    // Task principale (chiamato da loop o FreeRTOS task)
    void process();

    // Check timeout per spazi (chiamare periodicamente dal loop)
    void checkTimeout();

    // Reset decoder state
    void reset();

    // Statistiche
    uint32_t getCharSpacesDetected() { return _char_spaces_detected; }
    uint32_t getWordSpacesDetected() { return _word_spaces_detected; }

private:
    TimelineBuffer* _timeline;           // Timeline decoder (lettura eventi keyer)
    TimelineBuffer* _timeline_websocket; // Timeline WebSocket (broadcast eventi SPACE/DECODED_CHAR)

    // Configurazione
    uint8_t _char_space_tolerance;  // Tolleranza % per spazio carattere (default 20%)
    uint8_t _word_space_tolerance;  // Tolleranza % per spazio parola (default 20%)
    uint8_t _char_space_dots;       // Durata spazio carattere in DOT (default 3)
    uint8_t _word_space_dots;       // Durata spazio parola in DOT (default 7)

    // State tracking
    uint32_t _last_key_off_us;      // Timestamp ultimo EVENT_KEY_OFF
    bool _key_is_down;              // Stato corrente KEY (true se premuto)
    uint32_t _dot_duration_us;      // Durata DOT corrente (da KeyerLogic WPM)
    bool _timeout_decoded;          // Flag: checkTimeout ha già decodificato questo pattern

    // Pattern recognition
    String _current_char_pattern;   // Pattern corrente in costruzione (".-.-" etc)
    uint32_t _last_element_start_us;

    // Lookup table pattern → carattere
    std::map<String, char> _morse_table;

    // Statistiche
    uint32_t _char_spaces_detected;
    uint32_t _word_spaces_detected;

    // Helper per space detection
    void detectSpace(uint32_t pause_duration_us, uint32_t timestamp_us);
    bool isInRange(uint32_t value, uint32_t target, uint8_t tolerance_percent);

    // Helper per pattern recognition
    void initMorseTable();
    char decodePattern(const String& pattern);
};

#endif // MORSE_DECODER_H
