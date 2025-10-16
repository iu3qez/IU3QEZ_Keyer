#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdint.h>

#include "esp_err.h"

#include "tone_generator.h"

class KeyerLogic;
class MorseDecoder;

typedef struct {
    uint8_t wpm;
    uint8_t mode;
    uint8_t window_up_percent;
    uint8_t window_down_percent;
    uint32_t debounce_us;
    uint8_t volume_percent;
    uint16_t tone_frequency_hz;
    uint8_t fade_in_ms;
    uint8_t fade_out_ms;
    uint16_t char_space_tolerance_tenths;
    uint16_t word_space_tolerance_tenths;

    // RemoteCW network configuration
    bool remotecw_enabled;
    char remotecw_server_ip[64];
    uint16_t remotecw_server_port;
    char remotecw_username[84];
    char remotecw_callsign[84];
} persistent_config_t;

void config_store_set_defaults(persistent_config_t *cfg);
esp_err_t config_store_load(persistent_config_t *cfg, bool *out_loaded);
esp_err_t config_store_save(const persistent_config_t *cfg);
void config_store_apply_to_runtime(const persistent_config_t *cfg,
                                   KeyerLogic *keyer,
                                   tone_generator_t *tone,
                                   MorseDecoder *decoder);
void config_store_snapshot_from_runtime(persistent_config_t *cfg,
                                        KeyerLogic *keyer,
                                        tone_generator_t *tone,
                                        MorseDecoder *decoder);

#endif // CONFIG_STORE_H
