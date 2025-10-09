#include "config.h"

#include <limits.h>
#include "esp_log.h"

static const char *TAG = "config";

static audio_settings_t s_audio_settings;

int16_t config_volume_percent_to_amplitude(uint8_t percent) {
    if (percent >= 100) {
        return INT16_MAX;
    }
    return (int16_t)((percent * (int32_t)INT16_MAX) / 100);
}

void config_audio_set_defaults(audio_settings_t *cfg) {
    if (!cfg) {
        ESP_LOGE(TAG, "audio cfg null");
        return;
    }
    cfg->sample_rate_hz = AUDIO_SAMPLE_RATE_DEFAULT;
    cfg->tone_frequency_hz = AUDIO_TONE_FREQUENCY_DEFAULT;
    cfg->fade_in_ms = AUDIO_FADE_IN_MS_DEFAULT;
    cfg->fade_out_ms = AUDIO_FADE_OUT_MS_DEFAULT;
    cfg->buffer_frames = AUDIO_BUFFER_FRAMES_DEFAULT;
    cfg->volume_percent = AUDIO_TONE_VOLUME_PERCENT_DEFAULT;
    cfg->tone_amplitude = config_volume_percent_to_amplitude(cfg->volume_percent);
}

void config_init(void) {
    config_audio_set_defaults(&s_audio_settings);
    // TODO: Persist audio settings to NVS or external storage once configuration UI is available.
}

esp_err_t config_audio_get(audio_settings_t *out) {
    if (!out) {
        ESP_LOGE(TAG, "audio cfg output null");
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_audio_settings;
    return ESP_OK;
}

esp_err_t config_audio_update(const audio_settings_t *cfg) {
    if (!cfg) {
        ESP_LOGE(TAG, "audio cfg input null");
        return ESP_ERR_INVALID_ARG;
    }
    s_audio_settings = *cfg;
    s_audio_settings.tone_amplitude = config_volume_percent_to_amplitude(s_audio_settings.volume_percent);
    return ESP_OK;
}
