#include "sidetone_generator.h"
#include "es8311.h"  // Libreria demo
#include <math.h>

// Istanza ES8311 driver (libreria demo)
static es8311_handle_t es8311_handle = NULL;

SidetoneGenerator::SidetoneGenerator()
    : _frequency(SIDETONE_FREQ_HZ), _volume(SIDETONE_VOLUME), _playing(false),
      _phase_accumulator(0), _phase_increment(0),
      _envelope_state(ENV_IDLE), _envelope_sample_count(0),
      _audioTaskHandle(NULL), _mutex(NULL) {
}

bool SidetoneGenerator::begin() {
    Serial.println("Inizializzazione Sidetone Generator...");

    // Crea mutex per sincronizzazione tra core
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == NULL) {
        Serial.println("ERRORE: Creazione mutex fallita");
        return false;
    }

    // Genera wavetable sinusoidale
    generateWavetable();

    // Calcola phase increment per frequenza iniziale
    calculatePhaseIncrement();

    // Inizializza ES8311 codec
    if (!initES8311()) {
        Serial.println("ERRORE: Inizializzazione ES8311 fallita");
        return false;
    }

    // Inizializza I2S con pin corretti
    if (!initI2S()) {
        Serial.println("ERRORE: Inizializzazione I2S fallita");
        return false;
    }
    Serial.println("I2S inizializzato correttamente");

    Serial.printf("Sidetone: freq=%d Hz, volume=%d%%\n", _frequency, _volume);
    return true;
}

bool SidetoneGenerator::initES8311() {
    Serial.println("Inizializzazione ES8311 codec (libreria demo)...");
    Serial.printf("  I2C pins: SDA=%d, SCL=%d, addr=0x%02X\n",
                  I2C_SDA_PIN, I2C_SCL_PIN, ES8311_I2C_ADDR);

    // Wire già inizializzato da main.cpp
    // Libreria ES8311 usa Wire internamente

    // Crea handle ES8311 (i2c_port_t ignorato, usa Wire)
    es8311_handle = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
    if (es8311_handle == NULL) {
        Serial.println("ERRORE: es8311_create fallito");
        return false;
    }
    Serial.println("  ES8311 handle creato");

    // Configura clock ES8311
    es8311_clock_config_t clk_cfg = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = I2S_SAMPLE_RATE * 256,  // 16000 * 256 = 4096000 Hz
        .sample_frequency = I2S_SAMPLE_RATE        // 16000 Hz
    };

    // Inizializza ES8311 (16-bit input e output)
    esp_err_t err = es8311_init(es8311_handle, &clk_cfg, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (err != ESP_OK) {
        Serial.printf("ERRORE es8311_init: %d\n", err);
        return false;
    }
    Serial.println("  ES8311 init OK");

    // Imposta volume
    int volume_set = 0;
    err = es8311_voice_volume_set(es8311_handle, _volume, &volume_set);
    if (err != ESP_OK) {
        Serial.printf("ERRORE es8311_voice_volume_set: %d\n", err);
        return false;
    }
    Serial.printf("  ES8311 volume: %d%%\n", volume_set);

    // Unmute
    err = es8311_voice_mute(es8311_handle, false);
    if (err != ESP_OK) {
        Serial.printf("ERRORE es8311_voice_mute: %d\n", err);
        return false;
    }
    Serial.println("  ES8311 unmuted");

    // Disabilita microfono (non serve per DAC-only)
    err = es8311_microphone_config(es8311_handle, false);
    if (err != ESP_OK) {
        Serial.printf("ERRORE es8311_microphone_config: %d\n", err);
        return false;
    }
    Serial.println("  ES8311 microphone disabled");

    Serial.println("ES8311 inizializzato correttamente");
    return true;
}

void SidetoneGenerator::generateWavetable() {
    // Genera onda sinusoidale precalcolata
    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        float angle = (2.0f * PI * i) / WAVETABLE_SIZE;
        _wavetable[i] = (int16_t)(sin(angle) * TONE_AMPLITUDE);
    }
}

void SidetoneGenerator::calculatePhaseIncrement() {
    // Phase increment = (frequency * 2^32) / sample_rate
    // Il phase accumulator a 32-bit rappresenta UN ciclo completo (0-2^32)
    // La wavetable viene letta con i top 6 bit (shift 26)
    // Usando aritmetica a 64-bit per evitare overflow
    uint64_t increment = ((uint64_t)_frequency * 0x100000000ULL) / I2S_SAMPLE_RATE;
    _phase_increment = (uint32_t)increment;
}

bool SidetoneGenerator::initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 128,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = I2S_SAMPLE_RATE * 256
    };

    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_MCLK_PIN,
        .bck_io_num = I2S_BCLK_PIN,
        .ws_io_num = I2S_LRCK_PIN,
        .data_out_num = I2S_DOUT_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    // Installa driver I2S
    esp_err_t err = i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("ERRORE i2s_driver_install: %d\n", err);
        return false;
    }

    // Configura pin I2S
    err = i2s_set_pin(I2S_NUM, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("ERRORE i2s_set_pin: %d\n", err);
        return false;
    }

    // Start I2S
    err = i2s_start(I2S_NUM);
    if (err != ESP_OK) {
        Serial.printf("ERRORE i2s_start: %d\n", err);
        return false;
    }

    Serial.println("I2S inizializzato correttamente");
    Serial.printf("  Sample rate: %d Hz\n", I2S_SAMPLE_RATE);
    Serial.printf("  MCLK: GPIO%d, BCLK: GPIO%d, LRCK: GPIO%d, DOUT: GPIO%d\n",
                  I2S_MCLK_PIN, I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN);

    return true;
}

