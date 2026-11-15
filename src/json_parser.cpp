// ============================================================
// json_parser.cpp
// ============================================================
// A resposta de /me/player/currently-playing pode passar de 10 KB
// (lista de mercados, etc.). O filtro faz o ArduinoJson guardar só
// os campos abaixo, e o parse por stream evita carregar tudo na RAM.
// ============================================================

#include "json_parser.h"
#include <ArduinoJson.h>

static TrackInfo s_track;

static void buildFilter(JsonDocument& filter) {
    filter["is_playing"] = true;
    filter["progress_ms"] = true;

    JsonObject item = filter["item"].to<JsonObject>();
    item["id"] = true;
    item["name"] = true;
    item["duration_ms"] = true;
    item["artists"][0]["name"] = true;

    JsonObject album = item["album"].to<JsonObject>();
    album["images"][0]["url"] = true;
    album["images"][0]["height"] = true;
}

static bool extract(JsonDocument& doc) {
    s_track.is_playing = doc["is_playing"] | false;
    s_track.progress_ms = doc["progress_ms"] | 0;

    JsonObject item = doc["item"];
    if (item.isNull()) {
        // Acontece com anúncios e alguns podcasts
        Serial.println("[PARSER] Campo 'item' ausente no JSON");
        return false;
    }

    s_track.track_id = item["id"].as<String>();
    s_track.track_name = item["name"].as<String>();
    s_track.duration_ms = item["duration_ms"] | 0;

    s_track.artist_name = "";
    JsonArray artists = item["artists"];
    for (size_t i = 0; i < artists.size(); i++) {
        if (i > 0) s_track.artist_name += ", ";
        s_track.artist_name += artists[i]["name"].as<String>();
    }

    // Capa com altura mais próxima de COVER_TARGET_PX
    JsonArray images = item["album"]["images"];
    String bestUrl = "";
    int bestDiff = INT32_MAX;
    for (JsonObject img : images) {
        int diff = abs((int)(img["height"] | 0) - COVER_TARGET_PX);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestUrl = img["url"].as<String>();
        }
    }
    s_track.cover_url = bestUrl;

    Serial.printf("[PARSER] %s - %s | %u/%u ms\n",
                  s_track.artist_name.c_str(), s_track.track_name.c_str(),
                  s_track.progress_ms, s_track.duration_ms);
    return true;
}

bool json_parser_parse_stream(Stream& stream) {
    JsonDocument filter;
    buildFilter(filter);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    if (error) {
        Serial.printf("[PARSER] Erro ArduinoJson: %s\n", error.c_str());
        return false;
    }
    return extract(doc);
}

bool json_parser_parse(const String& json_payload) {
    JsonDocument filter;
    buildFilter(filter);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json_payload, DeserializationOption::Filter(filter));
    if (error) {
        Serial.printf("[PARSER] Erro ArduinoJson: %s\n", error.c_str());
        return false;
    }
    return extract(doc);
}

TrackInfo* json_parser_get_track() {
    return &s_track;
}

void json_parser_reset() {
    s_track.track_name = "";
    s_track.artist_name = "";
    s_track.track_id = "";
    s_track.cover_url = "";
    s_track.progress_ms = 0;
    s_track.duration_ms = 0;
    s_track.is_playing = false;
}
