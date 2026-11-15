// ============================================================
// json_parser.h
// ============================================================
// Parsing da resposta da API do Spotify com filtro do ArduinoJson,
// guardando só os campos que a tela usa.
// ============================================================

#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <Arduino.h>

// Tamanho de capa preferido. O Spotify fornece 640, 300 e 64 px.
// Na tela de 1.8" a capa ocupa 64x64, então a de 64 px é a ideal
// (poucos KB, cabe folgado na RAM do ESP32-C3).
#define COVER_TARGET_PX 64

struct TrackInfo {
    String   track_name;
    String   artist_name;   // artistas separados por ", "
    String   track_id;      // muda quando a música muda
    String   cover_url;     // capa mais próxima de COVER_TARGET_PX
    uint32_t progress_ms;
    uint32_t duration_ms;
    bool     is_playing;
};

/// Parseia direto do stream HTTP (não guarda a resposta inteira na RAM).
bool json_parser_parse_stream(Stream& stream);

/// Parseia a partir de uma String já carregada.
bool json_parser_parse(const String& json_payload);

/// Dados do último parse bem-sucedido
TrackInfo* json_parser_get_track();

/// Limpa os dados (usado quando não há nada tocando)
void json_parser_reset();

#endif // JSON_PARSER_H
