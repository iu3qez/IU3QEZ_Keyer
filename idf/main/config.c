#include "config.h"

#include "esp_log.h"

static const char *TAG = "config";

static audio_settings_t s_audio_settings;

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
    cfg->tone_amplitude = AUDIO_TONE_AMPLITUDE_DEFAULT;
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
    return ESP_OK;
}
