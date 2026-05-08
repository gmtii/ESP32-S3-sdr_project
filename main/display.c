#include "display.h"
#include "sdr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "DISPLAY";

// ─────────────────────────────────────────────
// ST7789 Comandos
// ─────────────────────────────────────────────
#define ST7789_SWRESET  0x01
#define ST7789_SLPOUT   0x11
#define ST7789_COLMOD   0x3A
#define ST7789_MADCTL   0x36
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C
#define ST7789_DISPON   0x29
#define ST7789_INVON    0x21

// ─────────────────────────────────────────────
// Framebuffer en PSRAM
// ─────────────────────────────────────────────
#define FB_SIZE  (TFT_WIDTH * TFT_HEIGHT * 2)
static uint16_t *framebuffer = NULL;

// Waterfall en PSRAM
#define WF_COLS  TFT_WIDTH
#define WF_ROWS  UI_WATERFALL_H
static uint16_t *wf_buffer = NULL;
static int wf_row = 0;

static spi_device_handle_t spi_tft;

// ─────────────────────────────────────────────
// PWM Backlight
// ─────────────────────────────────────────────
#define BL_LEDC_TIMER    LEDC_TIMER_0
#define BL_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BL_LEDC_FREQ     5000
#define BL_LEDC_RES      LEDC_TIMER_8_BIT

void display_set_backlight(uint8_t brightness) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

static void backlight_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_RES,
        .freq_hz         = BL_LEDC_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num   = PIN_TFT_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 128,   // 50% por defecto
        .hpoint     = 0,
    };
    ledc_channel_config(&channel);
}

// ─────────────────────────────────────────────
// SPI helpers
// ─────────────────────────────────────────────
static void tft_cmd(uint8_t cmd) {
    gpio_set_level(PIN_TFT_DC, 0);
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &cmd,
        .flags     = 0,
    };
    spi_device_polling_transmit(spi_tft, &t);
}

static void tft_data(const uint8_t *data, int len) {
    if (len == 0) return;
    gpio_set_level(PIN_TFT_DC, 1);
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(spi_tft, &t);
}

static void tft_data16(uint16_t val) {
    uint8_t buf[2] = { val >> 8, val & 0xFF };
    tft_data(buf, 2);
}

static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    y0 += TFT_ROW_OFFSET;
    y1 += TFT_ROW_OFFSET;
    tft_cmd(ST7789_CASET);
    tft_data16(x0); tft_data16(x1);
    tft_cmd(ST7789_RASET);
    tft_data16(y0); tft_data16(y1);
    tft_cmd(ST7789_RAMWR);
}

// ─────────────────────────────────────────────
// Vuelca framebuffer completo por DMA
// ─────────────────────────────────────────────
static void flush_framebuffer(void) {
    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    gpio_set_level(PIN_TFT_DC, 1);

    // El ST7789 necesita bytes en big-endian
    // Enviamos en chunks de 32KB (límite SPI DMA)
    const int CHUNK = 32768 / 2;  // pixels por chunk
    int total = TFT_WIDTH * TFT_HEIGHT;
    int offset = 0;

    while (offset < total) {
        int count = (total - offset) > CHUNK ? CHUNK : (total - offset);
        spi_transaction_t t = {
            .length    = count * 16,
            .tx_buffer = &framebuffer[offset],
        };
        spi_device_polling_transmit(spi_tft, &t);
        offset += count;
    }
}

// ─────────────────────────────────────────────
// Dibuja en framebuffer (sin SPI)
// ─────────────────────────────────────────────
static inline uint16_t swap16(uint16_t c) {
    return (c >> 8) | (c << 8);
}

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color) {
    uint16_t c = swap16(color);
    for (int row = y; row < y + h && row < TFT_HEIGHT; row++) {
        uint16_t *p = &framebuffer[row * TFT_WIDTH + x];
        for (int col = 0; col < w; col++) p[col] = c;
    }
}

static void fb_vline(int x, int y, int h, uint16_t color) {
    uint16_t c = swap16(color);
    for (int row = y; row < y + h && row < TFT_HEIGHT; row++) {
        framebuffer[row * TFT_WIDTH + x] = c;
    }
}

