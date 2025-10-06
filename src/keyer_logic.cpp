#include "keyer_logic.h"

// Istanza singleton per ISR
KeyerLogic* KeyerLogic::_instance = nullptr;

KeyerLogic::KeyerLogic()
    : _wpm(KEYER_WPM_DEFAULT),
      _dot_duration_us(0),
      _dash_duration_us(0),
      _element_space_us(0),
      _char_space_us(0),
      _window_up_percent(KEYER_MEMORY_WINDOW_UP),
      _window_down_percent(KEYER_MEMORY_WINDOW_DOWN),
      _mode(KEYER_MODE_DEFAULT),
      _debounce_us(PADDLE_DEBOUNCE_US),
      _state(KEYER_IDLE),
      _keying(false),
      _memory_latch(ELEMENT_NONE),
      _current_element(ELEMENT_NONE),
      _element_start_us(0),
      _element_duration_us(0),
      _dot_pressed(false),
      _dash_pressed(false),
      _dot_last_change_us(0),
      _dash_last_change_us(0),
      _dot_press_start_us(0),
      _dash_press_start_us(0),
      _dot_isr_count(0),
      _dash_isr_count(0),
      _timer(nullptr),
      _callback(nullptr) {
    _instance = this;
}

bool KeyerLogic::begin(KeyerCallback keyingCallback) {
    Serial.println("Inizializzazione Keyer Logic...");

    _callback = keyingCallback;

    // Calcola timing da WPM
    calculateTimings();

    // Configura GPIO come input (NO interrupt - faremo polling)
    pinMode(DOT_PIN, INPUT_PULLUP);
    pinMode(DASH_PIN, INPUT_PULLUP);

    // RIMOSSO: attachInterrupt - incompatibile con condensatori anti-rimbalzo
    // Le ISR GPIO CHANGE generano centinaia di interrupt spuri durante transizioni lente
    // Useremo polling nel timer ISR invece

    // Inizializza timer hardware
    // Timer 0, divider 80 = 1 MHz (1 tick = 1 us)
    _timer = timerBegin(0, KEYER_TIMER_DIVIDER, true);

    if (_timer == nullptr) {
        Serial.println("ERRORE: Creazione timer hardware fallita");
        return false;
    }

    // Attach ISR timer
    timerAttachInterrupt(_timer, &KeyerLogic::timerISR, true);

    // Timer continuous mode, 1000 us (1 ms) tick rate per state machine + paddle polling
    timerAlarmWrite(_timer, 1000, true);  // 1ms tick, auto-reload
    timerAlarmEnable(_timer);

    Serial.printf("Keyer inizializzato: %d WPM, Mode %d, WND U=%d%% D=%d%%\n",
                  _wpm, _mode, _window_up_percent, _window_down_percent);
    Serial.printf("Timing: DOT=%lu us, DASH=%lu us\n",
                  _dot_duration_us, _dash_duration_us);
    Serial.println("Paddle polling: 1ms in timer ISR (no GPIO interrupt)");

    return true;
}

void KeyerLogic::calculateTimings() {
    // PARIS standard: 1 WPM = 50 unità/minuto
    // Durata DOT (1 unità) = 60000000 us / (50 * WPM) = 1200000 / WPM us
    _dot_duration_us = 1200000UL / _wpm;
    _dash_duration_us = 3 * _dot_duration_us;
    _element_space_us = _dot_duration_us;
    _char_space_us = 3 * _dot_duration_us;
}

void KeyerLogic::setWPM(uint8_t wpm) {
    if (wpm < KEYER_WPM_MIN) wpm = KEYER_WPM_MIN;
    if (wpm > KEYER_WPM_MAX) wpm = KEYER_WPM_MAX;

    _wpm = wpm;
    calculateTimings();

    Serial.printf("WPM impostato a %d (DOT=%lu us)\n", _wpm, _dot_duration_us);
}

void KeyerLogic::setMode(uint8_t mode) {
    if (mode <= KEYER_MODE_ULTIMATIC) {
        _mode = mode;
        Serial.printf("Mode impostato a %d\n", _mode);
    }
}

void KeyerLogic::setDebounce(uint32_t debounce_us) {
    // Range: 100us - 10000us (0.1ms - 10ms)
    if (debounce_us < 100) debounce_us = 100;
    if (debounce_us > 10000) debounce_us = 10000;

    _debounce_us = debounce_us;
    Serial.printf("Debounce impostato a %lu us\n", _debounce_us);
}

