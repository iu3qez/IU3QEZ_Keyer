#ifndef SIDETONE_GENERATOR_H
#define SIDETONE_GENERATOR_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <driver/i2c.h>

// Configurazione I2S per ESP32-S3-AUDIO-Board
#define I2S_NUM             I2S_NUM_0
#define I2S_SAMPLE_RATE     16000
#define I2S_BITS_PER_SAMPLE I2S_BITS_PER_SAMPLE_16BIT
#define I2S_CHANNELS        2

// Pin I2S per ESP32-S3-AUDIO-Board (CORRETTI da schematico!)
#define I2S_MCLK_PIN        12  // GPIO12 -> I2S_MCLK
#define I2S_BCLK_PIN        13  // GPIO13 -> I2S_SCLK (BCLK)
#define I2S_LRCK_PIN        14  // GPIO14 -> I2S_LRCK (WS)
#define I2S_DOUT_PIN        16  // GPIO16 -> I2S_DSDIN (DOUT to ES8311)
// GPIO15 = I2S_ASDOUT (input from ES8311, non usato per DAC)

// I2C pins per ES8311 codec (bus separato da TCA9555!)
#define ES8311_SDA_PIN      11  // GPIO11 -> SDA (corretto da schematico)
#define ES8311_SCL_PIN      10  // GPIO10 -> SCL (corretto da schematico)
#define ES8311_I2C_ADDR     0x18

// Wavetable
#define WAVETABLE_SIZE      64
#define TONE_AMPLITUDE      16384  // ~50% del massimo 16-bit signed

class SidetoneGenerator {
public:
    SidetoneGenerator();

    // Inizializzazione (ritorna true se successo)
    bool begin();

    // Inizializza ES8311 codec
    bool initES8311();

    // Configurazione tono
    void setFrequency(uint16_t freq_hz);  // 400-600 Hz
    void setVolume(uint8_t volume);       // 0-100%

    // Controllo tono
    void start();
    void stop();
    bool isPlaying();

    // Task I2S (Core 1)
    void task();

private:
    uint16_t _frequency;
    uint8_t _volume;
    bool _playing;

    // Wavetable
    int16_t _wavetable[WAVETABLE_SIZE];
    uint32_t _phase_accumulator;
    uint32_t _phase_increment;

    // I2S
    bool initI2S();
    void generateWavetable();
    void calculatePhaseIncrement();
    void writeSamples(size_t num_samples);
};

#endif // SIDETONE_GENERATOR_H
