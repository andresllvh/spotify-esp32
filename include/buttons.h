// ============================================================
// buttons.h
// ============================================================
// Leitura dos 3 botões táteis com debounce.
// Cada botão liga o GPIO ao GND; o pull-up interno mantém o pino
// em HIGH quando solto. Pinos definidos no platformio.ini.
// ============================================================

#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

#ifndef PIN_BTN_PREV
#define PIN_BTN_PREV 0
#endif
#ifndef PIN_BTN_PLAY
#define PIN_BTN_PLAY 1
#endif
#ifndef PIN_BTN_NEXT
#define PIN_BTN_NEXT 20
#endif

#define BUTTON_DEBOUNCE_MS 30

enum ButtonEvent {
    BUTTON_NONE,
    BUTTON_PREV,
    BUTTON_PLAY,
    BUTTON_NEXT
};

/// Configura os pinos dos botões
void buttons_init();

/// Retorna o botão que acabou de ser apertado (um evento por aperto)
ButtonEvent buttons_poll();

#endif // BUTTONS_H