void KeyerLogic::setMemoryWindow(uint8_t up_percent, uint8_t down_percent) {
    if (up_percent <= 100 && down_percent <= 100) {
        _window_up_percent = up_percent;
        _window_down_percent = down_percent;
        Serial.printf("Memory window: U=%d%%, D=%d%%\n", up_percent, down_percent);
    }
}

bool KeyerLogic::isInMemoryWindow(uint32_t elapsed_us) {
    // Calcola progresso elemento corrente (0-100%)
    uint32_t progress = (elapsed_us * 100) / _element_duration_us;

    // Finestra aperta da U% a (100-D)%
    uint32_t window_close = 100 - _window_down_percent;

    return (progress >= _window_up_percent && progress <= window_close);
}

void KeyerLogic::startElement(Element_t element) {
    _current_element = element;
    _element_start_us = micros();

    if (element == ELEMENT_DOT) {
        _state = KEYER_DOT_ACTIVE;
        _element_duration_us = _dot_duration_us;
        // Emetti evento elemento DOT per decoder
        _timeline.pushExtended(EVENT_ELEMENT_DOT);
    } else {
        _state = KEYER_DASH_ACTIVE;
        _element_duration_us = _dash_duration_us;
        // Emetti evento elemento DASH per decoder
        _timeline.pushExtended(EVENT_ELEMENT_DASH);
    }

    // Key down
    _keying = true;

    // Cattura evento timeline KEY_ON
    _timeline.push(EVENT_KEY_ON);

    if (_callback) {
        _callback(true);
    }
}

void KeyerLogic::processNextElement() {
    // Determina prossimo elemento da generare
    Element_t next_element = ELEMENT_NONE;

    if (_mode == KEYER_MODE_IAMBIC_B) {
        // Curtis Mode B: controlla memory latch first
        if (_memory_latch != ELEMENT_NONE) {
            next_element = _memory_latch;
            _memory_latch = ELEMENT_NONE;
        }
        // Poi controlla paddle attuali
        else if (_dot_pressed && !_dash_pressed) {
            next_element = ELEMENT_DOT;
        }
        else if (_dash_pressed && !_dot_pressed) {
            next_element = ELEMENT_DASH;
        }
        else if (_dot_pressed && _dash_pressed) {
            // Entrambi premuti: genera opposto dell'ultimo
            next_element = (_current_element == ELEMENT_DOT) ? ELEMENT_DASH : ELEMENT_DOT;
        }
    }

    if (next_element != ELEMENT_NONE) {
        // Inizia nuovo elemento
        startElement(next_element);
    } else {
        // Nessun elemento: torna IDLE
        // NOTA: KEY_OFF già emesso in DOT/DASH_ACTIVE quando elemento finisce
        // Non riemetterlo qui per evitare duplicati
        _state = KEYER_IDLE;
        _current_element = ELEMENT_NONE;
        _keying = false;
    }
}

// ============================================================================
// ISR - IRAM_ATTR per evitare flash cache miss
// ============================================================================

