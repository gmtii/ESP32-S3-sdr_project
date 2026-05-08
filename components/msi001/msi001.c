#include "msi001.h"
#include "esp_log.h"

static const char *TAG = "MSI001";

// TODO: integra aquí tu librería MSI001 portada de Linux

esp_err_t msi001_init(void) {
    ESP_LOGI(TAG, "MSI001 init (stub)");
    return ESP_OK;
}

esp_err_t msi001_set_frequency(uint32_t freq_hz) {
    ESP_LOGI(TAG, "Set freq: %lu Hz", freq_hz);
    return ESP_OK;
}

esp_err_t msi001_set_gain(uint8_t gain) {
    ESP_LOGI(TAG, "Set gain: %d", gain);
    return ESP_OK;
}
