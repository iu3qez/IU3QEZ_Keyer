#include "gpio_config.h"

GPIO_Config::GPIO_Config() {
}

void GPIO_Config::begin() {
    configurePaddleInputs();
    configureKeyOutput();
    // configureStatusLED();  // Disabilitato

    Serial.println("GPIO Configuration:");
    Serial.printf("  Paddle DOT  : GPIO%d (input, pull-up)\n", DOT_PIN);
    Serial.printf("  Paddle DASH : GPIO%d (input, pull-up)\n", DASH_PIN);
    Serial.printf("  Key Output  : GPIO%d (output)\n", KEY_PIN);
    // Serial.printf("  Status LED  : disabilitato\n");
}

void GPIO_Config::configurePaddleInputs() {
    // Configura paddle inputs come INPUT_PULLUP
    // I paddle chiudono a massa quando premuti (active LOW)
    pinMode(DOT_PIN, INPUT_PULLUP);
    pinMode(DASH_PIN, INPUT_PULLUP);
}

void GPIO_Config::configureKeyOutput() {
    // Configura key output come OUTPUT, inizialmente LOW (non keying)
    pinMode(KEY_PIN, OUTPUT);
    digitalWrite(KEY_PIN, LOW);
}

void GPIO_Config::configureStatusLED() {
    // Disabilitato - usiamo NeoPixel invece
    // pinMode(PIN_STATUS_LED, OUTPUT);
    // digitalWrite(PIN_STATUS_LED, LOW);
}
