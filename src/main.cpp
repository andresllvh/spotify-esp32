// ============================================================
// main.cpp
// ============================================================
// Spotify Now Playing para chaveiro (ESP32-C3 + TFT ST7735).
//
// Estados:
//   BOOT        -> inicializa display, botões e LED
//   WIFI_SETUP  -> conecta via WiFiManager (portal cativo na 1a vez)
//   AUTH_INIT   -> carrega credenciais e obtém o primeiro token
//   POLLING     -> consulta a API, desenha a tela e lê os botões
//   ERROR       -> mostra o erro e tenta se recuperar
//
// Entre as consultas, a barra de progresso avança localmente com
// millis(), sem gastar requisição HTTP.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>

#include "display_driver.h"
#include "wifi_manager_setup.h"
#include "spotify_auth.h"
#include "spotify_poller.h"
#include "spotify_control.h"
#include "json_parser.h"
#include "ui_renderer.h"
#include "buttons.h"

// ---- LED azul da placa (indica que está tocando) ----
#ifndef PIN_STATUS_LED
#define PIN_STATUS_LED 8
#endif

static void led_init() {
    pinMode(PIN_STATUS_LED, OUTPUT);
}

static void led_set(bool on) {
    #if defined(STATUS_LED_ACTIVE_LOW) && STATUS_LED_ACTIVE_LOW
        digitalWrite(PIN_STATUS_LED, on ? LOW : HIGH);
    #else
        digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
    #endif
}

// ============================================================
// Estado
// ============================================================
enum AppState {
    STATE_BOOT,
    STATE_WIFI_SETUP,
    STATE_AUTH_INIT,
    STATE_POLLING,
    STATE_ERROR
};

static AppState s_state = STATE_BOOT;

static unsigned long s_last_poll_time = 0;
static unsigned long s_last_marquee_time = 0;
static unsigned long s_last_progress_time = 0;
static unsigned long s_last_eq_time = 0;
static unsigned long s_progress_sync_millis = 0;
static unsigned long s_last_activity = 0;   // último momento tocando ou botão apertado

static uint32_t s_local_progress_ms = 0;
static uint32_t s_duration_ms = 0;
static bool s_is_playing = false;
static bool s_has_track = false;

static int s_error_count = 0;
static const int MAX_ERRORS_BEFORE_RECONNECT = 5;

#define MARQUEE_UPDATE_MS    80
#define PROGRESS_UPDATE_MS   1000
#define EQ_UPDATE_MS         120

// Apaga a tela depois de 2 min sem nada tocando (economiza bateria).
// Qualquer botão ou música nova acende de novo.
#define SCREEN_OFF_AFTER_MS  120000UL

// ============================================================
// Comandos via Serial (USB)
// ============================================================
static void check_serial_commands() {
    if (!Serial.available()) return;

    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("SET_CREDS:")) {
        input = input.substring(10);
        int sep1 = input.indexOf(':');
        int sep2 = input.indexOf(':', sep1 + 1);

        if (sep1 > 0 && sep2 > sep1) {
            spotify_auth_save_credentials(
                input.substring(0, sep1).c_str(),
                input.substring(sep1 + 1, sep2).c_str(),
                input.substring(sep2 + 1).c_str());
            Serial.println("[MAIN] Credenciais salvas! Reiniciando...");
            delay(1000);
            ESP.restart();
        } else {
            Serial.println("[MAIN] Formato invalido. Use: SET_CREDS:client_id:client_secret:refresh_token");
        }
    } else if (input == "RESET_WIFI") {
        wifi_reset_credentials();
        Serial.println("[MAIN] Wi-Fi apagado. Reiniciando...");
        delay(1000);
        ESP.restart();
    } else if (input == "STATUS") {
        Serial.printf("[MAIN] Estado: %d\r\n", s_state);
        Serial.printf("[MAIN] Wi-Fi: %s (RSSI %d dBm)\r\n",
                      wifi_is_connected() ? "OK" : "Desconectado", WiFi.RSSI());
        Serial.printf("[MAIN] Token: %s\r\n", spotify_auth_token_expired() ? "Expirado" : "Valido");
        Serial.printf("[MAIN] Heap livre: %u bytes (minimo %u)\r\n",
                      ESP.getFreeHeap(), ESP.getMinFreeHeap());
    }
}

// ============================================================
// Tela ligada / desligada
// ============================================================
static void mark_activity() {
    s_last_activity = millis();
    if (!display_backlight_is_on()) {
        display_backlight(true);
    }
}

