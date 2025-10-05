#include "gpio_config.h"

GPIO_Config::GPIO_Config() {
}

void GPIO_Config::begin() {
    configurePaddleInputs();
    configureKeyOutput();
    // configureStatusLED();  // Disabilitato

    Serial.println("GPIO Configuration:");
    Serial.printf("  Paddle DOT  : GPIO%d (input, pull-up)\n", PIN_PADDLE_DOT);
    Serial.printf("  Paddle DASH : GPIO%d (input, pull-up)\n", PIN_PADDLE_DASH);
    Serial.printf("  Key Output  : GPIO%d (output)\n", PIN_KEY_OUTPUT);
    // Serial.printf("  Status LED  : disabilitato\n");
}

void GPIO_Config::configurePaddleInputs() {
    // Configura paddle inputs come INPUT_PULLUP
    // I paddle chiudono a massa quando premuti (active LOW)
    pinMode(PIN_PADDLE_DOT, INPUT_PULLUP);
    pinMode(PIN_PADDLE_DASH, INPUT_PULLUP);
}

void GPIO_Config::configureKeyOutput() {
    // Configura key output come OUTPUT, inizialmente LOW (non keying)
    pinMode(PIN_KEY_OUTPUT, OUTPUT);
    digitalWrite(PIN_KEY_OUTPUT, LOW);
}

void GPIO_Config::configureStatusLED() {
    // Disabilitato - usiamo NeoPixel invece
    // pinMode(PIN_STATUS_LED, OUTPUT);
    // digitalWrite(PIN_STATUS_LED, LOW);
}
