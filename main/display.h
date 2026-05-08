#pragma once

#include <stdint.h>

// Colores RGB565
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_GRAY      0x8410
#define COLOR_DARKGRAY  0x4208
#define COLOR_ORANGE    0xFD20

// Colores UI SDR
#define COLOR_BG        0x0000   // Fondo negro
#define COLOR_TOPBAR    0x0010   // Azul muy oscuro
#define COLOR_BOTBAR    0x0010
#define COLOR_SPECTRUM  0x07E0   // Verde
#define COLOR_WATERFALL_HOT  0xF800  // Rojo = señal fuerte
#define COLOR_WATERFALL_MID  0xFFE0  // Amarillo = señal media
#define COLOR_WATERFALL_COLD 0x001F  // Azul = sin señal
#define COLOR_GRID      0x2104   // Gris oscuro
#define COLOR_TEXT      0xFFFF   // Blanco
#define COLOR_FREQ      0x07FF   // Cyan para frecuencia

void display_init(void);
void display_update(void);
void display_draw_spectrum(float *magnitudes, int count);
void display_draw_waterfall(float *magnitudes, int count);
void display_draw_top_bar(void);
void display_draw_bot_bar(void);
