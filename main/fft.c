#include "fft.h"
#include "sdr.h"
#include "esp_log.h"
#include "esp_dsp.h"
#include <math.h>
#include <string.h>

static const char *TAG = "FFT";

// Por esto (alineado a 16 bytes):
static float fft_input[FFT_SIZE * 2] __attribute__((aligned(16)));
static float fft_wind[FFT_SIZE]      __attribute__((aligned(16)));
static float fft_out[FFT_SIZE * 2]   __attribute__((aligned(16)));
static fft_result_t fft_result;

// Buffer IQ compartido con audio (protegido por cola)
// audio.c llena este buffer vía q_iq_samples
extern QueueHandle_t q_iq_samples;

void fft_init(void) {
    ESP_LOGI(TAG, "Iniciando FFT...");

    // Inicializa tablas internas esp-dsp
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error init FFT: %d", ret);
        return;
    }

    dsps_wind_hann_f32(fft_wind, FFT_SIZE);

    ESP_LOGI(TAG, "FFT OK - %d puntos, %.1f Hz/bin", FFT_SIZE, (float)FFT_BIN_HZ);
}

void fft_process(void) {
    // Espera muestras IQ del audio
    float iq_buf[FFT_SIZE * 2];
    if (xQueueReceive(q_iq_samples, iq_buf, portMAX_DELAY) != pdTRUE) return;

    // Aplica ventana Hann a I y Q
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_input[i * 2]     = iq_buf[i * 2]     * fft_wind[i];  // I
        fft_input[i * 2 + 1] = iq_buf[i * 2 + 1] * fft_wind[i];  // Q
    }

    // FFT compleja (esp-dsp)
    memcpy(fft_out, fft_input, sizeof(fft_input));
    dsps_fft2r_fc32(fft_out, FFT_SIZE);
    dsps_bit_rev_fc32(fft_out, FFT_SIZE);

    // Calcula magnitudes y normaliza
    float max_mag = 1e-10f;
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float re = fft_out[i * 2];
        float im = fft_out[i * 2 + 1];
        float mag = sqrtf(re * re + im * im);
        fft_result.magnitude[i] = mag;
        if (mag > max_mag) max_mag = mag;
    }

    // Normaliza 0.0 - 1.0
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        fft_result.magnitude[i] /= max_mag;
    }

    // Envía al display (no bloqueante)
    xQueueOverwrite(q_fft_result, &fft_result);
}
