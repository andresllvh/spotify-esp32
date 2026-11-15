// ============================================================
// spotify_control.h
// ============================================================
// Comandos de reprodução pela Web API do Spotify.
// Exigem conta Premium e o escopo user-modify-playback-state
// no refresh_token (veja tools/get_refresh_token.py).
// ============================================================

#ifndef SPOTIFY_CONTROL_H
#define SPOTIFY_CONTROL_H

#include <Arduino.h>

enum ControlResult {
    CONTROL_OK,
    CONTROL_NEED_PREMIUM,   // 403: conta sem Premium ou escopo ausente
    CONTROL_NO_DEVICE,      // 404: nenhum aparelho tocando Spotify
    CONTROL_ERROR           // rede ou outro erro HTTP
};

ControlResult spotify_control_play();
ControlResult spotify_control_pause();
ControlResult spotify_control_next();
ControlResult spotify_control_previous();

#endif // SPOTIFY_CONTROL_H
