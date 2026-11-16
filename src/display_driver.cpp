// ============================================================
// display_driver.cpp
// ============================================================
// Implementação da camada de display (ST7735 160x128).
// ============================================================

#include "display_driver.h"

// Instância global do TFT_eSPI (configurado via build_flags)
static TFT_eSPI tft = TFT_eSPI();
static bool s_backlight_on = false;

void display_init() {
    tft.init();
    tft.setRotation(DISPLAY_ROTATION);
#ifdef WOKWI
    // Simulador: a ILI9341 tem 320x240. Desenha uma moldura e centraliza
    // a área de 160x128, do tamanho da tela real.
    tft.fillScreen(0x2104);
    tft.drawRect((tft.width() - DISPLAY_WIDTH) / 2 - 1, (tft.height() - DISPLAY_HEIGHT) / 2 - 1,
                 DISPLAY_WIDTH + 2, DISPLAY_HEIGHT + 2, 0x7BEF);
    tft.setViewport((tft.width() - DISPLAY_WIDTH) / 2, (tft.height() - DISPLAY_HEIGHT) / 2,
                    DISPLAY_WIDTH, DISPLAY_HEIGHT);
#endif
    tft.fillScreen(COLOR_BG);

    // Liga a luz de fundo só depois de limpar a tela (evita "lixo" na tela)
    #if defined(TFT_BL) && (TFT_BL >= 0)
        pinMode(TFT_BL, OUTPUT);
    #endif
    display_backlight(true);

    // TJpg_Decoder: escala ajustada por imagem em ui_renderer_draw_cover()
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(display_jpeg_output);

    #ifdef WOKWI
    Serial.println("[DISPLAY] Inicializado (simulador ILI9341, area 160x128)");
#else
    Serial.println("[DISPLAY] Inicializado (ST7735 160x128)");
#endif
}

TFT_eSPI* display_get_tft() {
    return &tft;
}

void display_backlight(bool on) {
    #if defined(TFT_BL) && (TFT_BL >= 0)
        digitalWrite(TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
    #endif
    s_backlight_on = on;
}

bool display_backlight_is_on() {
    return s_backlight_on;
}

void display_clear() {
    tft.fillScreen(COLOR_BG);
}

// Callback do TJpg_Decoder: cada bloco decodificado vai direto para a tela,
// sem framebuffer na RAM (o C3 não tem PSRAM).
bool display_jpeg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= DISPLAY_HEIGHT) return false;
    tft.pushImage(x, y, w, h, bitmap);
    return true;
}
