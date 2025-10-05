#ifndef SIDETONE_GENERATOR_H
#define SIDETONE_GENERATOR_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <driver/i2c.h>
#include "settings.h"

// Configurazione I2S
#define I2S_NUM             I2S_NUM_0
#define I2S_BITS_PER_SAMPLE I2S_BITS_PER_SAMPLE_16BIT

// Envelope states per rampe anti-click
enum EnvelopeState {
    ENV_IDLE,       // Nessun tono
    ENV_RAMP_UP,    // Ramp up (0 -> 1.0)
    ENV_SUSTAIN,    // Tono costante (1.0)
    ENV_RAMP_DOWN   // Ramp down (1.0 -> 0)
};

class SidetoneGenerator {
public:
    SidetoneGenerator();

    // Inizializzazione (ritorna true se successo)
    bool begin();

    // Inizializza ES8311 codec
    bool initES8311();

    // Configurazione tono
    void setFrequency(uint16_t freq_hz);  // 300-800 Hz
    uint16_t getFrequency() { return _frequency; }

    void setVolume(uint8_t volume);       // 0-100%
    uint8_t getVolume() { return _volume; }

    // Controllo tono
    void start();
    void stop();
    bool isPlaying();

    // Task I2S (Core 1) - chiamato automaticamente dal task FreeRTOS
    void task();

    // Avvia task audio su Core 1
    bool startAudioTask();

private:
    uint16_t _frequency;
    uint8_t _volume;
    volatile bool _playing;  // volatile perché acceduto da 2 core

    // Wavetable
    int16_t _wavetable[WAVETABLE_SIZE];
    uint32_t _phase_accumulator;
    uint32_t _phase_increment;

    // Envelope per rampe anti-click
    volatile EnvelopeState _envelope_state;  // volatile perché modificato da start/stop
    uint32_t _envelope_sample_count;  // Contatore samples nella fase corrente

    // FreeRTOS task
    TaskHandle_t _audioTaskHandle;
    SemaphoreHandle_t _mutex;  // Mutex per proteggere accesso a _playing/_envelope_state

    // I2S
    bool initI2S();
    void generateWavetable();
    void calculatePhaseIncrement();
    void writeSamples(size_t num_samples);

    // Envelope
    float getEnvelopeGain();  // Calcola gain corrente (0.0 - 1.0)

    // Task FreeRTOS statico (wrapper per chiamare task())
    static void audioTaskWrapper(void* parameter);
};

#endif // SIDETONE_GENERATOR_H
