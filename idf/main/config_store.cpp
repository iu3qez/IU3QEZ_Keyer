#include "config_store.h"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

#include "keyer_logic.h"
#include "morse_decoder.h"
#include "settings.h"
#include "tone_generator.h"
#include "config.h"

namespace {

constexpr const char *TAG = "config_store";
constexpr const char *kNamespace = "keyer";

esp_err_t open_namespace(nvs_handle_t *handle, nvs_open_mode mode) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_open(kNamespace, mode, handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open NVS namespace '%s': %s", kNamespace, esp_err_to_name(err));
    }
    return err;
}

uint16_t normalize_dot_tolerance(uint16_t value, uint16_t max) {
    if (value > max) {
        value = max;
    }
    if (value == 0) {
        return 0;
    }
    uint16_t rounded = static_cast<uint16_t>(((value + 5U) / 10U) * 10U);
    if (rounded > max) {
        rounded = max;
    }
    return rounded;
}

void clamp_config(persistent_config_t &cfg) {
    if (cfg.wpm < KEYER_WPM_MIN) {
        cfg.wpm = KEYER_WPM_MIN;
    } else if (cfg.wpm > KEYER_WPM_MAX) {
        cfg.wpm = KEYER_WPM_MAX;
    }

    if (cfg.mode > KEYER_MODE_ULTIMATIC) {
        cfg.mode = KEYER_MODE_DEFAULT;
    }

    if (cfg.window_up_percent > 100) {
        cfg.window_up_percent = 100;
    }
    if (cfg.window_down_percent > 100) {
        cfg.window_down_percent = 100;
    }

    if (cfg.debounce_us < 10) {
        cfg.debounce_us = 10;
    } else if (cfg.debounce_us > 10000) {
        cfg.debounce_us = 10000;
    }

    if (cfg.volume_percent > 100) {
        cfg.volume_percent = 100;
    }

    if (cfg.tone_frequency_hz < 200) {
        cfg.tone_frequency_hz = 200;
    } else if (cfg.tone_frequency_hz > 2000) {
        cfg.tone_frequency_hz = 2000;
    }

    if (cfg.fade_in_ms > 50) {
        cfg.fade_in_ms = 50;
    }
    if (cfg.fade_out_ms > 50) {
        cfg.fade_out_ms = 50;
    }

    if (cfg.char_space_tolerance_tenths > 50) {
        cfg.char_space_tolerance_tenths = 50;
    }
    if (cfg.word_space_tolerance_tenths > 70) {
        cfg.word_space_tolerance_tenths = 70;
    }
    cfg.char_space_tolerance_tenths = normalize_dot_tolerance(cfg.char_space_tolerance_tenths, 50);
    cfg.word_space_tolerance_tenths = normalize_dot_tolerance(cfg.word_space_tolerance_tenths, 70);
}

}  // namespace

void config_store_set_defaults(persistent_config_t *cfg) {
    if (!cfg) {
        return;
    }
    cfg->wpm = KEYER_WPM_DEFAULT;
    cfg->mode = KEYER_MODE_DEFAULT;
    cfg->window_up_percent = KEYER_MEMORY_WINDOW_UP;
    cfg->window_down_percent = KEYER_MEMORY_WINDOW_DOWN;
    cfg->debounce_us = PADDLE_DEBOUNCE_US;
    cfg->volume_percent = SIDETONE_VOLUME;
    cfg->tone_frequency_hz = SIDETONE_FREQ_HZ;
    cfg->fade_in_ms = SIDETONE_RAMP_UP_MS;
    cfg->fade_out_ms = SIDETONE_RAMP_DOWN_MS;
    cfg->char_space_tolerance_tenths = DECODER_CHAR_SPACE_TOLERANCE_TENTHS;
    cfg->word_space_tolerance_tenths = DECODER_WORD_SPACE_TOLERANCE_TENTHS;

    // RemoteCW defaults
    cfg->remotecw_enabled = false;
    strncpy(cfg->remotecw_server_ip, REMOTECW_SERVER_IP, sizeof(cfg->remotecw_server_ip) - 1);
    cfg->remotecw_server_port = REMOTECW_SERVER_PORT;
    strncpy(cfg->remotecw_username, REMOTECW_USERNAME, sizeof(cfg->remotecw_username) - 1);
    strncpy(cfg->remotecw_callsign, REMOTECW_CALLSIGN, sizeof(cfg->remotecw_callsign) - 1);
}

