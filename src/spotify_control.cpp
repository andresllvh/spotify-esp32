// ============================================================
// spotify_control.cpp
// ============================================================

#include "spotify_control.h"
#include "spotify_auth.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#define PLAYER_URL "https://api.spotify.com/v1/me/player/"

static int sendCommand(const char* method, const char* path) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, String(PLAYER_URL) + path);
    http.addHeader("Authorization", "Bearer " + spotify_auth_get_access_token());

    // Corpo vazio; o HTTPClient envia Content-Length: 0, que a API exige
    int code = (strcmp(method, "PUT") == 0) ? http.PUT("") : http.POST("");

    if (code >= 400) {
        Serial.printf("[CONTROL] %s %s -> HTTP %d %s\n", method, path, code, http.getString().c_str());
    } else {
        Serial.printf("[CONTROL] %s %s -> HTTP %d\n", method, path, code);
    }
    http.end();
    return code;
}

static ControlResult run(const char* method, const char* path) {
    if (!spotify_auth_ensure_valid_token()) return CONTROL_ERROR;

    int code = sendCommand(method, path);
    if (code == 401 && spotify_auth_refresh()) {
        code = sendCommand(method, path);
    }

    if (code == 200 || code == 202 || code == 204) return CONTROL_OK;
    if (code == 403) return CONTROL_NEED_PREMIUM;
    if (code == 404) return CONTROL_NO_DEVICE;
    return CONTROL_ERROR;
}

ControlResult spotify_control_play()     { return run("PUT",  "play"); }
ControlResult spotify_control_pause()    { return run("PUT",  "pause"); }
ControlResult spotify_control_next()     { return run("POST", "next"); }
ControlResult spotify_control_previous() { return run("POST", "previous"); }
