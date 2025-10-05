#ifndef GPIO_CONFIG_H
#define GPIO_CONFIG_H

#include <Arduino.h>
#include "settings.h"

// GPIO configuration
class GPIO_Config {
public:
    GPIO_Config();

    // Inizializzazione GPIO
    void begin();

    // Input paddle (lettura raw, prima del debouncing)
    inline bool readDotPaddle() { return digitalRead(DOT_PIN) == LOW; }
    inline bool readDashPaddle() { return digitalRead(DASH_PIN) == LOW; }

    // Output keying
    inline void setKeyOutput(bool state) { digitalWrite(KEY_PIN, state ? HIGH : LOW); }

    // Status LED (disabilitato, usiamo NeoPixel)
    inline void setStatusLED(bool state) { /* disabilitato */ }
    inline void toggleStatusLED() { /* disabilitato */ }

private:
    void configurePaddleInputs();
    void configureKeyOutput();
    void configureStatusLED();
};

#endif // GPIO_CONFIG_H