static const uint8_t *font5x7_glyph(char ch) {
    static const uint8_t space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t dot[5]   = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t n0[5]    = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t n1[5]    = {0x00, 0x42, 0x7F, 0x40, 0x00};
    static const uint8_t n2[5]    = {0x42, 0x61, 0x51, 0x49, 0x46};
    static const uint8_t n3[5]    = {0x21, 0x41, 0x45, 0x4B, 0x31};
    static const uint8_t n4[5]    = {0x18, 0x14, 0x12, 0x7F, 0x10};
    static const uint8_t n5[5]    = {0x27, 0x45, 0x45, 0x45, 0x39};
    static const uint8_t n6[5]    = {0x3C, 0x4A, 0x49, 0x49, 0x30};
    static const uint8_t n7[5]    = {0x01, 0x71, 0x09, 0x05, 0x03};
    static const uint8_t n8[5]    = {0x36, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t n9[5]    = {0x06, 0x49, 0x49, 0x29, 0x1E};
    static const uint8_t a[5]     = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    static const uint8_t b[5]     = {0x7F, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t c[5]     = {0x3E, 0x41, 0x41, 0x41, 0x22};
    static const uint8_t f[5]     = {0x7F, 0x09, 0x09, 0x09, 0x01};
    static const uint8_t h[5]     = {0x7F, 0x08, 0x08, 0x08, 0x7F};
    static const uint8_t l[5]     = {0x7F, 0x40, 0x40, 0x40, 0x40};
    static const uint8_t m[5]     = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    static const uint8_t n[5]     = {0x7F, 0x04, 0x08, 0x10, 0x7F};
    static const uint8_t o[5]     = {0x3E, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t s[5]     = {0x26, 0x49, 0x49, 0x49, 0x32};
    static const uint8_t u[5]     = {0x3F, 0x40, 0x40, 0x40, 0x3F};
    static const uint8_t v[5]     = {0x1F, 0x20, 0x40, 0x20, 0x1F};
    static const uint8_t w[5]     = {0x7F, 0x20, 0x18, 0x20, 0x7F};
    static const uint8_t z[5]     = {0x61, 0x51, 0x49, 0x45, 0x43};
    static const uint8_t g[5]     = {0x3E, 0x41, 0x49, 0x49, 0x7A};
    static const uint8_t dash[5]  = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t pct[5]   = {0x23, 0x13, 0x08, 0x64, 0x62};

    switch (ch) {
    case '0': return n0; case '1': return n1; case '2': return n2;
    case '3': return n3; case '4': return n4; case '5': return n5;
    case '6': return n6; case '7': return n7; case '8': return n8;
    case '9': return n9; case '.': return dot; case ' ': return space;
    case '-': return dash; case '%': return pct;
    case 'A': return a;  case 'B': return b;  case 'C': return c;
    case 'F': return f;  case 'G': return g;  case 'H': return h;
    case 'L': return l;  case 'M': return m;  case 'N': return n;
    case 'O': return o;  case 'S': return s;  case 'U': return u;
    case 'V': return v;  case 'W': return w;  case 'Z': return z;
    default: return space;
    }
}

static void fb_draw_text_5x7(int x, int y, const char *text, uint16_t color, int scale) {
    const uint16_t c = swap16(color);
    const int step = 6 * scale;

    while (*text && x + (5 * scale) <= TFT_WIDTH) {
        const uint8_t *glyph = font5x7_glyph(*text++);
        for (int col = 0; col < 5; col++) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; row++) {
                if ((bits & (1U << row)) == 0) continue;
                uint16_t *p = &framebuffer[(y + row * scale) * TFT_WIDTH + x + col * scale];
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) p[sx] = c;
                    p += TFT_WIDTH;
                }
            }
        }
        x += step;
    }
}

// ─────────────────────────────────────────────
// Color waterfall
// ─────────────────────────────────────────────
static uint16_t magnitude_to_color(float mag) {
    if (mag < 0.0f) mag = 0.0f;
    if (mag > 1.0f) mag = 1.0f;

    uint8_t r, g, b;
    if (mag < 0.33f) {
        float t = mag / 0.33f;
        r = 0; g = (uint8_t)(t * 255); b = 255;
    } else if (mag < 0.66f) {
        float t = (mag - 0.33f) / 0.33f;
        r = (uint8_t)(t * 255); g = 255; b = (uint8_t)((1 - t) * 255);
    } else {
        float t = (mag - 0.66f) / 0.34f;
        r = 255; g = (uint8_t)((1 - t) * 255); b = 0;
    }
    // RGB888 → RGB565 big-endian
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return swap16(c);
}

// ─────────────────────────────────────────────
// INIT
// ─────────────────────────────────────────────
void display_init(void) {
    ESP_LOGI(TAG, "Iniciando display...");

    // Aloja framebuffer y waterfall en PSRAM
    framebuffer = heap_caps_malloc(FB_SIZE, MALLOC_CAP_SPIRAM);
    wf_buffer   = heap_caps_malloc(WF_COLS * WF_ROWS * 2, MALLOC_CAP_SPIRAM);
    assert(framebuffer && wf_buffer);
    memset(framebuffer, 0, FB_SIZE);
    memset(wf_buffer, 0, WF_COLS * WF_ROWS * 2);

    // Backlight PWM
    backlight_init();

    // DC pin
    gpio_config_t dc_cfg = {
        .pin_bit_mask = (1ULL << PIN_TFT_DC),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&dc_cfg);

    // SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_TFT_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = PIN_TFT_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 32768,
    };
    spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t tft_cfg = {
        .clock_speed_hz = 80 * 1000 * 1000,   // 80MHz con PSRAM
        .mode           = 0,
        .spics_io_num   = PIN_TFT_CS,
        .queue_size     = 7,
    };
    spi_bus_add_device(SPI2_HOST, &tft_cfg, &spi_tft);

    // Reset
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << PIN_TFT_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(PIN_TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Init ST7789
    tft_cmd(ST7789_SWRESET); vTaskDelay(pdMS_TO_TICKS(150));
    tft_cmd(ST7789_SLPOUT);  vTaskDelay(pdMS_TO_TICKS(150));
    tft_cmd(ST7789_COLMOD);  tft_data((uint8_t[]){0x55}, 1);
    tft_cmd(ST7789_MADCTL);  tft_data((uint8_t[]){0x70}, 0x60);  // Horizontal
    tft_cmd(ST7789_INVON);
    tft_cmd(ST7789_DISPON);  vTaskDelay(pdMS_TO_TICKS(50));

    fb_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COLOR_BG);
    flush_framebuffer();

    ESP_LOGI(TAG, "Display OK - framebuffer PSRAM %.1fKB", FB_SIZE / 1024.0f);
}

