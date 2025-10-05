#ifndef NEOPIXEL_DEBUG_H
#define NEOPIXEL_DEBUG_H

#include <Arduino.h>
#include <FastLED.h>

#define BLINK_GPIO      38
#define NUM_LEDS        7
#define LED_BRIGHTNESS  50  // 0-255

// Stati visivi del keyer
enum KeyerState {
    STATE_IDLE,           // Idle - lampeggiante
    STATE_DOT_PRESSED,    // Dot paddle premuto
    STATE_DASH_PRESSED,   // Dash paddle premuto
    STATE_BOTH_PRESSED,   // Entrambi paddle premuti
    STATE_KEYING_DOT,     // Sta trasmettendo dot
    STATE_KEYING_DASH,    // Sta trasmettendo dash
    STATE_ERROR           // Errore
};

class NeoPixel_Debug {
public:
    NeoPixel_Debug();

    void begin();
    void update();  // Chiamare nel loop per aggiornare animazioni

    void setState(KeyerState state);
    KeyerState getState() { return _currentState; }

    // Controllo diretto (per debug avanzato)
    void setLED(uint8_t index, CRGB color);
    void setAll(CRGB color);
    void clear();
    void show();

private:
    CRGB _leds[NUM_LEDS];
    KeyerState _currentState;
    unsigned long _lastUpdate;
    uint8_t _animationPhase;

    void updateIdle();
    void updateDotPressed();
    void updateDashPressed();
    void updateBothPressed();
    void updateKeyingDot();
    void updateKeyingDash();
    void updateError();
};

#endif // NEOPIXEL_DEBUG_H
