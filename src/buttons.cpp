// ============================================================
// buttons.cpp
// ============================================================
// Os pedidos HTTPS ao Spotify travam o loop por alguns instantes.
// Para não perder um clique nesse meio-tempo, cada botão usa uma
// interrupção que só marca "foi apertado"; o loop lê depois.
// ============================================================

#include "buttons.h"

struct Button {
    uint8_t                pin;
    ButtonEvent            event;
    volatile bool          pending;
    volatile unsigned long last_irq;
};

static Button s_buttons[] = {
    {PIN_BTN_PREV, BUTTON_PREV, false, 0},
    {PIN_BTN_PLAY, BUTTON_PLAY, false, 0},
    {PIN_BTN_NEXT, BUTTON_NEXT, false, 0},
};

static void IRAM_ATTR on_press(void* arg) {
    Button* b = static_cast<Button*>(arg);
    unsigned long now = millis();
    if (now - b->last_irq < BUTTON_DEBOUNCE_MS * 5) return;  // ignora o "repique" do contato
    b->last_irq = now;
    b->pending = true;
}

void buttons_init() {
    for (Button& b : s_buttons) {
        pinMode(b.pin, INPUT_PULLUP);
        attachInterruptArg(digitalPinToInterrupt(b.pin), on_press, &b, FALLING);
    }
    Serial.printf("[BTN] Botoes: anterior=GPIO%d play=GPIO%d proxima=GPIO%d\r\n",
                  PIN_BTN_PREV, PIN_BTN_PLAY, PIN_BTN_NEXT);
}

ButtonEvent buttons_poll() {
    for (Button& b : s_buttons) {
        if (b.pending) {
            b.pending = false;
            return b.event;
        }
    }
    return BUTTON_NONE;
}
