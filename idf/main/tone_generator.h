#ifndef TONE_GENERATOR_H
#define TONE_GENERATOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TONE_STATE_IDLE = 0,
    TONE_STATE_FADE_IN,
    TONE_STATE_PLAYING,
    TONE_STATE_FADE_OUT,
} tone_state_t;

typedef struct tone_generator {
    audio_settings_t settings;
    tone_state_t state;
    bool pending_stop;
    size_t fade_position;
    size_t fade_in_samples;
    size_t fade_out_samples;
    float phase;
    float phase_step;
} tone_generator_t;

void tone_generator_init(tone_generator_t *gen, const audio_settings_t *settings);
void tone_generator_start(tone_generator_t *gen);
void tone_generator_stop(tone_generator_t *gen);
bool tone_generator_is_active(const tone_generator_t *gen);
void tone_generator_fill(tone_generator_t *gen, int16_t *stereo_buffer, size_t frames);
void tone_generator_set_volume(tone_generator_t *gen, uint8_t volume_percent);
uint8_t tone_generator_get_volume(const tone_generator_t *gen);
void tone_generator_set_frequency(tone_generator_t *gen, uint16_t frequency_hz);
uint16_t tone_generator_get_frequency(const tone_generator_t *gen);
void tone_generator_set_fade(tone_generator_t *gen, uint16_t fade_in_ms, uint16_t fade_out_ms);
uint16_t tone_generator_get_fade_in_ms(const tone_generator_t *gen);
uint16_t tone_generator_get_fade_out_ms(const tone_generator_t *gen);

#ifdef __cplusplus
}
#endif

#endif /* TONE_GENERATOR_H */
