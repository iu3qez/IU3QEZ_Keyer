#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "esp_err.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE_DEFAULT    I2S_SAMPLE_RATE
#define AUDIO_TONE_FREQUENCY_DEFAULT SIDETONE_FREQ_HZ
#define AUDIO_FADE_IN_MS_DEFAULT     SIDETONE_RAMP_UP_MS
#define AUDIO_FADE_OUT_MS_DEFAULT    SIDETONE_RAMP_DOWN_MS
#define AUDIO_BUFFER_FRAMES_DEFAULT  AUDIO_BUFFER_FRAMES_DEFAULT_INTERNAL
#define AUDIO_TONE_VOLUME_PERCENT_DEFAULT SIDETONE_VOLUME

#ifndef AUDIO_BUFFER_FRAMES_DEFAULT_INTERNAL
#define AUDIO_BUFFER_FRAMES_DEFAULT_INTERNAL 256U
#endif

typedef struct {
    uint32_t sample_rate_hz;
    uint16_t tone_frequency_hz;
    uint16_t fade_in_ms;
    uint16_t fade_out_ms;
    uint16_t buffer_frames;
    int16_t tone_amplitude;
    uint8_t volume_percent;
} audio_settings_t;

void config_init(void);
void config_audio_set_defaults(audio_settings_t *cfg);
esp_err_t config_audio_get(audio_settings_t *out);
esp_err_t config_audio_update(const audio_settings_t *cfg);
int16_t config_volume_percent_to_amplitude(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
