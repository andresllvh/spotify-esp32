// ============================================================
// spotify_poller.h
// ============================================================
// Módulo responsável por fazer polling da API do Spotify
// (endpoint /v1/me/player/currently-playing) a cada intervalo
// e baixar a imagem da capa do álbum quando o track muda.
// ============================================================

#ifndef SPOTIFY_POLLER_H
#define SPOTIFY_POLLER_H

#include <Arduino.h>

// Intervalo de polling em milissegundos (3-5 segundos)
#define POLL_INTERVAL_MS 2000

// URL da API de currently-playing
#define SPOTIFY_NOW_PLAYING_URL "https://api.spotify.com/v1/me/player/currently-playing"

// Tamanho máximo do buffer para download da capa JPEG.
// A capa de 64x64 tem 2 a 5 KB; o limite cobre a de 300x300 (~25 KB)
// caso o álbum não tenha a versão pequena.
#define COVER_BUFFER_MAX_SIZE 40000

// ---- Códigos de resultado do polling ----
enum PollResult {
    POLL_OK_PLAYING,       // Sucesso, música tocando
    POLL_OK_PAUSED,        // Sucesso, música pausada
    POLL_NO_CONTENT,       // 204 - nada tocando / player inativo
    POLL_TOKEN_EXPIRED,    // 401 - token expirado (renovar)
    POLL_RATE_LIMITED,     // 429 - rate limit atingido
    POLL_ERROR_NETWORK,    // Erro de conexão/rede
    POLL_ERROR_PARSE,      // Erro ao parsear resposta
    POLL_ERROR_OTHER       // Outro erro HTTP
};

/// Inicializa o módulo do poller
void spotify_poller_init();

/// Executa um poll da API do Spotify.
/// Retorna o código de resultado.
PollResult spotify_poller_poll();

/// Retorna o tempo (millis) que devemos esperar antes do próximo poll.
/// Respeita Retry-After do header 429.
unsigned long spotify_poller_get_next_interval();

/// Muda o intervalo até o próximo poll (usado após apertar um botão)
void spotify_poller_set_next_interval(unsigned long interval_ms);

/// Verifica se houve mudança de track desde a última consulta.
/// Resetado para false após ser lido.
bool spotify_poller_track_changed();

/// Baixa a capa do álbum atual da URL fornecida.
/// Armazena num buffer alocado na heap e retorna o tamanho.
/// Retorna 0 se falhar.
uint32_t spotify_poller_download_cover(const String& url, uint8_t** out_buffer);

/// Libera o buffer da capa após renderização
void spotify_poller_free_cover(uint8_t* buffer);

#endif // SPOTIFY_POLLER_H
