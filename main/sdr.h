#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ─────────────────────────────────────────────
// PINES - Pantalla ST7789
// ─────────────────────────────────────────────
#define PIN_TFT_RST     5
#define PIN_TFT_CS      6
#define PIN_TFT_DC      7
#define PIN_TFT_SCLK    8
#define PIN_TFT_MOSI    9
#define PIN_TFT_BL      38

// ─────────────────────────────────────────────
// PINES - MSI001 (SPI compartido con pantalla)
// ─────────────────────────────────────────────
#define PIN_MSI001_CS   10
// SCLK y MOSI compartidos con pantalla (8, 9)

// ─────────────────────────────────────────────
// PINES - NAU8822 I2S
// ─────────────────────────────────────────────
#define PIN_I2S_BCLK    17
#define PIN_I2S_WCLK    18
#define PIN_I2S_DIN     11   // ESP32 → NAU8822
#define PIN_I2S_DOUT    12   // NAU8822 → ESP32 (IQ)

// ─────────────────────────────────────────────
// PINES - NAU8822 I2C
// ─────────────────────────────────────────────
#define PIN_I2C_SDA     21
#define PIN_I2C_SCL     16

// ─────────────────────────────────────────────
// PINES - Encoder
// ─────────────────────────────────────────────
#define PIN_ENC_A       1
#define PIN_ENC_B       2
#define PIN_ENC_SW      3

// ─────────────────────────────────────────────
// PANTALLA
// ─────────────────────────────────────────────
#define TFT_WIDTH       320
#define TFT_HEIGHT      170
#define TFT_ROW_OFFSET  35

// ─────────────────────────────────────────────
// FFT
// ─────────────────────────────────────────────
#define FFT_SIZE        512
#define SAMPLE_RATE     48000
#define FFT_BIN_HZ      (SAMPLE_RATE / FFT_SIZE)   // ~93 Hz/bin

// ─────────────────────────────────────────────
// DISPLAY LAYOUT (px)
// ─────────────────────────────────────────────
#define UI_TOP_BAR_H    20
#define UI_SPECTRUM_H   60
#define UI_WATERFALL_H  70
#define UI_BOT_BAR_H    20

// ─────────────────────────────────────────────
// MODOS DE DEMODULACIÓN
// ─────────────────────────────────────────────
typedef enum {
    MODE_AM = 0,
    MODE_FM,
    MODE_USB,
    MODE_LSB,
    MODE_CW,
    MODE_COUNT
} sdr_mode_t;

// ─────────────────────────────────────────────
// ESTADO GLOBAL SDR
// ─────────────────────────────────────────────
typedef struct {
    uint32_t    frequency_hz;
    sdr_mode_t  mode;
    uint8_t     volume;         // 0-100
    uint8_t     agc_enabled;
    int8_t      signal_level;   // S-meter dBm
} sdr_state_t;

// ─────────────────────────────────────────────
// MENSAJES ENTRE TAREAS
// ─────────────────────────────────────────────
typedef struct {
    float magnitude[FFT_SIZE / 2];  // Espectro FFT
} fft_result_t;

// ─────────────────────────────────────────────
// COLAS GLOBALES
// ─────────────────────────────────────────────
extern QueueHandle_t q_fft_result;   // fft → display
extern QueueHandle_t q_encoder;      // encoder → ui

// ─────────────────────────────────────────────
// ESTADO GLOBAL
// ─────────────────────────────────────────────
extern sdr_state_t g_sdr_state;
