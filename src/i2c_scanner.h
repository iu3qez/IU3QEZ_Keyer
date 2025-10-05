#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#include <Arduino.h>
#include <Wire.h>

// Scan I2C bus per trovare dispositivi
void scanI2CBus(TwoWire &wire, const char* bus_name) {
    Serial.printf("\n=== I2C Scanner: %s ===\n", bus_name);

    uint8_t devices_found = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        wire.beginTransmission(addr);
        uint8_t error = wire.endTransmission();

        if (error == 0) {
            Serial.printf("  Device found at 0x%02X\n", addr);
            devices_found++;
        }
    }

    if (devices_found == 0) {
        Serial.println("  No I2C devices found!");
    } else {
        Serial.printf("  Total: %d device(s) found\n", devices_found);
    }
    Serial.println();
}

#endif // I2C_SCANNER_H
