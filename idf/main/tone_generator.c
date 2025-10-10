#include "tone_generator.h"

#include <math.h>
#include <limits.h>

#include "esp_check.h"
#include "esp_log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "tone_gen"

static size_t ms_to_samples(uint32_t sample_rate, uint16_t duration_ms) {
    uint64_t samples = ((uint64_t)sample_rate * duration_ms) / 1000U;
    if (samples == 0) {
        samples = 1;  // Guarantee at least one sample to avoid division by zero
    }
    return (size_t)samples;
}

static void update_fade_samples(tone_generator_t *gen) {
    gen->fade_in_samples = ms_to_samples(gen->settings.sample_rate_hz, gen->settings.fade_in_ms);
    gen->fade_out_samples = ms_to_samples(gen->settings.sample_rate_hz, gen->settings.fade_out_ms);
}

void tone_generator_init(tone_generator_t *gen, const audio_settings_t *settings) {
    if (!gen || !settings) {
        ESP_LOGE(TAG, "invalid init arguments");
        return;
    }

    gen->settings = *settings;
    tone_generator_set_volume(gen, gen->settings.volume_percent);
    if (gen->settings.sample_rate_hz == 0) {
        ESP_LOGW(TAG, "sample rate is zero, forcing 1 Hz to avoid division by zero");
        gen->settings.sample_rate_hz = 1;
    }
    gen->state = TONE_STATE_IDLE;
    gen->pending_stop = false;
    gen->fade_position = 0;
    update_fade_samples(gen);
    gen->phase = 0.0f;
    gen->phase_step = (float)(2.0 * M_PI * (double)gen->settings.tone_frequency_hz / (double)gen->settings.sample_rate_hz);
}

void tone_generator_start(tone_generator_t *gen) {
    if (!gen) {
        ESP_LOGE(TAG, "tone generator null on start");
        return;
    }

    gen->pending_stop = false;
    gen->fade_position = 0;
    gen->state = (gen->settings.fade_in_ms > 0) ? TONE_STATE_FADE_IN : TONE_STATE_PLAYING;
}

void tone_generator_stop(tone_generator_t *gen) {
    if (!gen) {
        ESP_LOGE(TAG, "tone generator null on stop");
        return;
    }
    if (gen->state == TONE_STATE_IDLE || gen->state == TONE_STATE_FADE_OUT) {
        return;
    }
    gen->pending_stop = true;
}

bool tone_generator_is_active(const tone_generator_t *gen) {
    if (!gen) {
        return false;
    }
    return gen->state != TONE_STATE_IDLE;
}

static float envelope_gain(tone_generator_t *gen) {
    switch (gen->state) {
        case TONE_STATE_FADE_IN: {
            float gain = (float)gen->fade_position / (float)gen->fade_in_samples;
            if (gain >= 1.0f) {
                gen->state = TONE_STATE_PLAYING;
                gen->fade_position = 0;
                return 1.0f;
            }
            return gain;
        }
        case TONE_STATE_PLAYING:
            if (gen->pending_stop) {
                gen->state = TONE_STATE_FADE_OUT;
                gen->fade_position = 0;
                return envelope_gain(gen);
            }
            return 1.0f;
        case TONE_STATE_FADE_OUT: {
            float gain = 1.0f - ((float)gen->fade_position / (float)gen->fade_out_samples);
            if (gain <= 0.0f) {
                gen->state = TONE_STATE_IDLE;
                gen->fade_position = 0;
                gen->pending_stop = false;
                return 0.0f;
            }
            return gain;
        }
        case TONE_STATE_IDLE:
        default:
            return 0.0f;
    }
}

void tone_generator_fill(tone_generator_t *gen, int16_t *stereo_buffer, size_t frames) {
    if (!gen || !stereo_buffer) {
        ESP_LOGE(TAG, "fill arguments invalid");
        return;
    }

    for (size_t i = 0; i < frames; ++i) {
        float gain = envelope_gain(gen);
        float sample = sinf(gen->phase) * gain * (float)gen->settings.tone_amplitude;
        gen->phase += gen->phase_step;
        if (gen->phase >= (float)(2.0 * M_PI)) {
            gen->phase -= (float)(2.0 * M_PI);
        }
        if (sample > (float)INT16_MAX) {
            sample = (float)INT16_MAX;
        } else if (sample < (float)INT16_MIN) {
            sample = (float)INT16_MIN;
        }
        int16_t value = (int16_t)lroundf(sample);
        stereo_buffer[i * 2] = value;
        stereo_buffer[i * 2 + 1] = value;

        if (gen->state == TONE_STATE_FADE_IN) {
            if (gen->fade_position < gen->fade_in_samples) {
                gen->fade_position++;
            }
        } else if (gen->state == TONE_STATE_FADE_OUT) {
            if (gen->fade_position < gen->fade_out_samples) {
                gen->fade_position++;
            }
        }
    }
}

void tone_generator_set_volume(tone_generator_t *gen, uint8_t volume_percent) {
    if (!gen) {
        ESP_LOGE(TAG, "tone generator null on set_volume");
        return;
    }
    gen->settings.volume_percent = volume_percent;
    gen->settings.tone_amplitude = config_volume_percent_to_amplitude(volume_percent);
    ESP_LOGI(TAG, "Tone generator volume set to %u%% (amplitude=%d)", volume_percent, gen->settings.tone_amplitude);
}

uint8_t tone_generator_get_volume(const tone_generator_t *gen) {
    if (!gen) {
        return 0;
    }
    return gen->settings.volume_percent;
}

void tone_generator_set_frequency(tone_generator_t *gen, uint16_t frequency_hz) {
    if (!gen) {
        ESP_LOGE(TAG, "tone generator null on set_frequency");
        return;
    }
    if (frequency_hz == 0) {
        ESP_LOGW(TAG, "frequency 0 requested, keeping previous value %u", gen->settings.tone_frequency_hz);
        return;
    }
    gen->settings.tone_frequency_hz = frequency_hz;
    gen->phase_step = (float)(2.0 * M_PI * (double)gen->settings.tone_frequency_hz / (double)gen->settings.sample_rate_hz);
    ESP_LOGI(TAG, "Tone generator frequency set to %u Hz", frequency_hz);
}

uint16_t tone_generator_get_frequency(const tone_generator_t *gen) {
    if (!gen) {
        return 0;
    }
    return gen->settings.tone_frequency_hz;
}

void tone_generator_set_fade(tone_generator_t *gen, uint16_t fade_in_ms, uint16_t fade_out_ms) {
    if (!gen) {
        ESP_LOGE(TAG, "tone generator null on set_fade");
        return;
    }
    gen->settings.fade_in_ms = fade_in_ms;
    gen->settings.fade_out_ms = fade_out_ms;
    update_fade_samples(gen);
    ESP_LOGI(TAG, "Tone generator fade set to %u/%u ms", fade_in_ms, fade_out_ms);
}

uint16_t tone_generator_get_fade_in_ms(const tone_generator_t *gen) {
    if (!gen) {
        return 0;
    }
    return gen->settings.fade_in_ms;
}

uint16_t tone_generator_get_fade_out_ms(const tone_generator_t *gen) {
    if (!gen) {
        return 0;
    }
    return gen->settings.fade_out_ms;
}
