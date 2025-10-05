#ifndef GPIO_CONFIG_H
#define GPIO_CONFIG_H

#include <Arduino.h>

// Pin assignments based on PINOUT.png
// Input paddle pins (active LOW with internal pull-up)
#define PIN_PADDLE_DOT      3   // GPIO3 - Dot/Dit paddle input
#define PIN_PADDLE_DASH     4   // GPIO4 - Dash/Dah paddle input

// Output keying pin
#define PIN_KEY_OUTPUT      5   // GPIO5 - Keying output to radio

// Optional status LED (disabilitato - GPIO6 usato da ES8311, usiamo NeoPixel)
// #define PIN_STATUS_LED      1   // Disabilitato per evitare conflitti

// GPIO configuration
class GPIO_Config {
public:
    GPIO_Config();

    // Inizializzazione GPIO
    void begin();

    // Input paddle (lettura raw, prima del debouncing)
    inline bool readDotPaddle() { return digitalRead(PIN_PADDLE_DOT) == LOW; }
    inline bool readDashPaddle() { return digitalRead(PIN_PADDLE_DASH) == LOW; }

    // Output keying
    inline void setKeyOutput(bool state) { digitalWrite(PIN_KEY_OUTPUT, state ? HIGH : LOW); }

    // Status LED (disabilitato, usiamo NeoPixel)
    inline void setStatusLED(bool state) { /* disabilitato */ }
    inline void toggleStatusLED() { /* disabilitato */ }

private:
    void configurePaddleInputs();
    void configureKeyOutput();
    void configureStatusLED();
};

#endif // GPIO_CONFIG_H
