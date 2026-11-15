// ============================================================
// buttons.cpp
// ============================================================

#include "buttons.h"

struct Button {
    uint8_t       pin;
    ButtonEvent   event;
    bool          stable_pressed;
    bool          last_raw;
    unsigned long last_change;
};

static Button s_buttons[] = {
    {PIN_BTN_PREV, BUTTON_PREV, false, false, 0},
    {PIN_BTN_PLAY, BUTTON_PLAY, false, false, 0},
    {PIN_BTN_NEXT, BUTTON_NEXT, false, false, 0},
};

void buttons_init() {
    for (Button& b : s_buttons) {
        pinMode(b.pin, INPUT_PULLUP);
        b.last_raw = (digitalRead(b.pin) == LOW);
        b.stable_pressed = b.last_raw;
        b.last_change = millis();
    }
    Serial.printf("[BTN] Botoes: anterior=GPIO%d play=GPIO%d proxima=GPIO%d\n",
                  PIN_BTN_PREV, PIN_BTN_PLAY, PIN_BTN_NEXT);
}

ButtonEvent buttons_poll() {
    unsigned long now = millis();
    for (Button& b : s_buttons) {
        bool raw = (digitalRead(b.pin) == LOW);
        if (raw != b.last_raw) {
            b.last_raw = raw;
            b.last_change = now;
        }
        if (now - b.last_change >= BUTTON_DEBOUNCE_MS && raw != b.stable_pressed) {
            b.stable_pressed = raw;
            if (raw) return b.event;  // dispara ao apertar
        }
    }
    return BUTTON_NONE;
}
