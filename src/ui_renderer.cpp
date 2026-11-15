// ============================================================
// ui_renderer.cpp
// ============================================================
// Interface do mini player no TFT 160x128.
// Textos longos rolam (marquee), o equalizador é uma animação
// (a música toca no celular, não na placa) e a barra de progresso
// é interpolada localmente entre as consultas à API.
// ============================================================

#include "ui_renderer.h"
#include "display_driver.h"
#include <TJpg_Decoder.h>

static TFT_eSPI* tft = nullptr;

// ---- Marquee ----
struct MarqueeState {
    String  text;
    int16_t text_width;
    int16_t offset;
    int16_t pause_counter;
    bool    scrolling;
};

static MarqueeState s_title;
static MarqueeState s_artist;

#define MARQUEE_GAP 24  // espaço entre o fim e o recomeço do texto

// ---- Estado ----
static String   s_last_title  = "";
static String   s_last_artist = "";
static bool     s_is_playing  = false;
static uint8_t  s_eq_height[EQ_BARS];
static uint8_t  s_eq_target[EQ_BARS];

static int            s_flash_control = -1;
static unsigned long  s_flash_until   = 0;
static unsigned long  s_hint_until    = 0;

// ============================================================
// Auxiliares
// ============================================================

static String formatTime(uint32_t ms) {
    uint32_t totalSec = ms / 1000;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u:%02u", totalSec / 60, totalSec % 60);
    return String(buf);
}

// As fontes do TFT_eSPI só têm ASCII. Troca letras acentuadas
// (UTF-8 de 2 bytes começando em 0xC3) pela letra sem acento e
// descarta o resto (emojis, outros alfabetos).
static String toDisplayText(const String& in) {
    static const char* MAP_C3 =
        // 0x80..0xBF depois do byte 0xC3
        "AAAAAAACEEEEIIII"   // 80-8F  À Á Â Ã Ä Å Æ Ç È É Ê Ë Ì Í Î Ï
        "DNOOOOOxOUUUUYTs"   // 90-9F  Ð Ñ Ò Ó Ô Õ Ö × Ø Ù Ú Û Ü Ý Þ ß
        "aaaaaaaceeeeiiii"   // A0-AF
        "dnooooo/ouuuuyty";  // B0-BF
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
        uint8_t c = (uint8_t)in[i];
        if (c < 0x80) {
            out += (char)c;
        } else if (c == 0xC3 && i + 1 < in.length()) {
            uint8_t n = (uint8_t)in[i + 1];
            if (n >= 0x80 && n <= 0xBF) out += MAP_C3[n - 0x80];
            i++;
        } else if (c >= 0xC0) {
            // Pula os bytes de continuação de um caractere multibyte
            int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
            i += extra;
        }
    }
    out.trim();
    return out;
}

static void initMarquee(MarqueeState& m, const String& text, uint8_t font) {
    m.text = text;
    m.offset = 0;
    m.pause_counter = MARQUEE_PAUSE_FRAMES;
    tft->setTextFont(font);
    m.text_width = tft->textWidth(text);
    m.scrolling = (m.text_width > TEXT_AREA_W);
}

static void drawMarquee(const MarqueeState& m, int16_t y, int16_t h, uint8_t font, uint16_t color) {
    TFT_eSprite spr = TFT_eSprite(tft);
    if (!spr.createSprite(TEXT_AREA_W, h)) return;
    spr.fillSprite(COLOR_BG);
    spr.setTextFont(font);
    spr.setTextColor(color, COLOR_BG);
    spr.setTextDatum(TL_DATUM);
    spr.drawString(m.text, -m.offset, 0);
    if (m.scrolling) {
        // Segunda cópia logo atrás para o loop ficar contínuo
        spr.drawString(m.text, -m.offset + m.text_width + MARQUEE_GAP, 0);
    }
    spr.pushSprite(TEXT_X_MARGIN, y);
    spr.deleteSprite();
}

static void stepMarquee(MarqueeState& m) {
    if (!m.scrolling) return;
    if (m.pause_counter > 0) {
        m.pause_counter--;
        return;
    }
    m.offset += MARQUEE_SPEED;
    if (m.offset >= m.text_width + MARQUEE_GAP) {
        m.offset = 0;
        m.pause_counter = MARQUEE_PAUSE_FRAMES;
    }
}

// ---- Ícones dos controles ----

