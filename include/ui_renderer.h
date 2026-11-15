// ============================================================
// ui_renderer.h
// ============================================================
// Renderização da interface no TFT 160x128 (paisagem).
//
// Layout:
// ┌──────────────────────────────────┐
// │ NOW PLAYING                    ● │  cabeçalho
// │ ┌────────┐  Título (marquee)     │
// │ │  CAPA  │  Artista (marquee)    │
// │ │ 64x64  │  ▌▐ ▌▐▌ ▐▌ ▌          │  equalizador animado
// │ └────────┘                       │
// │ ▓▓▓▓▓▓▓▓▓●░░░░░░░░░░░░░░░░░░░░░ │  progresso
// │ 1:34                        3:50 │
// │        |◀◀      ▮▮      ▶▶|      │  botões físicos
// └──────────────────────────────────┘
// ============================================================

#ifndef UI_RENDERER_H
#define UI_RENDERER_H

#include <Arduino.h>
#include "display_driver.h"
#include "json_parser.h"

// ---- Layout ----
#define HEADER_Y        3

#define COVER_SIZE      64
#define COVER_X         4
#define COVER_Y         16

#define TEXT_X_MARGIN   74                       // capa + 6 px
#define TEXT_AREA_W     (DISPLAY_WIDTH - TEXT_X_MARGIN - 4)
#define TEXT_Y_TITLE    17
#define TEXT_H_TITLE    16                       // fonte 2
#define TEXT_Y_ARTIST   35
#define TEXT_H_ARTIST   10                       // fonte 1

#define EQ_X            TEXT_X_MARGIN
#define EQ_Y            50
#define EQ_H            30
#define EQ_BARS         9
#define EQ_BAR_W        6
#define EQ_GAP          3

#define PROGRESS_BAR_X      4
#define PROGRESS_BAR_Y      88
#define PROGRESS_BAR_W      (DISPLAY_WIDTH - 8)
#define PROGRESS_BAR_HEIGHT 3
#define TIME_Y              94

#define CONTROLS_Y      114                      // centro dos ícones de controle

// ---- Marquee ----
#define MARQUEE_SPEED        1    // pixels por frame
#define MARQUEE_PAUSE_FRAMES 30   // pausa no início de cada volta

// ---- Botões (para destacar o ícone ao apertar) ----
enum UiControl { UI_CTRL_PREV, UI_CTRL_PLAY, UI_CTRL_NEXT };

/// Inicializa o módulo de UI
void ui_renderer_init();

/// Desenha cabeçalho, textos, controles e progresso do track
void ui_renderer_draw_track(TrackInfo* track);

/// Atualiza só a barra de progresso e os tempos
void ui_renderer_update_progress(uint32_t progress_ms, uint32_t duration_ms, bool is_playing);

/// Atualiza só o ícone de play/pause (e o ponto do cabeçalho)
void ui_renderer_update_play_state(bool is_playing);

/// Desenha a capa a partir de um JPEG em memória
void ui_renderer_draw_cover(uint8_t* jpeg_data, uint32_t jpeg_size);

/// Desenha um quadrado no lugar da capa (enquanto baixa ou se falhar)
void ui_renderer_draw_cover_placeholder();

/// Avança o scroll do título e do artista (chamar a cada ~80 ms)
void ui_renderer_update_marquee();

/// Anima o equalizador (chamar a cada ~120 ms). Parado = barras baixas.
void ui_renderer_update_equalizer(bool is_playing);

/// Cuida dos efeitos temporários (destaque de botão, avisos). Chamar no loop.
void ui_renderer_tick();

/// Destaca por um instante o ícone do botão apertado
void ui_renderer_flash_control(UiControl control);

/// Mostra uma mensagem curta na linha dos controles (ex.: "Precisa Premium")
void ui_renderer_show_hint(const char* message);

/// Tela "Nada tocando"
void ui_renderer_draw_idle();

/// Tela de erro
void ui_renderer_draw_error(const char* message);

/// Tela de "Conectando..."
void ui_renderer_draw_connecting();

/// Tela pedindo as credenciais via Serial
void ui_renderer_draw_need_credentials();

/// Força redesenho completo dos textos na próxima chamada
void ui_renderer_reset_cache();

#endif // UI_RENDERER_H
