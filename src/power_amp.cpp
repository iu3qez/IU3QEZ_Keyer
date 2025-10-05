#include "power_amp.h"

PowerAmplifier::PowerAmplifier() : _enabled(false) {
}

bool PowerAmplifier::begin() {
    Serial.println("Inizializzazione Power Amplifier (TCA9555)...");

    // TCA9555 è sullo stesso bus I2C di ES8311 (Wire, GPIO10/11)
    // Wire già inizializzato da main

    // Verifica presenza TCA9555 su I2C
    Wire.beginTransmission(TCA9555_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.printf("ERRORE: TCA9555 non trovato su I2C addr 0x%02X\n", TCA9555_ADDR);
        return false;
    }
    Serial.printf("TCA9555 trovato a 0x%02X\n", TCA9555_ADDR);

    // Configura Port0 pin 6 come output (I2C/USB selector) e setta HIGH per I2C
    uint8_t config0 = readRegister(TCA9555_CONFIG_PORT0);
    config0 &= ~(1 << I2C_USB_SEL_PIN);  // Clear bit per set output
    writeRegister(TCA9555_CONFIG_PORT0, config0);

    // Abilita I2C mode su GPIO19/20 (EXIO7 = HIGH)
    uint8_t output0 = readRegister(TCA9555_OUTPUT_PORT0);
    output0 |= (1 << I2C_USB_SEL_PIN);  // Set HIGH per I2C mode
    writeRegister(TCA9555_OUTPUT_PORT0, output0);
    Serial.println("  EXIO7 (I2C/USB sel) = HIGH (I2C mode)");

    // Configura Port1:
    // - P1.0 (Extend_IO9) = OUTPUT per PA_ENABLE
    // - P1.1 (Extend_IO10) = INPUT per test
    uint8_t config1 = readRegister(TCA9555_CONFIG_PORT1);
    config1 &= ~(1 << PA_ENABLE_PIN);  // P1.0 = output
    config1 |= (1 << 1);               // P1.1 = input (bit=1)
    writeRegister(TCA9555_CONFIG_PORT1, config1);
    Serial.println("  Extend_IO10 (P1.1) configurato come INPUT per test");

    // Inizialmente disabilitato
    disable();

    Serial.printf("TCA9555: addr=0x%02X, PA pin=P1.%d\n", TCA9555_ADDR, PA_ENABLE_PIN);
    return true;
}

void PowerAmplifier::enable() {
    if (!_enabled) {
        // EXIO8 = pin 8 del TCA9555 = P10 (Port1, bit 0)
        // Attiva PA (HIGH) su Port1, bit 0
        uint8_t output = readRegister(TCA9555_OUTPUT_PORT1);
        output |= (1 << PA_ENABLE_PIN);  // Set bit 0 HIGH
        writeRegister(TCA9555_OUTPUT_PORT1, output);

        delay(50);  // Delay per stabilizzazione PA
        _enabled = true;
        Serial.println("Power Amplifier ENABLED (EXIO8/P10)");
    }
}

void PowerAmplifier::disable() {
    // Disattiva PA (LOW) su Port1
    uint8_t output = readRegister(TCA9555_OUTPUT_PORT1);
    output &= ~(1 << PA_ENABLE_PIN);  // Clear bit LOW
    writeRegister(TCA9555_OUTPUT_PORT1, output);

    if (_enabled) {
        delay(50);
        _enabled = false;
        Serial.println("Power Amplifier DISABLED");
    }
}

bool PowerAmplifier::isEnabled() {
    return _enabled;
}

// Low-level I2C helpers (usa Wire Arduino)
bool PowerAmplifier::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(TCA9555_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return (Wire.endTransmission() == 0);
}

uint8_t PowerAmplifier::readRegister(uint8_t reg) {
    Wire.beginTransmission(TCA9555_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom((uint8_t)TCA9555_ADDR, (uint8_t)1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0x00;
}

bool PowerAmplifier::readTestInput() {
    // Leggi Port1 Input Register
    uint8_t port1 = readRegister(TCA9555_INPUT_PORT1);
    // Bit 1 = P1.1 (Extend_IO10)
    return (port1 & (1 << 1)) != 0;
}

void PowerAmplifier::testAllPort1Pins() {
    Serial.println("\n=== TEST PA: Provo Port0 E Port1 ===");
    Serial.println("Demo sketch usa EXIO8 che potrebbe essere P07!");

    // TEST Port0 (P00-P07)
    writeRegister(TCA9555_CONFIG_PORT0, 0x00);  // Tutti output
    for (int pin = 0; pin < 8; pin++) {
        Serial.printf("\nTest P0.%d HIGH (5 sec, premi paddle)...", pin);
        Serial.flush();
        writeRegister(TCA9555_OUTPUT_PORT0, (1 << pin));
        delay(5000);
        writeRegister(TCA9555_OUTPUT_PORT0, 0x00);
    }

    // TEST Port1 (P10-P17)
    writeRegister(TCA9555_CONFIG_PORT1, 0x00);  // Tutti output
    for (int pin = 0; pin < 8; pin++) {
        Serial.printf("\nTest P1.%d HIGH (5 sec, premi paddle)...", pin);
        Serial.flush();
        writeRegister(TCA9555_OUTPUT_PORT1, (1 << pin));
        delay(5000);
        writeRegister(TCA9555_OUTPUT_PORT1, 0x00);
    }

    Serial.println("\n=== Test completato ===");
    Serial.println("Hai sentito audio? Nota quale PX.Y!");
    Serial.flush();
}