esp_err_t config_store_load(persistent_config_t *cfg, bool *out_loaded) {
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_loaded) {
        *out_loaded = false;
    }

    config_store_set_defaults(cfg);

    nvs_handle_t handle = 0;
    esp_err_t err = open_namespace(&handle, NVS_READONLY);
    if (err != ESP_OK) {
        return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
    }

    uint8_t u8_value = 0;
    uint16_t u16_value = 0;
    uint32_t u32_value = 0;

    if (nvs_get_u8(handle, "wpm", &u8_value) == ESP_OK) {
        cfg->wpm = u8_value;
    }
    if (nvs_get_u8(handle, "mode", &u8_value) == ESP_OK) {
        cfg->mode = u8_value;
    }
    if (nvs_get_u8(handle, "window_up", &u8_value) == ESP_OK) {
        cfg->window_up_percent = u8_value;
    }
    if (nvs_get_u8(handle, "window_down", &u8_value) == ESP_OK) {
        cfg->window_down_percent = u8_value;
    }
    if (nvs_get_u32(handle, "debounce", &u32_value) == ESP_OK) {
        cfg->debounce_us = u32_value;
    }
    if (nvs_get_u8(handle, "volume", &u8_value) == ESP_OK) {
        cfg->volume_percent = u8_value;
    }
    if (nvs_get_u16(handle, "frequency", &u16_value) == ESP_OK) {
        cfg->tone_frequency_hz = u16_value;
    }
    if (nvs_get_u8(handle, "fade_in", &u8_value) == ESP_OK) {
        cfg->fade_in_ms = u8_value;
    }
    if (nvs_get_u8(handle, "fade_out", &u8_value) == ESP_OK) {
        cfg->fade_out_ms = u8_value;
    }
    esp_err_t err_char = nvs_get_u16(handle, "char_tol", &u16_value);
    if (err_char == ESP_OK) {
        cfg->char_space_tolerance_tenths = u16_value;
    } else if (err_char == ESP_ERR_NVS_TYPE_MISMATCH) {
        if (nvs_get_u8(handle, "char_tol", &u8_value) == ESP_OK) {
            cfg->char_space_tolerance_tenths = static_cast<uint16_t>((3 * u8_value * 10 + 50) / 100);
        }
    }
    esp_err_t err_word = nvs_get_u16(handle, "word_tol", &u16_value);
    if (err_word == ESP_OK) {
        cfg->word_space_tolerance_tenths = u16_value;
    } else if (err_word == ESP_ERR_NVS_TYPE_MISMATCH) {
        if (nvs_get_u8(handle, "word_tol", &u8_value) == ESP_OK) {
            cfg->word_space_tolerance_tenths = static_cast<uint16_t>((7 * u8_value * 10 + 50) / 100);
        }
    }

    // Load RemoteCW configuration
    if (nvs_get_u8(handle, "rcw_enabled", &u8_value) == ESP_OK) {
        cfg->remotecw_enabled = (u8_value != 0);
    }
    size_t len = sizeof(cfg->remotecw_server_ip);
    nvs_get_str(handle, "rcw_ip", cfg->remotecw_server_ip, &len);
    if (nvs_get_u16(handle, "rcw_port", &u16_value) == ESP_OK) {
        cfg->remotecw_server_port = u16_value;
    }
    len = sizeof(cfg->remotecw_username);
    nvs_get_str(handle, "rcw_user", cfg->remotecw_username, &len);
    len = sizeof(cfg->remotecw_callsign);
    nvs_get_str(handle, "rcw_call", cfg->remotecw_callsign, &len);

    bool initialized = false;
    if (nvs_get_u8(handle, "initialized", &u8_value) == ESP_OK) {
        initialized = (u8_value != 0);
    }

    nvs_close(handle);

    clamp_config(*cfg);

    if (out_loaded) {
        *out_loaded = initialized;
    }

    ESP_LOGI(TAG, "Loaded config: WPM=%u mode=%u win=%u/%u debounce=%lu volume=%u freq=%u fade=%u/%u",
             cfg->wpm, cfg->mode, cfg->window_up_percent, cfg->window_down_percent,
             static_cast<unsigned long>(cfg->debounce_us), cfg->volume_percent,
             cfg->tone_frequency_hz, cfg->fade_in_ms, cfg->fade_out_ms);
    ESP_LOGI(TAG, "  Decoder extra dots: char=+%u, word=+%u",
             cfg->char_space_tolerance_tenths / 10,
             cfg->word_space_tolerance_tenths / 10);
    return ESP_OK;
}

