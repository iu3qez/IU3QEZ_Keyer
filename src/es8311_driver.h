#ifndef ES8311_DRIVER_H
#define ES8311_DRIVER_H

#include <Arduino.h>
#include <Wire.h>

// ES8311 I2C Address
#define ES8311_ADDR 0x18

// ES8311 Register Map (principali)
#define ES8311_REG00_RESET          0x00
#define ES8311_REG01_CLK_MANAGER    0x01
#define ES8311_REG02_CLK_MANAGER    0x02
#define ES8311_REG03_CLK_MANAGER    0x03
#define ES8311_REG04_CLK_MANAGER    0x04
#define ES8311_REG05_SYSTEM         0x05
#define ES8311_REG06_SYSTEM         0x06
#define ES8311_REG07_SYSTEM         0x07
#define ES8311_REG08_SYSTEM         0x08
#define ES8311_REG09_SDPIN          0x09
#define ES8311_REG0A_SDPOUT         0x0A
#define ES8311_REG0B_SYSTEM         0x0B
#define ES8311_REG0C_SYSTEM         0x0C
#define ES8311_REG0D_SYSTEM         0x0D
#define ES8311_REG0E_SYSTEM         0x0E
#define ES8311_REG0F_SYSTEM         0x0F
#define ES8311_REG10_SYSTEM         0x10
#define ES8311_REG11_SYSTEM         0x11
#define ES8311_REG12_SYSTEM         0x12
#define ES8311_REG13_SYSTEM         0x13
#define ES8311_REG14_SYSTEM         0x14
#define ES8311_REG15_ADC            0x15
#define ES8311_REG16_ADC            0x16
#define ES8311_REG17_ADC            0x17
#define ES8311_REG18_ADC            0x18
#define ES8311_REG19_ADC            0x19
#define ES8311_REG1A_ADC            0x1A
#define ES8311_REG1B_ADC            0x1B
#define ES8311_REG1C_ADC            0x1C
#define ES8311_REG31_DAC            0x31
#define ES8311_REG32_DAC            0x32
#define ES8311_REG33_DAC            0x33
#define ES8311_REG34_DAC            0x34
#define ES8311_REG35_DAC            0x35
#define ES8311_REG36_DAC            0x36
#define ES8311_REG37_DAC            0x37
#define ES8311_REG44_GPIO           0x44
#define ES8311_REG45_GPIO           0x45

// Sample rates
enum ES8311_SampleRate {
    ES8311_SR_8000  = 8000,
    ES8311_SR_11025 = 11025,
    ES8311_SR_16000 = 16000,
    ES8311_SR_22050 = 22050,
    ES8311_SR_32000 = 32000,
    ES8311_SR_44100 = 44100,
    ES8311_SR_48000 = 48000
};

// Bit depths
enum ES8311_BitDepth {
    ES8311_BIT_16 = 16,
    ES8311_BIT_18 = 18,
    ES8311_BIT_20 = 20,
    ES8311_BIT_24 = 24,
    ES8311_BIT_32 = 32
};

class ES8311_Driver {
public:
    ES8311_Driver(uint8_t i2c_addr = ES8311_ADDR);

    // Inizializzazione
    bool begin(int sda_pin, int scl_pin, uint32_t freq = 100000);
    bool reset();

    // Configurazione DAC per sidetone
    bool configureDACMode(ES8311_SampleRate sample_rate, ES8311_BitDepth bit_depth);
    bool setDACVolume(uint8_t volume); // 0-100%
    bool enableDAC(bool enable);
    bool enablePowerAmp(bool enable);

    // Configurazione MCLK
    bool setMCLK(uint32_t mclk_freq, ES8311_SampleRate sample_rate);

    // Low-level I2C
    bool writeReg(uint8_t reg, uint8_t value);
    uint8_t readReg(uint8_t reg);

private:
    uint8_t _i2c_addr;
    TwoWire* _wire;

    // Helper functions
    uint8_t calculateMCLKDiv(uint32_t mclk_freq, ES8311_SampleRate sample_rate);
};

#endif // ES8311_DRIVER_H