static void drawPrevIcon(int cx, int cy, uint16_t c) {
    tft->fillRect(cx - 7, cy - 5, 2, 11, c);
    tft->fillTriangle(cx - 5, cy, cx + 1, cy - 5, cx + 1, cy + 5, c);
    tft->fillTriangle(cx + 1, cy, cx + 7, cy - 5, cx + 7, cy + 5, c);
}

static void drawNextIcon(int cx, int cy, uint16_t c) {
    tft->fillTriangle(cx - 7, cy - 5, cx - 7, cy + 5, cx - 1, cy, c);
    tft->fillTriangle(cx - 1, cy - 5, cx - 1, cy + 5, cx + 5, cy, c);
    tft->fillRect(cx + 5, cy - 5, 2, 11, c);
}

static void drawPlayPauseIcon(int cx, int cy, bool playing, uint16_t c) {
    // Círculo verde com o símbolo da ação disponível
    tft->fillCircle(cx, cy, 9, c);
    if (playing) {
        tft->fillRect(cx - 4, cy - 4, 3, 9, COLOR_BG);
        tft->fillRect(cx + 1, cy - 4, 3, 9, COLOR_BG);
    } else {
        tft->fillTriangle(cx - 3, cy - 5, cx - 3, cy + 5, cx + 5, cy, COLOR_BG);
    }
}

static void drawControls() {
    tft->fillRect(0, CONTROLS_Y - 11, DISPLAY_WIDTH, 23, COLOR_BG);
    uint16_t base = COLOR_TEXT_SECONDARY;
    drawPrevIcon(40, CONTROLS_Y, s_flash_control == UI_CTRL_PREV ? COLOR_SPOTIFY_GREEN : base);
    drawPlayPauseIcon(80, CONTROLS_Y, s_is_playing,
                      s_flash_control == UI_CTRL_PLAY ? TFT_WHITE : COLOR_SPOTIFY_GREEN);
    drawNextIcon(120, CONTROLS_Y, s_flash_control == UI_CTRL_NEXT ? COLOR_SPOTIFY_GREEN : base);
}

static void drawHeader() {
    tft->fillRect(0, 0, DISPLAY_WIDTH, 13, COLOR_BG);
    tft->setTextFont(1);
    tft->setTextColor(COLOR_SPOTIFY_GREEN, COLOR_BG);
    tft->setTextDatum(TL_DATUM);
    tft->drawString(s_is_playing ? "NOW PLAYING" : "PAUSED", 4, HEADER_Y);
    tft->fillCircle(DISPLAY_WIDTH - 7, HEADER_Y + 3, 3,
                    s_is_playing ? COLOR_SPOTIFY_GREEN : COLOR_TEXT_MUTED);
}

static void drawCenteredScreen(const char* l1, uint8_t f1, uint16_t c1,
                               const char* l2, const char* l3, const char* l4) {
    display_clear();
    tft->setTextDatum(MC_DATUM);
    tft->setTextFont(f1);
    tft->setTextColor(c1, COLOR_BG);
    tft->drawString(l1, DISPLAY_WIDTH / 2, 36);

    tft->setTextFont(1);
    tft->setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
    if (l2) tft->drawString(l2, DISPLAY_WIDTH / 2, 70);
    tft->setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    if (l3) tft->drawString(l3, DISPLAY_WIDTH / 2, 88);
    if (l4) tft->drawString(l4, DISPLAY_WIDTH / 2, 102);
    tft->setTextDatum(TL_DATUM);
}

// ============================================================
// API
// ============================================================

void ui_renderer_init() {
    tft = display_get_tft();
    tft->setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
    tft->setTextDatum(TL_DATUM);
    display_clear();
    for (int i = 0; i < EQ_BARS; i++) { s_eq_height[i] = 2; s_eq_target[i] = 2; }
    randomSeed(esp_random());
    Serial.println("[UI] Inicializado");
}

void ui_renderer_draw_track(TrackInfo* track) {
    if (!track || !tft) return;

    s_is_playing = track->is_playing;
    drawHeader();

    String title = toDisplayText(track->track_name);
    if (title != s_last_title) {
        s_last_title = title;
        initMarquee(s_title, title, 2);
        drawMarquee(s_title, TEXT_Y_TITLE, TEXT_H_TITLE, 2, COLOR_TEXT_PRIMARY);
    }

    String artist = toDisplayText(track->artist_name);
    if (artist != s_last_artist) {
        s_last_artist = artist;
        initMarquee(s_artist, artist, 1);
        // Título e artista recomeçam juntos
        s_artist.pause_counter = s_title.pause_counter;
        drawMarquee(s_artist, TEXT_Y_ARTIST, TEXT_H_ARTIST, 1, COLOR_TEXT_SECONDARY);
    }

    if (millis() > s_hint_until) drawControls();
    ui_renderer_update_progress(track->progress_ms, track->duration_ms, track->is_playing);
}