void IRAM_ATTR KeyerLogic::timerISR() {
    if (_instance == nullptr) return;

    KeyerLogic* k = _instance;
    uint32_t now = micros();

    // ========================================================================
    // POLLING PADDLE GPIO (sostituisce GPIO interrupt)
    // ========================================================================
    // Leggi stato GPIO corrente (Active LOW)
    bool dot_gpio = (digitalRead(DOT_PIN) == LOW);
    bool dash_gpio = (digitalRead(DASH_PIN) == LOW);

    // Debouncing DOT: aggiorna solo se passato debounce dall'ultimo cambio
    if (dot_gpio != k->_dot_pressed) {
        if ((now - k->_dot_last_change_us) >= k->_debounce_us) {
            k->_dot_pressed = dot_gpio;
            k->_dot_last_change_us = now;

            // Cattura evento timeline
            TimelineEventFlags flags = FLAG_NONE;
            if (k->_dot_pressed && k->_dash_pressed) {
                flags = FLAG_IAMBIC;  // Squeeze iambic
            }
            k->_timeline.push(dot_gpio ? EVENT_DOT_PRESS : EVENT_DOT_RELEASE, flags, now);

            // Traccia tempo di pressione
            if (dot_gpio) {
                k->_dot_press_start_us = now;  // DOT appena premuto
            }
        }
    }

    // Debouncing DASH: aggiorna solo se passato debounce dall'ultimo cambio
    if (dash_gpio != k->_dash_pressed) {
        if ((now - k->_dash_last_change_us) >= k->_debounce_us) {
            k->_dash_pressed = dash_gpio;
            k->_dash_last_change_us = now;

            // Cattura evento timeline
            TimelineEventFlags flags = FLAG_NONE;
            if (k->_dot_pressed && k->_dash_pressed) {
                flags = FLAG_IAMBIC;  // Squeeze iambic
            }
            k->_timeline.push(dash_gpio ? EVENT_DASH_PRESS : EVENT_DASH_RELEASE, flags, now);

            // Traccia tempo di pressione
            if (dash_gpio) {
                k->_dash_press_start_us = now;  // DASH appena premuto
            }
        }
    }

    // ========================================================================
    // State machine principale
    // ========================================================================
    switch (k->_state) {
        case KEYER_IDLE:
            // Check se paddle premuto
            if (k->_dot_pressed && !k->_dash_pressed) {
                k->startElement(ELEMENT_DOT);
            }
            else if (k->_dash_pressed && !k->_dot_pressed) {
                k->startElement(ELEMENT_DASH);
            }
            else if (k->_dot_pressed && k->_dash_pressed) {
                // Entrambi: inizia con DOT
                k->startElement(ELEMENT_DOT);
            }
            break;

        case KEYER_DOT_ACTIVE:
        case KEYER_DASH_ACTIVE: {
            uint32_t elapsed = now - k->_element_start_us;

            // Controlla memoria dentro finestra (solo Curtis Mode B)
            if (k->_mode == KEYER_MODE_IAMBIC_B && k->_memory_latch == ELEMENT_NONE) {
                if (k->isInMemoryWindow(elapsed)) {
                    // Dentro finestra: controlla paddle opposto
                    if (k->_current_element == ELEMENT_DOT && k->_dash_pressed) {
                        k->_memory_latch = ELEMENT_DASH;
                    }
                    else if (k->_current_element == ELEMENT_DASH && k->_dot_pressed) {
                        k->_memory_latch = ELEMENT_DOT;
                    }
                }
            }

            // Check fine elemento
            if (elapsed >= k->_element_duration_us) {
                // Key up
                k->_keying = false;

                // Cattura evento timeline KEY_OFF
                k->_timeline.push(EVENT_KEY_OFF);

                if (k->_callback) {
                    k->_callback(false);
                }

                // Vai a inter-element space
                k->_state = KEYER_INTER_ELEMENT;
                k->_element_start_us = now;
                k->_element_duration_us = k->_element_space_us;
            }
            break;
        }

        case KEYER_INTER_ELEMENT: {
            uint32_t elapsed = now - k->_element_start_us;

            if (elapsed >= k->_element_duration_us) {
                // Fine pausa: processa prossimo elemento
                k->processNextElement();
            }
            break;
        }

        case KEYER_INTER_CHAR:
            // TODO: implementare se necessario
            k->processNextElement();
            break;
    }
}

// ============================================================================
// GPIO ISR OBSOLETE - Non più usate (polling in timerISR invece)
// ============================================================================
// PROBLEMA: Con condensatori anti-rimbalzo, le transizioni lente generano
// centinaia di interrupt CHANGE spuri → stato paddle errato
// SOLUZIONE: Polling a 1ms in timerISR con debounce software

void IRAM_ATTR KeyerLogic::dotPaddleISR() {
    // NON PIÙ USATA - mantenuta solo per compatibilità header
    if (_instance == nullptr) return;
    KeyerLogic* k = _instance;
    k->_dot_isr_count++;  // Solo per debug
}

void IRAM_ATTR KeyerLogic::dashPaddleISR() {
    // NON PIÙ USATA - mantenuta solo per compatibilità header
    if (_instance == nullptr) return;
    KeyerLogic* k = _instance;
    k->_dash_isr_count++;  // Solo per debug
}

void KeyerLogic::printStatus() {
    Serial.printf("Keyer Status: WPM=%d, Mode=%d, State=%d, Keying=%d\n",
                  _wpm, _mode, _state, _keying);
    Serial.printf("  DOT=%d, DASH=%d, Memory=%d, Current=%d\n",
                  _dot_pressed, _dash_pressed, _memory_latch, _current_element);
    Serial.printf("  Timing: DOT=%lu us, DASH=%lu us\n",
                  _dot_duration_us, _dash_duration_us);
}
