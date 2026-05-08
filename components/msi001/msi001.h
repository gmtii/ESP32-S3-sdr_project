#pragma once
#include <stdint.h>
#include "esp_err.h"

// Reemplaza con tu librería MSI001
esp_err_t msi001_init(void);
esp_err_t msi001_set_frequency(uint32_t freq_hz);
esp_err_t msi001_set_gain(uint8_t gain);
