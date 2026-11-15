// ============================================================
// display_driver.h
// ============================================================
// Camada de abstração do display.
// A configuração do controlador (driver e pinos) fica nos
// build_flags do platformio.ini. O ui_renderer só usa a API abaixo.
// ============================================================

#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

// ---- Dimensões do display ----
// ST7735 1.8" 128x160, usado em paisagem (rotação 1) = 160x128
#define DISPLAY_WIDTH  160
#define DISPLAY_HEIGHT 128

// Rotação: 1 = paisagem, 3 = paisagem invertida (pinos do outro lado)
#define DISPLAY_ROTATION 1

// ---- Cores (RGB565) ----
#define COLOR_BG             TFT_BLACK
#define COLOR_TEXT_PRIMARY   TFT_WHITE
#define COLOR_TEXT_SECONDARY 0xBDF7  // Cinza claro
#define COLOR_TEXT_MUTED     0x7BEF  // Cinza médio
#define COLOR_PROGRESS_BG    0x2104  // Cinza escuro
#define COLOR_PROGRESS_FG    0x1DCA  // Verde Spotify (#1DB954)
#define COLOR_SPOTIFY_GREEN  0x1DCA
#define COLOR_COVER_EMPTY    0x18E3  // Fundo da capa enquanto carrega

// ---- API pública ----

/// Inicializa o display (SPI, backlight, rotação, TJpg_Decoder)
void display_init();

/// Retorna ponteiro para a instância TFT (para uso pelo ui_renderer)
TFT_eSPI* display_get_tft();

/// Liga/desliga o backlight
void display_backlight(bool on);

/// Indica se o backlight está ligado
bool display_backlight_is_on();

/// Limpa a tela inteira com a cor de fundo padrão
void display_clear();

/// Callback interno do TJpg_Decoder para renderizar blocos JPEG no display.
/// Registrado automaticamente em display_init(). Não chamar diretamente.
bool display_jpeg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

#endif // DISPLAY_DRIVER_H
