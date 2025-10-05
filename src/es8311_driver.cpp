#include "es8311_driver.h"

ES8311_Driver::ES8311_Driver(uint8_t i2c_addr) : _i2c_addr(i2c_addr), _wire(&Wire) {}

bool ES8311_Driver::begin(int sda_pin, int scl_pin, uint32_t freq) {
    _wire->begin(sda_pin, scl_pin, freq);
    delay(10);

    // Verifica presenza codec
    _wire->beginTransmission(_i2c_addr);
    if (_wire->endTransmission() != 0) {
        return false;
    }

    return reset();
}

bool ES8311_Driver::reset() {
    // Software reset
    if (!writeReg(ES8311_REG00_RESET, 0x1F)) return false;
    delay(20);
    if (!writeReg(ES8311_REG00_RESET, 0x00)) return false;
    delay(20);

    return true;
}

bool ES8311_Driver::configureDACMode(ES8311_SampleRate sample_rate, ES8311_BitDepth bit_depth) {
    // Configurazione base DAC mode

    // Clock manager: set MCLK source e divisori
    // Assumiamo MCLK = 16 MHz (da GPIO16)
    if (!setMCLK(16000000, sample_rate)) return false;

    // System settings
    if (!writeReg(ES8311_REG0D_SYSTEM, 0x01)) return false; // Normal bias
    if (!writeReg(ES8311_REG0E_SYSTEM, 0x02)) return false; // VMIDSEL

    // SDP Input (I2S format, slave mode)
    uint8_t sdp_format = 0x00; // I2S format
    switch (bit_depth) {
        case ES8311_BIT_16: sdp_format |= 0x0C; break; // 16-bit
        case ES8311_BIT_18: sdp_format |= 0x04; break; // 18-bit
        case ES8311_BIT_20: sdp_format |= 0x08; break; // 20-bit
        case ES8311_BIT_24: sdp_format |= 0x00; break; // 24-bit
        case ES8311_BIT_32: sdp_format |= 0x10; break; // 32-bit
    }
    if (!writeReg(ES8311_REG09_SDPIN, sdp_format)) return false;

    // DAC settings
    if (!writeReg(ES8311_REG31_DAC, 0x00)) return false; // DAC normal
    if (!writeReg(ES8311_REG32_DAC, 0xBF)) return false; // DAC enable, unmute
    if (!writeReg(ES8311_REG33_DAC, 0x00)) return false; // DAC volume

    // Analog output
    if (!writeReg(ES8311_REG37_DAC, 0x08)) return false; // Analog output enable

    return true;
}

bool ES8311_Driver::setMCLK(uint32_t mclk_freq, ES8311_SampleRate sample_rate) {
    uint8_t div = calculateMCLKDiv(mclk_freq, sample_rate);

    // CLK Manager registers
    if (!writeReg(ES8311_REG01_CLK_MANAGER, 0x30)) return false; // MCLK source
    if (!writeReg(ES8311_REG02_CLK_MANAGER, 0x00)) return false; // PreDiv = 1
    if (!writeReg(ES8311_REG03_CLK_MANAGER, div)) return false;   // MCLK divider
    if (!writeReg(ES8311_REG04_CLK_MANAGER, 0x20)) return false; // LRCK divider

    return true;
}

uint8_t ES8311_Driver::calculateMCLKDiv(uint32_t mclk_freq, ES8311_SampleRate sample_rate) {
    // Calcola divisore MCLK per ottenere sample rate desiderato
    // MCLK / (div * 2) = sample_rate * 256 (per oversampling tipico)

    uint32_t target_freq = sample_rate * 256;
    uint8_t div = mclk_freq / (target_freq * 2);

    // Clamp a valori validi (1-8)
    if (div < 1) div = 1;
    if (div > 8) div = 8;

    return div;
}

bool ES8311_Driver::setDACVolume(uint8_t volume) {
    // Volume: 0-100% -> 0x00 (max) to 0xFF (mute)
    // ES8311 DAC volume: 0x00 = 0dB, 0xC0 = -96dB (step 0.5dB)

    if (volume > 100) volume = 100;

    uint8_t reg_val;
    if (volume == 0) {
        reg_val = 0xC0; // Mute
    } else {
        // Map 1-100% a 0xC0-0x00 (inversamente proporzionale)
        reg_val = 0xC0 - ((volume * 0xC0) / 100);
    }

    return writeReg(ES8311_REG33_DAC, reg_val);
}

bool ES8311_Driver::enableDAC(bool enable) {
    uint8_t val = enable ? 0xBF : 0x00;
    return writeReg(ES8311_REG32_DAC, val);
}

bool ES8311_Driver::enablePowerAmp(bool enable) {
    // GPIO control per PA (se necessario)
    // Dipende dallo schema hardware - potrebbe essere controllato via GPIO separato
    return true; // Placeholder
}

bool ES8311_Driver::writeReg(uint8_t reg, uint8_t value) {
    _wire->beginTransmission(_i2c_addr);
    _wire->write(reg);
    _wire->write(value);
    return (_wire->endTransmission() == 0);
}

uint8_t ES8311_Driver::readReg(uint8_t reg) {
    _wire->beginTransmission(_i2c_addr);
    _wire->write(reg);
    _wire->endTransmission(false);

    _wire->requestFrom(_i2c_addr, (uint8_t)1);
    if (_wire->available()) {
        return _wire->read();
    }
    return 0xFF;
}