static void update_screen_timeout() {
    bool idle = !s_has_track || !s_is_playing;
    if (idle && display_backlight_is_on() && millis() - s_last_activity > SCREEN_OFF_AFTER_MS) {
        Serial.println("[MAIN] Sem musica ha um tempo, apagando a tela");
        display_backlight(false);
    }
}

// ============================================================
// Botões
// ============================================================
static void show_control_result(ControlResult r) {
    switch (r) {
        case CONTROL_OK:           break;
        case CONTROL_NEED_PREMIUM: ui_renderer_show_hint("Precisa Spotify Premium"); break;
        case CONTROL_NO_DEVICE:    ui_renderer_show_hint("Abra o Spotify no celular"); break;
        case CONTROL_ERROR:        ui_renderer_show_hint("Falha no comando"); break;
    }
}

static void handle_buttons() {
    ButtonEvent ev = buttons_poll();
    if (ev == BUTTON_NONE) return;
    Serial.printf("[BTN] %s\r\n", ev == BUTTON_PLAY ? "play/pause" : ev == BUTTON_NEXT ? "proxima" : "anterior");

    // Com a tela apagada, o primeiro aperto só acende
    bool was_off = !display_backlight_is_on();
    mark_activity();
    if (was_off) return;

    ControlResult r = CONTROL_OK;
    switch (ev) {
        case BUTTON_PLAY: {
            if (s_has_track) ui_renderer_flash_control(UI_CTRL_PLAY);
            bool want_play = !s_is_playing;
            r = want_play ? spotify_control_play() : spotify_control_pause();
            if (r == CONTROL_OK && s_has_track) {
                // Atualiza a tela na hora; o próximo poll confirma
                s_local_progress_ms += millis() - s_progress_sync_millis;
                if (s_local_progress_ms > s_duration_ms) s_local_progress_ms = s_duration_ms;
                s_progress_sync_millis = millis();
                s_is_playing = want_play;
                led_set(want_play);
                ui_renderer_update_play_state(want_play);
            }
            spotify_poller_set_next_interval(800);
            break;
        }
        case BUTTON_NEXT:
            if (s_has_track) ui_renderer_flash_control(UI_CTRL_NEXT);
            r = spotify_control_next();
            spotify_poller_set_next_interval(700);
            break;
        case BUTTON_PREV:
            if (s_has_track) ui_renderer_flash_control(UI_CTRL_PREV);
            r = spotify_control_previous();
            spotify_poller_set_next_interval(700);
            break;
        default:
            break;
    }
    show_control_result(r);
    s_last_poll_time = millis();
}

// ============================================================
// Estados
// ============================================================
static void handle_state_boot() {
    Serial.println("\n====================================");
    Serial.println("  Spotify Now Playing - chaveiro");
    Serial.println("====================================");
    Serial.printf("Heap livre: %u bytes\n\r\n", ESP.getFreeHeap());

    led_init();
    led_set(false);
    buttons_init();

    display_init();
    ui_renderer_init();
    ui_renderer_draw_connecting();

    spotify_poller_init();
    s_last_activity = millis();
    s_state = STATE_WIFI_SETUP;
}

static void handle_state_wifi_setup() {
    if (wifi_setup_init()) {
        s_state = STATE_AUTH_INIT;
    } else {
        ui_renderer_draw_error("Wi-Fi desconectado");
        delay(5000);
        ESP.restart();  // reabre o portal
    }
}

static void handle_state_auth_init() {
    if (!spotify_auth_init()) {
        Serial.println("[MAIN] Sem credenciais! Envie pela USB:");
        Serial.println("[MAIN] SET_CREDS:client_id:client_secret:refresh_token");
        ui_renderer_draw_need_credentials();
        while (!spotify_auth_has_credentials()) {
            check_serial_commands();
            delay(100);
        }
    }

    Serial.println("[MAIN] Obtendo access_token...");
    if (spotify_auth_refresh()) {
        s_state = STATE_POLLING;
        s_last_poll_time = 0;
        s_error_count = 0;
        ui_renderer_draw_idle();
    } else {
        ui_renderer_draw_error("Login do Spotify falhou");
        delay(5000);
        s_state = STATE_ERROR;
    }
}

