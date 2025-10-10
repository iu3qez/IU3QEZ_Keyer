#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

#include "tone_generator.h"

class KeyerLogic;
class MorseDecoder;
class TimelineBuffer;

typedef struct {
    KeyerLogic *keyer;
    tone_generator_t *tone;
    MorseDecoder *decoder;
    TimelineBuffer *timeline;
} web_server_config_t;

esp_err_t web_server_start(const web_server_config_t &config);

#endif // WEB_SERVER_H
