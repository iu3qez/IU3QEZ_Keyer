#include "morse_decoder.h"

// Helper privati per broadcast eventi decoder a timeline multiple
static inline void decoderBroadcastPush(TimelineBuffer* tl_decoder, TimelineBuffer* tl_websocket,
                                         TimelineEventType type, TimelineEventFlags flags, uint32_t timestamp_us) {
    // Decoder scrive SOLO su websocket timeline (decoder timeline serve solo per leggere eventi keyer)
    if (tl_websocket) tl_websocket->push(type, flags, timestamp_us);
}

static inline void decoderBroadcastPushExtended(TimelineBuffer* tl_decoder, TimelineBuffer* tl_websocket,
                                                 TimelineEventTypeExtended type_extended, uint8_t payload, uint32_t timestamp_us) {
    // DEBUG: stampa carattere prima di inviare al WebSocket
    if (type_extended & EVENT_DECODED_CHAR) {
        Serial.printf("[WS_PUSH] char='%c' (0x%02X) ts=%lu\n", (char)payload, payload, timestamp_us);
    }

    // Decoder scrive SOLO su websocket timeline
    if (tl_websocket) tl_websocket->pushExtended(type_extended, payload, timestamp_us);
}

MorseDecoder::MorseDecoder(TimelineBuffer* timeline)
    : _timeline(timeline),
      _timeline_websocket(nullptr),
      _char_space_tolerance(DECODER_CHAR_SPACE_TOLERANCE),
      _word_space_tolerance(DECODER_WORD_SPACE_TOLERANCE),
      _last_key_off_us(0),
      _key_is_down(false),
      _dot_duration_us(0),
      _timeout_decoded(false),
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

void MorseDecoder::checkTimeout() {
    // Se nessun DOT duration o nessun KEY_OFF precedente, skip
    if (_dot_duration_us == 0 || _last_key_off_us == 0 || _key_is_down) {
        return;
    }

    // Se non c'è pattern pendente o già decodificato, niente da fare
    if (_current_char_pattern.length() == 0 || _timeout_decoded) {
        return;
    }

    // Calcola tempo passato dall'ultimo KEY_OFF
    uint32_t now_us = micros();
    uint32_t elapsed_us = now_us - _last_key_off_us;

    // Calcola target per spazio carattere e parola
    uint32_t char_space_target = _dot_duration_us * 3;
    uint32_t word_space_target = _dot_duration_us * 7;

    // Se è passato abbastanza tempo per uno spazio carattere
    if (elapsed_us >= char_space_target) {
        // DEBUG: marca quando timeout rileva lo spazio
        Serial.print("[T]");

        // Determina se è spazio carattere o parola
        if (elapsed_us >= word_space_target) {
            // Spazio parola
            char decoded_char = decodePattern(_current_char_pattern);
            if (decoded_char != '\0') {
                Serial.printf(" = '%c' ", decoded_char);
            } else {
                Serial.print(" = ? ");
            }
            Serial.print("|| ");
        } else {
            // Spazio carattere
            char decoded_char = decodePattern(_current_char_pattern);
            if (decoded_char != '\0') {
                Serial.printf(" = '%c' | ", decoded_char);
            } else {
                Serial.print(" = ? | ");
            }
        }

        // Reset pattern e setta flag per evitare ri-decodifica
        _current_char_pattern = "";
        _timeout_decoded = true;  // Previene che detectSpace() lo ri-decodifichi
    }
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
                Serial.print(".");  // Stampa tono riconosciuto
                // Reset timeout flag - il pattern è stato esteso, timeout non deve decodificare
                _timeout_decoded = false;
            }
            else if (evt.type_extended & EVENT_ELEMENT_DASH) {
                // Aggiungi DASH al pattern corrente
                _current_char_pattern += '-';
                Serial.print("-");  // Stampa tono riconosciuto
                // Reset timeout flag - il pattern è stato esteso, timeout non deve decodificare
                _timeout_decoded = false;
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

                    // Se c'è stato un KEY_OFF precedente E timeout non ha già decodificato, calcola pausa
                    if (_last_key_off_us > 0 && !_timeout_decoded) {
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
                    _timeout_decoded = false;  // Reset flag per nuovo ciclo
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

    // Se la pausa è assurdamente lunga (> 2 secondi), ignora (timestamp corrotto o primo evento)
    if (pause_duration_us > 2000000) {
        return;
    }

    // Controlla se pausa è spazio PAROLA (7 DOT ± tolleranza)
    // Controlla prima WORD perché ha priorità su CHAR
    if (isInRange(pause_duration_us, word_space_target, _word_space_tolerance)) {
        // Decodifica eventuale pattern pendente (carattere finale prima dello spazio)
        if (_current_char_pattern.length() > 0) {
            char decoded_char = decodePattern(_current_char_pattern);
            if (decoded_char != '\0') {
                decoderBroadcastPushExtended(_timeline, _timeline_websocket, EVENT_DECODED_CHAR, (uint8_t)decoded_char, timestamp_us);
                Serial.printf(" = '%c' ", decoded_char);  // Stampa decodifica
            } else {
                Serial.print(" = ? ");  // Pattern non riconosciuto
            }
        }

        // Spazio inter-parola rilevato!
        decoderBroadcastPush(_timeline, _timeline_websocket, EVENT_SPACE_WORD, FLAG_NONE, timestamp_us);
        _word_spaces_detected++;

        // Emetti spazio come carattere
        decoderBroadcastPushExtended(_timeline, _timeline_websocket, EVENT_DECODED_CHAR, (uint8_t)' ', timestamp_us);
        Serial.print("|| ");  // Stampa spazio parola

        // Reset pattern corrente (nuova parola)
        _current_char_pattern = "";
    }
    // Controlla se pausa è spazio CARATTERE (3 DOT ± tolleranza)
    else if (isInRange(pause_duration_us, char_space_target, _char_space_tolerance)) {
        // Spazio inter-carattere rilevato!
        decoderBroadcastPush(_timeline, _timeline_websocket, EVENT_SPACE_CHAR, FLAG_NONE, timestamp_us);
        _char_spaces_detected++;

        // Decodifica pattern corrente → carattere
        if (_current_char_pattern.length() > 0) {
            char decoded_char = decodePattern(_current_char_pattern);

            // Emetti evento carattere decodificato
            if (decoded_char != '\0') {
                decoderBroadcastPushExtended(_timeline, _timeline_websocket, EVENT_DECODED_CHAR, (uint8_t)decoded_char, timestamp_us);
                Serial.printf(" = '%c' | ", decoded_char);  // Stampa decodifica + spazio carattere
            } else {
                Serial.print(" = ? | ");  // Pattern non riconosciuto + spazio carattere
            }
        }

        // Reset pattern per nuovo carattere
        _current_char_pattern = "";
    }
    // Pausa troppo lunga → timeout, decodifica eventuale pattern prima di resettare
    else if (pause_duration_us > (word_space_target * 2)) {
        // Decodifica eventuale pattern pendente prima del timeout
        if (_current_char_pattern.length() > 0) {
            char decoded_char = decodePattern(_current_char_pattern);
            if (decoded_char != '\0') {
                decoderBroadcastPushExtended(_timeline, _timeline_websocket, EVENT_DECODED_CHAR, (uint8_t)decoded_char, timestamp_us);
                Serial.printf(" = '%c' ", decoded_char);
            } else {
                Serial.print(" = ? ");
            }
        }

        // Timeout: pausa molto lunga, resetta stato e stampa spazio parola
        Serial.print("|| ");
        _current_char_pattern = "";
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
