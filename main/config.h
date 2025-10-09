#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE_DEFAULT   16000U
#define AUDIO_TONE_FREQUENCY_DEFAULT 600U
#define AUDIO_FADE_IN_MS_DEFAULT    2U
#define AUDIO_FADE_OUT_MS_DEFAULT   4U
#define AUDIO_BUFFER_FRAMES_DEFAULT 256U
#define AUDIO_TONE_AMPLITUDE_DEFAULT 12000

typedef struct {
    uint32_t sample_rate_hz;
    uint16_t tone_frequency_hz;
    uint16_t fade_in_ms;
    uint16_t fade_out_ms;
    uint16_t buffer_frames;
    int16_t tone_amplitude;
} audio_settings_t;

void config_init(void);
void config_audio_set_defaults(audio_settings_t *cfg);
esp_err_t config_audio_get(audio_settings_t *out);
esp_err_t config_audio_update(const audio_settings_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
