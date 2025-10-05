#include "neopixel_debug.h"

NeoPixel_Debug::NeoPixel_Debug()
    : _currentState(STATE_IDLE), _lastUpdate(0), _animationPhase(0) {
}

void NeoPixel_Debug::begin() {
    FastLED.addLeds<WS2812B, BLINK_GPIO, GRB>(_leds, NUM_LEDS);
    FastLED.setBrightness(LED_BRIGHTNESS);

    // Test iniziale: arcobaleno rapido
    for (int i = 0; i < NUM_LEDS; i++) {
        _leds[i] = CHSV(i * 255 / NUM_LEDS, 255, 255);
    }
    FastLED.show();
    delay(500);

    clear();
    Serial.printf("NeoPixel Debug: %d LEDs on GPIO%d\n", NUM_LEDS, BLINK_GPIO);
}

void NeoPixel_Debug::setState(KeyerState state) {
    if (_currentState != state) {
        _currentState = state;
        _animationPhase = 0;  // Reset animazione
    }
}

void NeoPixel_Debug::update() {
    unsigned long now = millis();

    // Aggiorna animazione ogni 50ms
    if (now - _lastUpdate < 50) {
        return;
    }
    _lastUpdate = now;
    _animationPhase++;

    switch (_currentState) {
        case STATE_IDLE:
            updateIdle();
            break;
        case STATE_DOT_PRESSED:
            updateDotPressed();
            break;
        case STATE_DASH_PRESSED:
            updateDashPressed();
            break;
        case STATE_BOTH_PRESSED:
            updateBothPressed();
            break;
        case STATE_KEYING_DOT:
            updateKeyingDot();
            break;
        case STATE_KEYING_DASH:
            updateKeyingDash();
            break;
        case STATE_ERROR:
            updateError();
            break;
    }

    FastLED.show();
}

void NeoPixel_Debug::updateIdle() {
    // Breathing blu lento su primo LED
    uint8_t brightness = beatsin8(20, 10, 255);  // ~3 sec per ciclo
    _leds[0] = CRGB::Blue;
    _leds[0].nscale8(brightness);

    for (int i = 1; i < NUM_LEDS; i++) {
        _leds[i] = CRGB::Black;
    }
}

void NeoPixel_Debug::updateDotPressed() {
    // Verde sui primi 3 LED (DOT = corto)
    for (int i = 0; i < 3; i++) {
        _leds[i] = CRGB::Green;
    }
    for (int i = 3; i < NUM_LEDS; i++) {
        _leds[i] = CRGB::Black;
    }
}

void NeoPixel_Debug::updateDashPressed() {
    // Giallo su tutti i LED (DASH = lungo)
    for (int i = 0; i < NUM_LEDS; i++) {
        _leds[i] = CRGB::Yellow;
    }
}

void NeoPixel_Debug::updateBothPressed() {
    // Alternanza arancione-viola (iambic mode)
    CRGB color = (_animationPhase % 10 < 5) ? CRGB::Orange : CRGB::Purple;
    for (int i = 0; i < NUM_LEDS; i++) {
        _leds[i] = color;
    }
}

void NeoPixel_Debug::updateKeyingDot() {
    // Verde brillante pulsante (sta trasmettendo dot)
    uint8_t brightness = beatsin8(60, 128, 255);  // Pulsazione veloce
    for (int i = 0; i < 3; i++) {
        _leds[i] = CRGB::Lime;
        _leds[i].nscale8(brightness);
    }
    for (int i = 3; i < NUM_LEDS; i++) {
        _leds[i] = CRGB::Black;
    }
}

void NeoPixel_Debug::updateKeyingDash() {
    // Giallo brillante pulsante (sta trasmettendo dash)
    uint8_t brightness = beatsin8(60, 128, 255);  // Pulsazione veloce
    for (int i = 0; i < NUM_LEDS; i++) {
        _leds[i] = CRGB::Gold;
        _leds[i].nscale8(brightness);
    }
}

void NeoPixel_Debug::updateError() {
    // Rosso lampeggiante veloce
    CRGB color = (_animationPhase % 4 < 2) ? CRGB::Red : CRGB::Black;
    for (int i = 0; i < NUM_LEDS; i++) {
        _leds[i] = color;
    }
}

void NeoPixel_Debug::setLED(uint8_t index, CRGB color) {
    if (index < NUM_LEDS) {
        _leds[index] = color;
    }
}

void NeoPixel_Debug::setAll(CRGB color) {
    for (int i = 0; i < NUM_LEDS; i++) {
        _leds[i] = color;
    }
}

void NeoPixel_Debug::clear() {
    setAll(CRGB::Black);
    FastLED.show();
}

void NeoPixel_Debug::show() {
    FastLED.show();
}