esp_err_t config_store_save(const persistent_config_t *cfg) {
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    persistent_config_t temp = *cfg;
    clamp_config(temp);

    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(open_namespace(&handle, NVS_READWRITE), TAG, "open NVS");

    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(nvs_set_u8(handle, "wpm", temp.wpm), cleanup, TAG, "set wpm");
    ESP_GOTO_ON_ERROR(nvs_set_u8(handle, "mode", temp.mode), cleanup, TAG, "set mode");
    ESP_GOTO_ON_ERROR(nvs_set_u8(handle, "window_up", temp.window_up_percent), cleanup, TAG, "set window_up");
    ESP_GOTO_ON_ERROR(nvs_set_u8(handle, "window_down", temp.window_down_percent), cleanup, TAG, "set window_down");
    ESP_GOTO_ON_ERROR(nvs_set_u32(handle, "debounce", temp.debounce_us), cleanup, TAG, "set debounce");
    ESP_GOTO_ON_ERROR(nvs_set_u8(handle, "volume", temp.volume_percent), cleanup, TAG, "set volume");
    ESP_GOTO_ON_ERROR(nvs_set_u16(handle, "frequency", temp.tone_frequency_hz), cleanup, TAG, "set freq");
    ESP_GOTO_ON_ERROR(nvs_set_u8(handle, "fade_in", temp.fade_in_ms), cleanup, TAG, "set fade_in");
    ESP_GOTO_ON_ERROR(nvs_set_u8(handle, "fade_out", temp.fade_out_ms), cleanup, TAG, "set fade_out");
    ESP_GOTO_ON_ERROR(nvs_set_u16(handle, "char_tol", temp.char_space_tolerance_tenths), cleanup, TAG, "set char_tol");
    ESP_GOTO_ON_ERROR(nvs_set_u16(handle, "word_tol", temp.word_space_tolerance_tenths), cleanup, TAG, "set word_tol");

    // Save RemoteCW configuration
    ESP_GOTO_ON_ERROR(nvs_set_u8(handle, "rcw_enabled", temp.remotecw_enabled ? 1 : 0), cleanup, TAG, "set rcw_enabled");
    ESP_GOTO_ON_ERROR(nvs_set_str(handle, "rcw_ip", temp.remotecw_server_ip), cleanup, TAG, "set rcw_ip");
    ESP_GOTO_ON_ERROR(nvs_set_u16(handle, "rcw_port", temp.remotecw_server_port), cleanup, TAG, "set rcw_port");
    ESP_GOTO_ON_ERROR(nvs_set_str(handle, "rcw_user", temp.remotecw_username), cleanup, TAG, "set rcw_user");
    ESP_GOTO_ON_ERROR(nvs_set_str(handle, "rcw_call", temp.remotecw_callsign), cleanup, TAG, "set rcw_call");

    ESP_GOTO_ON_ERROR(nvs_set_u8(handle, "initialized", 1), cleanup, TAG, "set initialized");

    ret = nvs_commit(handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Configuration saved (fade %u/%u ms, extra dots +%u/+%u)",
                 temp.fade_in_ms, temp.fade_out_ms,
                 temp.char_space_tolerance_tenths / 10,
                 temp.word_space_tolerance_tenths / 10);
    }

cleanup:
    nvs_close(handle);
    return ret;
}

void config_store_apply_to_runtime(const persistent_config_t *cfg,
                                   KeyerLogic *keyer,
                                   tone_generator_t *tone,
                                   MorseDecoder *decoder) {
    if (!cfg) {
        return;
    }
    persistent_config_t temp = *cfg;
    clamp_config(temp);

    ESP_LOGI(TAG, "Applying config: WPM=%u mode=%u debounce=%lu vol=%u freq=%u fade=%u/%u extra dots +%u/+%u",
             temp.wpm, temp.mode, (unsigned long)temp.debounce_us, temp.volume_percent, temp.tone_frequency_hz,
             temp.fade_in_ms, temp.fade_out_ms,
             temp.char_space_tolerance_tenths / 10, temp.word_space_tolerance_tenths / 10);

    if (keyer) {
        keyer->setWPM(temp.wpm);
        keyer->setMode(temp.mode);
        keyer->setMemoryWindow(temp.window_up_percent, temp.window_down_percent);
        keyer->setDebounce(temp.debounce_us);
    }
    if (tone) {
        tone_generator_set_volume(tone, temp.volume_percent);
        tone_generator_set_frequency(tone, temp.tone_frequency_hz);
        tone_generator_set_fade(tone, temp.fade_in_ms, temp.fade_out_ms);
    }
    if (decoder) {
        decoder->setCharSpaceToleranceDots(temp.char_space_tolerance_tenths);
        decoder->setWordSpaceToleranceDots(temp.word_space_tolerance_tenths);
    }
}

void config_store_snapshot_from_runtime(persistent_config_t *cfg,
                                        KeyerLogic *keyer,
                                        tone_generator_t *tone,
                                        MorseDecoder *decoder) {
    if (!cfg) {
        return;
    }
    config_store_set_defaults(cfg);

    if (keyer) {
        cfg->wpm = keyer->getWPM();
        cfg->mode = keyer->getMode();
        keyer->getMemoryWindow(&cfg->window_up_percent, &cfg->window_down_percent);
        cfg->debounce_us = keyer->getDebounce();
    }
    if (tone) {
        cfg->volume_percent = tone_generator_get_volume(tone);
        cfg->tone_frequency_hz = tone_generator_get_frequency(tone);
        cfg->fade_in_ms = tone_generator_get_fade_in_ms(tone);
        cfg->fade_out_ms = tone_generator_get_fade_out_ms(tone);
    }
    if (decoder) {
        cfg->char_space_tolerance_tenths = decoder->getCharSpaceToleranceDots();
        cfg->word_space_tolerance_tenths = decoder->getWordSpaceToleranceDots();
    }

    clamp_config(*cfg);
    ESP_LOGI(TAG, "Snapshot runtime: fade=%u/%u extra dots +%u/+%u",
             cfg->fade_in_ms, cfg->fade_out_ms,
             cfg->char_space_tolerance_tenths / 10,
             cfg->word_space_tolerance_tenths / 10);
}