static void on_track_data(TrackInfo* track, bool track_changed) {
    bool was_idle = !s_has_track;
    s_has_track = true;

    if (track->is_playing) mark_activity();

    s_is_playing = track->is_playing;
    s_duration_ms = track->duration_ms;
    s_local_progress_ms = track->progress_ms;
    s_progress_sync_millis = millis();
    led_set(track->is_playing);

    if (track_changed || was_idle) {
        // Texto primeiro (aparece na hora), capa depois (precisa baixar)
        display_clear();
        ui_renderer_reset_cache();
        ui_renderer_draw_cover_placeholder();
        ui_renderer_draw_track(track);

        uint8_t* cover = nullptr;
        uint32_t size = spotify_poller_download_cover(track->cover_url, &cover);
        if (size > 0 && cover) {
            ui_renderer_draw_cover(cover, size);
            spotify_poller_free_cover(cover);
        }
    } else {
        ui_renderer_draw_track(track);
    }
}

static void handle_state_polling() {
    unsigned long now = millis();

    if (!wifi_is_connected()) {
        led_set(false);
        ui_renderer_draw_error("Wi-Fi perdido");
        if (!wifi_reconnect()) {
            if (++s_error_count > MAX_ERRORS_BEFORE_RECONNECT) ESP.restart();
            delay(2000);
            return;
        }
        s_error_count = 0;
        s_has_track = false;
        s_last_poll_time = 0;
    }

    handle_buttons();

    // ---- Consulta à API ----
    if (now - s_last_poll_time >= spotify_poller_get_next_interval()) {
        s_last_poll_time = now;

        switch (spotify_poller_poll()) {
            case POLL_OK_PLAYING:
            case POLL_OK_PAUSED:
                s_error_count = 0;
                on_track_data(json_parser_get_track(), spotify_poller_track_changed());
                break;

            case POLL_NO_CONTENT:
                led_set(false);
                if (s_has_track) {
                    ui_renderer_draw_idle();
                    s_has_track = false;
                    s_is_playing = false;
                }
                s_error_count = 0;
                break;

            case POLL_TOKEN_EXPIRED:
                if (!spotify_auth_refresh()) {
                    s_error_count++;
                    ui_renderer_draw_error("Token expirado");
                }
                break;

            case POLL_RATE_LIMITED:
                break;

            case POLL_ERROR_NETWORK:
                if (++s_error_count > MAX_ERRORS_BEFORE_RECONNECT) {
                    ui_renderer_draw_error("Rede instavel");
                    s_has_track = false;
                }
                break;

            case POLL_ERROR_PARSE:
            case POLL_ERROR_OTHER:
                if (++s_error_count > MAX_ERRORS_BEFORE_RECONNECT * 2) {
                    s_state = STATE_ERROR;
                }
                break;
        }
    }

    if (s_has_track) {
        // ---- Progresso interpolado ----
        if (s_is_playing && now - s_last_progress_time >= PROGRESS_UPDATE_MS) {
            s_last_progress_time = now;
            uint32_t p = s_local_progress_ms + (now - s_progress_sync_millis);
            if (p > s_duration_ms) p = s_duration_ms;
            ui_renderer_update_progress(p, s_duration_ms, true);
        }

        if (now - s_last_marquee_time >= MARQUEE_UPDATE_MS) {
            s_last_marquee_time = now;
            ui_renderer_update_marquee();
        }

        if (now - s_last_eq_time >= EQ_UPDATE_MS) {
            s_last_eq_time = now;
            ui_renderer_update_equalizer(s_is_playing);
        }
    }

    ui_renderer_tick();
    update_screen_timeout();
    check_serial_commands();
}

static void handle_state_error() {
    led_set(false);
    ui_renderer_draw_error("Erro persistente");
    delay(10000);

    if ((wifi_is_connected() || wifi_reconnect()) && spotify_auth_refresh()) {
        s_error_count = 0;
        s_has_track = false;
        s_state = STATE_POLLING;
        s_last_poll_time = 0;
        return;
    }
    ESP.restart();
}

// ============================================================
// Arduino
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);  // dá tempo da USB CDC aparecer no computador
    s_state = STATE_BOOT;
}

void loop() {
    switch (s_state) {
        case STATE_BOOT:       handle_state_boot();       break;
        case STATE_WIFI_SETUP: handle_state_wifi_setup(); break;
        case STATE_AUTH_INIT:  handle_state_auth_init();  break;
        case STATE_POLLING:    handle_state_polling();    break;
        case STATE_ERROR:      handle_state_error();      break;
    }
    delay(2);  // cede tempo ao Wi-Fi e economiza um pouco de energia
}