// ─────────────────────────────────────────────
// SPECTRUM en framebuffer
// ─────────────────────────────────────────────
void display_draw_spectrum(float *magnitudes, int count) {
    int y_base = UI_TOP_BAR_H + UI_SPECTRUM_H;

    fb_fill_rect(0, UI_TOP_BAR_H, TFT_WIDTH, UI_SPECTRUM_H, COLOR_BG);

    // Grid
    for (int g = 1; g < 4; g++) {
        int y = UI_TOP_BAR_H + (UI_SPECTRUM_H / 4) * g;
        fb_fill_rect(0, y, TFT_WIDTH, 1, COLOR_GRID);
    }

    // Barras
    for (int x = 0; x < TFT_WIDTH; x++) {
        int bin = (x * count) / TFT_WIDTH;
        float mag = magnitudes[bin];
        int h = (int)(mag * UI_SPECTRUM_H);
        if (h < 1) h = 1;
        if (h > UI_SPECTRUM_H) h = UI_SPECTRUM_H;
        fb_vline(x, y_base - h, h, COLOR_SPECTRUM);
    }
}

// ─────────────────────────────────────────────
// WATERFALL en framebuffer
// ─────────────────────────────────────────────
void display_draw_waterfall(float *magnitudes, int count) {
    int y_start = UI_TOP_BAR_H + UI_SPECTRUM_H;

    // Nueva línea
    for (int x = 0; x < WF_COLS; x++) {
        int bin = (x * count) / WF_COLS;
        wf_buffer[wf_row * WF_COLS + x] = magnitude_to_color(magnitudes[bin]);
    }
    wf_row = (wf_row + 1) % WF_ROWS;

    // Copia waterfall al framebuffer (invertido)
    for (int row = 0; row < WF_ROWS; row++) {
        int buf_row = (wf_row + row) % WF_ROWS;
        memcpy(
            &framebuffer[(y_start + (WF_ROWS - 1 - row)) * TFT_WIDTH],
            &wf_buffer[buf_row * WF_COLS],
            WF_COLS * 2
        );
    }
}

// ─────────────────────────────────────────────
// BARRAS
// ─────────────────────────────────────────────
void display_draw_top_bar(void) {
    fb_fill_rect(0, 0, TFT_WIDTH, UI_TOP_BAR_H, COLOR_TOPBAR);
    static const char *mode_names[MODE_COUNT] = {"AM", "FM", "USB", "LSB", "CW"};
    const uint32_t hz = g_sdr_state.frequency_hz;
    const sdr_mode_t mode = g_sdr_state.mode;
    char text[32];

    snprintf(text, sizeof(text), "%lu.%03lu.%03lu MHZ %s",
             (unsigned long)(hz / 1000000U),
             (unsigned long)((hz / 1000U) % 1000U),
             (unsigned long)(hz % 1000U),
             mode < MODE_COUNT ? mode_names[mode] : "AM");
    fb_draw_text_5x7(4, 3, text, COLOR_FREQ, 2);
}

void display_draw_bot_bar(void) {
    int y = UI_TOP_BAR_H + UI_SPECTRUM_H + UI_WATERFALL_H;
    fb_fill_rect(0, y, TFT_WIDTH, UI_BOT_BAR_H, COLOR_BOTBAR);

    char text[32];
    snprintf(text, sizeof(text), "VOL %u%% AGC %s S %d",
             (unsigned)g_sdr_state.volume,
             g_sdr_state.agc_enabled ? "ON" : "OFF",
             (int)g_sdr_state.signal_level);
    fb_draw_text_5x7(4, y + 4, text, COLOR_TEXT, 1);
}

// ─────────────────────────────────────────────
// UPDATE
// ─────────────────────────────────────────────
void display_update(void) {
    fft_result_t result;
    if (xQueueReceive(q_fft_result, &result, pdMS_TO_TICKS(30)) != pdTRUE) return;

    display_draw_top_bar();
    display_draw_spectrum(result.magnitude, FFT_SIZE / 2);
    display_draw_waterfall(result.magnitude, FFT_SIZE / 2);
    display_draw_bot_bar();

    flush_framebuffer();
}
