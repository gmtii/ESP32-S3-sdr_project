#include "encoder.h"
#include "sdr.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "ENCODER";

// Paso de sintonía según modo
#define STEP_FINE    100      // 100 Hz
#define STEP_NORMAL  1000     // 1 kHz
#define STEP_FAST    10000    // 10 kHz

static volatile int8_t enc_delta = 0;
static volatile uint8_t enc_last = 0;

static void IRAM_ATTR encoder_isr(void *arg) {
    uint8_t a = gpio_get_level(PIN_ENC_A);
    uint8_t b = gpio_get_level(PIN_ENC_B);
    uint8_t state = (a << 1) | b;

    if (enc_last == 0b10 && state == 0b00) enc_delta++;
    if (enc_last == 0b01 && state == 0b00) enc_delta--;

    enc_last = state;
}

void encoder_init(void) {
    gpio_config_t enc_cfg = {
        .pin_bit_mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B) | (1ULL << PIN_ENC_SW),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&enc_cfg);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_ENC_A, encoder_isr, NULL);
    gpio_isr_handler_add(PIN_ENC_B, encoder_isr, NULL);

    ESP_LOGI(TAG, "Encoder OK");
}

void encoder_process(void) {
    // Procesa delta del encoder
    if (enc_delta != 0) {
        int8_t d = enc_delta;
        enc_delta = 0;

        int32_t step = STEP_NORMAL;

        // Cambia frecuencia
        int32_t new_freq = (int32_t)g_sdr_state.frequency_hz + d * step;
        if (new_freq < 100000)    new_freq = 100000;    // min 100 kHz
        if (new_freq > 30000000)  new_freq = 30000000;  // max 30 MHz
        g_sdr_state.frequency_hz = (uint32_t)new_freq;

        // TODO: actualizar MSI001 con nueva frecuencia
        // msi001_set_frequency(g_sdr_state.frequency_hz);

        int8_t msg = d;
        xQueueSend(q_encoder, &msg, 0);
    }

    // Pulsador - cambia modo
    static uint8_t sw_last = 1;
    uint8_t sw = gpio_get_level(PIN_ENC_SW);
    if (sw == 0 && sw_last == 1) {
        g_sdr_state.mode = (g_sdr_state.mode + 1) % MODE_COUNT;
        ESP_LOGI(TAG, "Modo: %d", g_sdr_state.mode);
    }
    sw_last = sw;
}