void SidetoneGenerator::setFrequency(uint16_t freq_hz) {
    // Limita range 300-800 Hz
    if (freq_hz < 300) freq_hz = 300;
    if (freq_hz > 800) freq_hz = 800;

    _frequency = freq_hz;
    calculatePhaseIncrement();

    Serial.printf("Sidetone freq: %d Hz\n", _frequency);
}

void SidetoneGenerator::setVolume(uint8_t volume) {
    if (volume > 100) volume = 100;
    _volume = volume;
    Serial.printf("Sidetone volume: %d%%\n", _volume);
}

void SidetoneGenerator::start() {
    // Proteggi accesso con mutex (chiamato da ISR via callback - NO Serial.print!)
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        if (!_playing) {
            _playing = true;
            _phase_accumulator = 0;  // Reset phase
            _envelope_state = ENV_RAMP_UP;
            _envelope_sample_count = 0;
            // RIMOSSO: Serial.println() non sicuro in contesto ISR
        }
        xSemaphoreGive(_mutex);
    }
}

void SidetoneGenerator::stop() {
    // Proteggi accesso con mutex (chiamato da ISR via callback - NO Serial.print!)
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        if (_playing) {
            // Non fermare subito, inizia ramp down
            if (_envelope_state != ENV_RAMP_DOWN) {
                _envelope_state = ENV_RAMP_DOWN;
                _envelope_sample_count = 0;
                // RIMOSSO: Serial.println() non sicuro in contesto ISR
            }
        }
        xSemaphoreGive(_mutex);
    }
}

bool SidetoneGenerator::isPlaying() {
    return _playing;
}

float SidetoneGenerator::getEnvelopeGain() {
    float gain = 0.0f;

    switch (_envelope_state) {
        case ENV_IDLE:
            gain = 0.0f;
            break;

        case ENV_RAMP_UP:
            // Linear ramp 0 -> 1.0
            gain = (float)_envelope_sample_count / RAMP_UP_SAMPLES;
            if (gain > 1.0f) gain = 1.0f;
            break;

        case ENV_SUSTAIN:
            gain = 1.0f;
            break;

        case ENV_RAMP_DOWN:
            // Linear ramp 1.0 -> 0
            gain = 1.0f - ((float)_envelope_sample_count / RAMP_DOWN_SAMPLES);
            if (gain < 0.0f) gain = 0.0f;
            break;
    }

    return gain;
}

void SidetoneGenerator::writeSamples(size_t num_samples) {
    // Buffer stereo (L+R)
    int16_t samples[128 * 2];  // Max 128 stereo frames
    static uint32_t debugCount = 0;

    if (num_samples > 128) num_samples = 128;

    for (size_t i = 0; i < num_samples; i++) {
        int16_t sample;

        if (_playing || _envelope_state != ENV_IDLE) {
            // Leggi dalla wavetable usando phase accumulator
            // Shift = 32 - log2(WAVETABLE_SIZE) = 32 - 6 = 26 bit
            uint32_t table_index = (_phase_accumulator >> 26) & (WAVETABLE_SIZE - 1);
            sample = _wavetable[table_index];

            // Applica volume
            sample = (sample * _volume) / 100;

            // Applica envelope per rampe anti-click
            float envelope_gain = getEnvelopeGain();
            sample = (int16_t)(sample * envelope_gain);

            // Avanza phase accumulator
            _phase_accumulator += _phase_increment;

            // Aggiorna envelope state machine
            _envelope_sample_count++;

            if (_envelope_state == ENV_RAMP_UP && _envelope_sample_count >= RAMP_UP_SAMPLES) {
                _envelope_state = ENV_SUSTAIN;
                _envelope_sample_count = 0;
            } else if (_envelope_state == ENV_RAMP_DOWN && _envelope_sample_count >= RAMP_DOWN_SAMPLES) {
                _envelope_state = ENV_IDLE;
                _envelope_sample_count = 0;
                _playing = false;  // Ora effettivamente fermo
                // RIMOSSO: Serial.print da task audio per evitare race con loop
            }

            // RIMOSSO: Debug Serial da task audio (race condition con loop)
        } else {
            sample = 0;  // Silenzio
        }

        // Stereo: stesso sample su L+R
        samples[i * 2] = sample;      // Left
        samples[i * 2 + 1] = sample;  // Right
    }

    // Scrivi su I2S
    size_t bytes_written;
    i2s_write(I2S_NUM, samples, num_samples * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
}

bool SidetoneGenerator::startAudioTask() {
    // Crea task su Core 1 (Application Core)
    BaseType_t result = xTaskCreatePinnedToCore(
        audioTaskWrapper,      // Funzione task
        "AudioTask",           // Nome task
        4096,                  // Stack size
        this,                  // Parametro (puntatore a this)
        1,                     // Priorità (1 = normale)
        &_audioTaskHandle,     // Handle task
        1                      // Core 1 (Application Core)
    );

    if (result != pdPASS) {
        Serial.println("ERRORE: Creazione audio task fallita");
        return false;
    }

    Serial.println("Audio task avviato su Core 1");
    return true;
}

void SidetoneGenerator::audioTaskWrapper(void* parameter) {
    // Wrapper statico che chiama il metodo di istanza
    SidetoneGenerator* instance = static_cast<SidetoneGenerator*>(parameter);

    while (true) {
        instance->task();
        // Yielding implicito durante i2s_write blocking
    }
}

void SidetoneGenerator::task() {
    // Task eseguito continuamente su Core 1
    // Genera e invia 64 samples per chiamata
    // Mutex NON necessario per lettura di _playing (volatile)
    writeSamples(64);
}