void ui_renderer_update_play_state(bool is_playing) {
    if (!tft) return;
    s_is_playing = is_playing;
    drawHeader();
    if (millis() > s_hint_until) drawControls();
}

void ui_renderer_update_progress(uint32_t progress_ms, uint32_t duration_ms, bool is_playing) {
    if (!tft || duration_ms == 0) return;

    float ratio = (float)progress_ms / (float)duration_ms;
    ratio = constrain(ratio, 0.0f, 1.0f);
    int filled_w = (int)(PROGRESS_BAR_W * ratio);

    // Limpa a faixa inteira (inclui a bolinha) e redesenha
    tft->fillRect(0, PROGRESS_BAR_Y - 3, DISPLAY_WIDTH, PROGRESS_BAR_HEIGHT + 6, COLOR_BG);
    tft->fillRect(PROGRESS_BAR_X, PROGRESS_BAR_Y, PROGRESS_BAR_W, PROGRESS_BAR_HEIGHT, COLOR_PROGRESS_BG);
    if (filled_w > 0) {
        tft->fillRect(PROGRESS_BAR_X, PROGRESS_BAR_Y, filled_w, PROGRESS_BAR_HEIGHT, COLOR_PROGRESS_FG);
    }
    tft->fillCircle(PROGRESS_BAR_X + filled_w, PROGRESS_BAR_Y + 1, 3, COLOR_TEXT_PRIMARY);

    tft->setTextFont(1);
    tft->setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    tft->fillRect(0, TIME_Y, DISPLAY_WIDTH, 8, COLOR_BG);
    tft->setTextDatum(TL_DATUM);
    tft->drawString(formatTime(progress_ms), PROGRESS_BAR_X, TIME_Y);
    tft->setTextDatum(TR_DATUM);
    tft->drawString(formatTime(duration_ms), PROGRESS_BAR_X + PROGRESS_BAR_W, TIME_Y);
    tft->setTextDatum(TL_DATUM);
}

void ui_renderer_draw_cover_placeholder() {
    if (!tft) return;
    tft->fillRoundRect(COVER_X, COVER_Y, COVER_SIZE, COVER_SIZE, 4, COLOR_COVER_EMPTY);
    // Nota musical simples
    int cx = COVER_X + COVER_SIZE / 2, cy = COVER_Y + COVER_SIZE / 2;
    tft->fillCircle(cx - 6, cy + 10, 6, COLOR_TEXT_MUTED);
    tft->fillRect(cx - 1, cy - 14, 3, 25, COLOR_TEXT_MUTED);
    tft->fillTriangle(cx + 2, cy - 14, cx + 12, cy - 8, cx + 2, cy - 4, COLOR_TEXT_MUTED);
}

void ui_renderer_draw_cover(uint8_t* jpeg_data, uint32_t jpeg_size) {
    if (!tft || !jpeg_data || jpeg_size == 0) return;

    uint16_t img_w = 0, img_h = 0;
    if (TJpgDec.getJpgSize(&img_w, &img_h, jpeg_data, jpeg_size) != JDR_OK || img_w == 0) {
        Serial.println("[UI] JPEG da capa inválido");
        ui_renderer_draw_cover_placeholder();
        return;
    }

    // Menor escala (1, 2, 4 ou 8) que faz a imagem caber no quadrado da capa
    uint8_t scale = 1;
    while (scale < 8 && (img_w / scale > COVER_SIZE || img_h / scale > COVER_SIZE)) {
        scale *= 2;
    }
    uint16_t w = img_w / scale, h = img_h / scale;
    Serial.printf("[UI] Capa %ux%u, escala 1/%u -> %ux%u\n", img_w, img_h, scale, w, h);

    if (w < COVER_SIZE || h < COVER_SIZE) {
        tft->fillRect(COVER_X, COVER_Y, COVER_SIZE, COVER_SIZE, COLOR_BG);
    }

    TJpgDec.setJpgScale(scale);
    TJpgDec.drawJpg(COVER_X + (COVER_SIZE - w) / 2, COVER_Y + (COVER_SIZE - h) / 2,
                    jpeg_data, jpeg_size);
    TJpgDec.setJpgScale(1);
}

