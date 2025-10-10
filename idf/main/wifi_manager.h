#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_manager_start(void);
bool wifi_manager_sta_connected(void);

#endif // WIFI_MANAGER_H
