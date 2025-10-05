#ifndef POWER_AMP_H
#define POWER_AMP_H

#include <Arduino.h>
#include <Wire.h>
#include "settings.h"

// TCA9555 Register addresses
#define TCA9555_INPUT_PORT0     0x00
#define TCA9555_INPUT_PORT1     0x01
#define TCA9555_OUTPUT_PORT0    0x02
#define TCA9555_OUTPUT_PORT1    0x03
#define TCA9555_POLARITY_PORT0  0x04
#define TCA9555_POLARITY_PORT1  0x05
#define TCA9555_CONFIG_PORT0    0x06
#define TCA9555_CONFIG_PORT1    0x07

// Pin mapping TCA9555:
// Extend_IO7 = P0.6 (I2C_USB_SEL_PIN) -> I2C/USB selector (HIGH per I2C)
// Extend_IO8 = P1.0 (PA_ENABLE_PIN) -> Power Amplifier enable

class PowerAmplifier {
public:
    PowerAmplifier();

    // Inizializzazione (usa pin da schematico)
    bool begin();

    // Controllo PA
    void enable();
    void disable();
    bool isEnabled();

    // Test I/O (Extend_IO10 = P1.1)
    bool readTestInput();  // Legge P1.1 (Extend_IO10)

    // Test PA - prova tutti i pin di Port1
    void testAllPort1Pins();

private:
    bool _enabled;

    // Low-level I2C
    bool writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
};

#endif // POWER_AMP_H
