#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "sdr.h"
#include "display.h"
#include "fft.h"
#include "audio.h"
#include "encoder.h"

static const char *TAG = "SDR_MAIN";

// ─────────────────────────────────────────────
// COLAS GLOBALES
// ─────────────────────────────────────────────
QueueHandle_t q_fft_result;
QueueHandle_t q_encoder;

// ─────────────────────────────────────────────
// ESTADO GLOBAL
// ─────────────────────────────────────────────
sdr_state_t g_sdr_state = {
    .frequency_hz  = 14205000,   // 14.205 MHz - 20m SSB
    .mode          = MODE_USB,
    .volume        = 70,
    .agc_enabled   = 1,
    .signal_level  = -100,
};

// ─────────────────────────────────────────────
// TAREAS
// ─────────────────────────────────────────────

// Core 0 - Tiempo real
static void task_audio(void *arg) {
    audio_init();
    while (1) {
        audio_process();
    }
}

static void task_fft(void *arg) {
    fft_init();
    while (1) {
        fft_process();
    }
}

// Core 1 - UI
static void task_display(void *arg) {
    display_init();
    while (1) {
        display_update();
        vTaskDelay(pdMS_TO_TICKS(33));  // ~30fps
    }
}

static void task_encoder(void *arg) {
    encoder_init();
    while (1) {
        encoder_process();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ─────────────────────────────────────────────
// APP MAIN
// ─────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "SDR ESP32-S3 iniciando...");

    // Colas
    q_fft_result = xQueueCreate(1, sizeof(fft_result_t));  // xQueueOverwrite requiere tamaño 1
    q_encoder    = xQueueCreate(10, sizeof(int8_t));

    // Core 0 - Audio + FFT (tiempo real)
    xTaskCreatePinnedToCore(task_audio,   "audio",   8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_fft,     "fft",     8192, NULL, 4, NULL, 0);

    // Core 1 - Display + Encoder (UI)
    xTaskCreatePinnedToCore(task_display, "display", 8192, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(task_encoder, "encoder", 4096, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "Tareas iniciadas");
}
