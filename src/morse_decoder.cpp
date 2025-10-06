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

    initMorseTable();
    Serial.printf("  Morse table caricata: %d caratteri\n", _morse_table.size());

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

        // Gestisci eventi estesi (ELEMENT_DOT/DASH)
        if (evt.type_extended != 0) {
            if (evt.type_extended & EVENT_ELEMENT_DOT) {
                // Aggiungi DOT al pattern corrente
                _current_char_pattern += '.';
            }
            else if (evt.type_extended & EVENT_ELEMENT_DASH) {
                // Aggiungi DASH al pattern corrente
                _current_char_pattern += '-';
            }
            else if (evt.type_extended & EVENT_DECODED_CHAR) {
                // Evento carattere decodificato (loop feedback), ignora
            }
            continue;  // Eventi estesi non hanno evt.type valido
        }

        // Gestisci eventi base
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
                }
                break;

            case EVENT_DOT_PRESS:
            case EVENT_DOT_RELEASE:
            case EVENT_DASH_PRESS:
            case EVENT_DASH_RELEASE:
                // Ignorati (eventi paddle fisici, non rilevanti per decoder)
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
        // Decodifica eventuale pattern pendente (carattere finale prima dello spazio)
        if (_current_char_pattern.length() > 0) {
            char decoded_char = decodePattern(_current_char_pattern);
            if (decoded_char != '\0') {
                _timeline->pushExtended(EVENT_DECODED_CHAR, (uint8_t)decoded_char, timestamp_us);
                Serial.printf("Decoded: '%s' -> '%c'\n", _current_char_pattern.c_str(), decoded_char);
            }
        }

        // Spazio inter-parola rilevato!
        _timeline->push(EVENT_SPACE_WORD, FLAG_NONE, timestamp_us);
        _word_spaces_detected++;

        // Emetti spazio come carattere
        _timeline->pushExtended(EVENT_DECODED_CHAR, (uint8_t)' ', timestamp_us);

        // Reset pattern corrente (nuova parola)
        _current_char_pattern = "";
    }
    // Controlla se pausa è spazio CARATTERE (3 DOT ± tolleranza)
    else if (isInRange(pause_duration_us, char_space_target, _char_space_tolerance)) {
        // Spazio inter-carattere rilevato!
        _timeline->push(EVENT_SPACE_CHAR, FLAG_NONE, timestamp_us);
        _char_spaces_detected++;

        // Decodifica pattern corrente → carattere
        if (_current_char_pattern.length() > 0) {
            char decoded_char = decodePattern(_current_char_pattern);

            // Emetti evento carattere decodificato
            if (decoded_char != '\0') {
                _timeline->pushExtended(EVENT_DECODED_CHAR, (uint8_t)decoded_char, timestamp_us);

                // Debug
                Serial.printf("Decoded: '%s' -> '%c'\n", _current_char_pattern.c_str(), decoded_char);
            }
        }

        // Reset pattern per nuovo carattere
        _current_char_pattern = "";
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

void MorseDecoder::initMorseTable() {
    // ITU-R M.1677-1 International Morse code
    // Letters
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

    // Numbers
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

    // Punctuation marks
    _morse_table[".-.-.-"] = '.';  // Full stop
    _morse_table["--..--"] = ',';  // Comma
    _morse_table["---..."] = ':';  // Colon
    _morse_table["..--.."] = '?';  // Question mark
    _morse_table[".----."] = '\''; // Apostrophe
    _morse_table["-....-"] = '-';  // Hyphen
    _morse_table["-..-."] = '/';   // Slash
    _morse_table["-.--."] = '(';   // Left parenthesis
    _morse_table["-.--.-"] = ')';  // Right parenthesis
    _morse_table["-...-"] = '=';   // Equal sign

    // Special characters (accented)
    _morse_table[".-..-"] = 'È';   // È (Italian)
    _morse_table["---."] = 'Ó';    // Ó
    _morse_table["..--"] = 'Ü';    // Ü
}

char MorseDecoder::decodePattern(const String& pattern) {
    if (pattern.length() == 0) {
        return '\0';  // Pattern vuoto
    }

    auto it = _morse_table.find(pattern);
    if (it != _morse_table.end()) {
        return it->second;  // Carattere trovato
    }

    return '?';  // Pattern non riconosciuto
}