void ui_renderer_update_marquee() {
    if (!tft) return;
    if (s_title.scrolling) {
        stepMarquee(s_title);
        drawMarquee(s_title, TEXT_Y_TITLE, TEXT_H_TITLE, 2, COLOR_TEXT_PRIMARY);
    }
    if (s_artist.scrolling) {
        stepMarquee(s_artist);
        drawMarquee(s_artist, TEXT_Y_ARTIST, TEXT_H_ARTIST, 1, COLOR_TEXT_SECONDARY);
    }
}

void ui_renderer_update_equalizer(bool is_playing) {
    if (!tft) return;
    for (int i = 0; i < EQ_BARS; i++) {
        if (is_playing) {
            // Alvo novo de vez em quando; barras do meio um pouco mais altas
            if (s_eq_height[i] == s_eq_target[i] || random(4) == 0) {
                int center_boost = 6 - abs(i - EQ_BARS / 2);
                s_eq_target[i] = constrain((int)random(4, EQ_H - 4) + center_boost, 3, EQ_H);
            }
        } else {
            s_eq_target[i] = 2;
        }

        // Aproxima a altura do alvo (sobe rápido, desce devagar)
        int h = s_eq_height[i];
        int t = s_eq_target[i];
        if (h < t)      h = min(t, h + 6);
        else if (h > t) h = max(t, h - 3);
        s_eq_height[i] = h;

        int x = EQ_X + i * (EQ_BAR_W + EQ_GAP);
        tft->fillRect(x, EQ_Y, EQ_BAR_W, EQ_H - h, COLOR_BG);
        tft->fillRect(x, EQ_Y + EQ_H - h, EQ_BAR_W, h,
                      is_playing ? COLOR_SPOTIFY_GREEN : COLOR_PROGRESS_BG);
    }
}

void ui_renderer_tick() {
    unsigned long now = millis();
    if (s_flash_control >= 0 && now > s_flash_until) {
        s_flash_control = -1;
        if (now > s_hint_until) drawControls();
    }
    if (s_hint_until != 0 && now > s_hint_until) {
        s_hint_until = 0;
        drawControls();
    }
}

void ui_renderer_flash_control(UiControl control) {
    if (!tft) return;
    s_flash_control = control;
    s_flash_until = millis() + 180;
    if (millis() > s_hint_until) drawControls();
}

void ui_renderer_show_hint(const char* message) {
    if (!tft) return;
    tft->fillRect(0, CONTROLS_Y - 11, DISPLAY_WIDTH, 23, COLOR_BG);
    tft->setTextFont(1);
    tft->setTextColor(TFT_ORANGE, COLOR_BG);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(message, DISPLAY_WIDTH / 2, CONTROLS_Y);
    tft->setTextDatum(TL_DATUM);
    s_hint_until = millis() + 2500;
}

void ui_renderer_draw_idle() {
    if (!tft) return;
    drawCenteredScreen("Spotify", 4, COLOR_SPOTIFY_GREEN,
                       "Nada tocando", "Toque algo no Spotify", nullptr);
    ui_renderer_reset_cache();
}

void ui_renderer_draw_error(const char* message) {
    if (!tft) return;
    drawCenteredScreen("Erro", 2, TFT_RED, message, "Tentando de novo...", nullptr);
    ui_renderer_reset_cache();
}

void ui_renderer_draw_connecting() {
    if (!tft) return;
    drawCenteredScreen("Spotify Display", 2, COLOR_SPOTIFY_GREEN,
                       "Conectando ao Wi-Fi...", "Se nao conectar, use",
                       "SpotifyDisplay-Setup");
    ui_renderer_reset_cache();
}

void ui_renderer_draw_need_credentials() {
    if (!tft) return;
    drawCenteredScreen("Falta login", 2, TFT_ORANGE,
                       "Envie pela USB:", "SET_CREDS:id:secret:", "refresh_token");
    ui_renderer_reset_cache();
}

void ui_renderer_reset_cache() {
    s_last_title = "";
    s_last_artist = "";
    s_title.scrolling = false;
    s_artist.scrolling = false;
}
