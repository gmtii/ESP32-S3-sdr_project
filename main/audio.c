#include "audio.h"
#include "sdr.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include <math.h>
#include <string.h>

static const char *TAG = "AUDIO";

// Cola IQ hacia FFT
QueueHandle_t q_iq_samples;

static i2s_chan_handle_t i2s_rx_chan;

// ─────────────────────────────────────────────
// NAU8822 I2C
// ─────────────────────────────────────────────
#define NAU8822_ADDR    0x1A
static i2c_master_bus_handle_t  i2c_bus;
static i2c_master_dev_handle_t  i2c_nau;

static esp_err_t nau8822_write(uint8_t reg, uint16_t val) {
    uint8_t buf[2] = {
        (reg << 1) | ((val >> 8) & 0x01),
        val & 0xFF
    };
    return i2c_master_transmit(i2c_nau, buf, 2, pdMS_TO_TICKS(100));
}

static void nau8822_init(void) {
    // Reset
    nau8822_write(0x00, 0x000);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Power management
    nau8822_write(0x01, 0x01F);  // PGA, ADC, mic bias ON
    nau8822_write(0x02, 0x1BF);  // ADC L+R ON
    nau8822_write(0x03, 0x06F);  // DAC L+R, LOUT, ROUT ON

    // Clock: MCLK → fs=48kHz, BCLK=64fs
    nau8822_write(0x06, 0x000);  // MCLK, sin PLL
    nau8822_write(0x07, 0x006);  // 48kHz

    // I2S - 16 bit
    nau8822_write(0x04, 0x010);  // I2S format, 16bit

    // ADC input: Line In diferencial
    nau8822_write(0x2C, 0x003);  // LMIXSEL: LIN
    nau8822_write(0x2D, 0x003);  // RMIXSEL: RIN
    nau8822_write(0x2E, 0x010);  // Left mixer ON
    nau8822_write(0x2F, 0x010);  // Right mixer ON

    // Ganancia ADC
    nau8822_write(0x0F, 0x1FF);  // ADC L max gain
    nau8822_write(0x10, 0x1FF);  // ADC R max gain

    ESP_LOGI(TAG, "NAU8822 configurado");
}

// ─────────────────────────────────────────────
// I2S INIT
// ─────────────────────────────────────────────
static void i2s_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, NULL, &i2s_rx_chan);

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WCLK,
            .dout = PIN_I2S_DIN,
            .din  = PIN_I2S_DOUT,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    i2s_channel_init_std_mode(i2s_rx_chan, &std_cfg);
    i2s_channel_enable(i2s_rx_chan);
}

// ─────────────────────────────────────────────
// SEÑAL SIMULADA (para pruebas sin hardware RF)
// Genera tono IQ a 5kHz
// ─────────────────────────────────────────────
#define SIMULATE_AUDIO  1   // Cambia a 0 cuando tengas hardware

static float sim_phase = 0.0f;
static float sim_iq_buf[FFT_SIZE * 2];

static void generate_sim_iq(void) {
    float freq   = 5000.0f;   // 5kHz tono
    float delta  = 2.0f * M_PI * freq / SAMPLE_RATE;

    for (int i = 0; i < FFT_SIZE; i++) {
        sim_iq_buf[i * 2]     = cosf(sim_phase);   // I
        sim_iq_buf[i * 2 + 1] = sinf(sim_phase);   // Q
        sim_phase += delta;
        if (sim_phase > 2.0f * M_PI) sim_phase -= 2.0f * M_PI;
    }

    xQueueSend(q_iq_samples, sim_iq_buf, 0);
}

// ─────────────────────────────────────────────
// AUDIO INIT
// ─────────────────────────────────────────────
void audio_init(void) {
    ESP_LOGI(TAG, "Iniciando audio...");

    q_iq_samples = xQueueCreate(2, sizeof(float) * FFT_SIZE * 2);

    // I2C para NAU8822
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port      = I2C_NUM_0,
        .sda_io_num    = PIN_I2C_SDA,
        .scl_io_num    = PIN_I2C_SCL,
        .clk_source    = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    i2c_new_master_bus(&i2c_cfg, &i2c_bus);

    i2c_device_config_t nau_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = NAU8822_ADDR,
        .scl_speed_hz    = 100000,
    };
    i2c_master_bus_add_device(i2c_bus, &nau_cfg, &i2c_nau);

#if !SIMULATE_AUDIO
    nau8822_init();
    i2s_init();
#endif

    ESP_LOGI(TAG, "Audio OK");
}

// ─────────────────────────────────────────────
// AUDIO PROCESS - llamado desde task_audio
// ─────────────────────────────────────────────
void audio_process(void) {
#if SIMULATE_AUDIO
    generate_sim_iq();
    vTaskDelay(pdMS_TO_TICKS(10));
#else
    // Lee muestras IQ del NAU8822 por I2S
    int16_t raw[FFT_SIZE * 2];
    size_t bytes_read = 0;
    i2s_channel_read(i2s_rx_chan, raw, sizeof(raw), &bytes_read, portMAX_DELAY);

    // Convierte int16 → float normalizado
    static float iq_buf[FFT_SIZE * 2];
    int samples = bytes_read / 2;
    for (int i = 0; i < samples; i++) {
        iq_buf[i] = raw[i] / 32768.0f;
    }

    xQueueSend(q_iq_samples, iq_buf, 0);
#endif
}
