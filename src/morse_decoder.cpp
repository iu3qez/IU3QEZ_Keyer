#include "morse_decoder.h"

MorseDecoder::MorseDecoder(TimelineBuffer* timeline)
    : _timeline(timeline),
      _char_space_tolerance(DECODER_CHAR_SPACE_TOLERANCE),
      _word_space_tolerance(DECODER_WORD_SPACE_TOLERANCE),
      _last_key_off_us(0),
      _key_is_down(false),
      _dot_duration_us(0),
      _last_element_start_us(0),
      _char_spaces_detected(0),
      _word_spaces_detected(0) {
}

bool MorseDecoder::begin() {
    Serial.println("Inizializzazione Morse Decoder...");
    Serial.printf("  Char space tolerance: %d%%\n", _char_space_tolerance);
    Serial.printf("  Word space tolerance: %d%%\n", _word_space_tolerance);

    reset();
    return true;
}

void MorseDecoder::setCharSpaceTolerance(uint8_t percent) {
    if (percent > 100) percent = 100;
    _char_space_tolerance = percent;
    Serial.printf("Char space tolerance: %d%%\n", _char_space_tolerance);
}

void MorseDecoder::setWordSpaceTolerance(uint8_t percent) {
    if (percent > 100) percent = 100;
    _word_space_tolerance = percent;
    Serial.printf("Word space tolerance: %d%%\n", _word_space_tolerance);
}

void MorseDecoder::setDotDuration(uint32_t dot_duration_us) {
    _dot_duration_us = dot_duration_us;
}

void MorseDecoder::reset() {
    _last_key_off_us = 0;
    _key_is_down = false;
    _current_char_pattern = "";
    _last_element_start_us = 0;
}

void MorseDecoder::process() {
    // Leggi eventi dalla timeline (max 32 per chiamata)
    TimelineEvent events[32];
    size_t num_events = _timeline->read(events, 32);

    for (size_t i = 0; i < num_events; i++) {
        TimelineEvent& evt = events[i];

        switch (evt.type) {
            case EVENT_KEY_ON:
                // KEY appena attivato
                if (!_key_is_down) {
                    _key_is_down = true;

                    // Se c'è stato un KEY_OFF precedente, calcola pausa
                    if (_last_key_off_us > 0) {
                        uint32_t pause_duration_us = evt.timestamp_us - _last_key_off_us;
                        detectSpace(pause_duration_us, evt.timestamp_us);
                    }

                    _last_element_start_us = evt.timestamp_us;
                }
                break;

            case EVENT_KEY_OFF:
                // KEY appena disattivato
                if (_key_is_down) {
                    _key_is_down = false;
                    _last_key_off_us = evt.timestamp_us;

                    // Future: analizza durata elemento per pattern DOT/DASH
                    // uint32_t element_duration = evt.timestamp_us - _last_element_start_us;
                }
                break;

            case EVENT_DOT_PRESS:
            case EVENT_DOT_RELEASE:
            case EVENT_DASH_PRESS:
            case EVENT_DASH_RELEASE:
                // Ignorati per space detection (già gestiti da KEY_ON/OFF)
                break;

            case EVENT_SPACE_CHAR:
            case EVENT_SPACE_WORD:
                // Eventi già emessi dal decoder (loop feedback), ignora
                break;

            default:
                break;
        }
    }
}

void MorseDecoder::detectSpace(uint32_t pause_duration_us, uint32_t timestamp_us) {
    // Se DOT duration non ancora impostato, skip (keyer non ancora configurato)
    if (_dot_duration_us == 0) {
        return;
    }

    // Calcola target per spazio carattere e parola
    uint32_t char_space_target = _dot_duration_us * 3;  // 3 DOT
    uint32_t word_space_target = _dot_duration_us * 7;  // 7 DOT

    // Controlla se pausa è spazio PAROLA (7 DOT ± tolleranza)
    // Controlla prima WORD perché ha priorità su CHAR
    if (isInRange(pause_duration_us, word_space_target, _word_space_tolerance)) {
        // Spazio inter-parola rilevato!
        _timeline->push(EVENT_SPACE_WORD, FLAG_NONE, timestamp_us);
        _word_spaces_detected++;

        // Reset pattern corrente (nuova parola)
        _current_char_pattern = "";

        // Debug (rimuovi in produzione per performance)
        // Serial.printf("SPACE_WORD detected: pause=%lu us (target=%lu, tol=%d%%)\n",
        //               pause_duration_us, word_space_target, _word_space_tolerance);
    }
    // Controlla se pausa è spazio CARATTERE (3 DOT ± tolleranza)
    else if (isInRange(pause_duration_us, char_space_target, _char_space_tolerance)) {
        // Spazio inter-carattere rilevato!
        _timeline->push(EVENT_SPACE_CHAR, FLAG_NONE, timestamp_us);
        _char_spaces_detected++;

        // Future: qui si decodifica _current_char_pattern → carattere
        // e si resetta pattern per nuovo carattere
        _current_char_pattern = "";

        // Debug (rimuovi in produzione per performance)
        // Serial.printf("SPACE_CHAR detected: pause=%lu us (target=%lu, tol=%d%%)\n",
        //               pause_duration_us, char_space_target, _char_space_tolerance);
    }
    // Pausa troppo lunga → timeout, reset decoder
    else if (pause_duration_us > (word_space_target * 2)) {
        // Timeout: pausa molto lunga, resetta stato
        _current_char_pattern = "";
        // Serial.printf("Decoder timeout: pause=%lu us (too long)\n", pause_duration_us);
    }
    // Pausa troppo corta → inter-element space o parte dello stesso carattere
    // Nessuna azione richiesta
}

bool MorseDecoder::isInRange(uint32_t value, uint32_t target, uint8_t tolerance_percent) {
    // Calcola range con tolleranza: target ± (target * tolerance / 100)
    uint32_t delta = (target * tolerance_percent) / 100;
    uint32_t min_value = target - delta;
    uint32_t max_value = target + delta;

    return (value >= min_value && value <= max_value);
}
