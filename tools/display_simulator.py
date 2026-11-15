#!/usr/bin/env python3
# ============================================================
# display_simulator.py
# ============================================================
# Simulador da tela do chaveiro (TFT 1.8" 160x128) no computador.
# Usa o mesmo layout do firmware (include/ui_renderer.h).
#
# Requisitos:
#   pip install requests Pillow python-dotenv
#
# Uso:
#   python display_simulator.py --demo   # sem Spotify, música de exemplo
#   python display_simulator.py          # lê o Spotify de verdade
#
# Para o modo real, crie tools/.env com:
#   SPOTIFY_CLIENT_ID=...
#   SPOTIFY_CLIENT_SECRET=...
#   SPOTIFY_REFRESH_TOKEN=...
#
# Teclas: espaço = play/pause, ← → = anterior/próxima (só no demo),
#         R = consultar agora, S = salvar print, ESC = sair
# ============================================================

import base64
import io
import os
import random
import sys
import threading
import time
import tkinter as tk
import unicodedata

try:
    from PIL import Image, ImageDraw, ImageFont, ImageTk
except ImportError:
    print("ERRO: instale o Pillow: pip install Pillow")
    sys.exit(1)

try:
    import requests
except ImportError:
    requests = None

try:
    from dotenv import load_dotenv
    load_dotenv(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env"))
except ImportError:
    pass

DEMO = "--demo" in sys.argv

CLIENT_ID = os.environ.get("SPOTIFY_CLIENT_ID", "")
CLIENT_SECRET = os.environ.get("SPOTIFY_CLIENT_SECRET", "")
REFRESH_TOKEN = os.environ.get("SPOTIFY_REFRESH_TOKEN", "")

SPOTIFY_TOKEN_URL = "https://accounts.spotify.com/api/token"
SPOTIFY_NOW_PLAYING_URL = "https://api.spotify.com/v1/me/player/currently-playing"

# ============================================================
# Layout (igual a include/ui_renderer.h)
# ============================================================
W, H = 160, 128
SCALE = 4

COVER_SIZE, COVER_X, COVER_Y = 64, 4, 16
TEXT_X, TEXT_W = 74, W - 74 - 4
TITLE_Y, ARTIST_Y = 17, 35
EQ_X, EQ_Y, EQ_H, EQ_BARS, EQ_BAR_W, EQ_GAP = TEXT_X, 50, 30, 9, 6, 3
BAR_X, BAR_Y, BAR_W, BAR_H = 4, 88, W - 8, 3
TIME_Y = 94
CONTROLS_Y = 114

BG = "#000000"
WHITE = "#FFFFFF"
SECONDARY = "#BDBDBD"
MUTED = "#7B7B7B"
BAR_BG = "#202020"
GREEN = "#1DB954"
COVER_EMPTY = "#181C18"

MARQUEE_SPEED = 1
MARQUEE_PAUSE = 30
MARQUEE_GAP = 24
POLL_INTERVAL = 4.0

DEMO_TRACKS = [
    {"track_name": "Starboy", "artist_name": "The Weeknd, Daft Punk",
     "duration_ms": 230_000, "color": (180, 40, 60)},
    {"track_name": "Garota de Ipanema", "artist_name": "Tom Jobim, Vinicius de Moraes",
     "duration_ms": 195_000, "color": (40, 110, 160)},
    {"track_name": "Evidências", "artist_name": "Chitãozinho & Xororó",
     "duration_ms": 280_000, "color": (150, 110, 40)},
]


def load_font(px):
    candidates = [
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "arial.ttf",
        "DejaVuSans.ttf",
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(path, px * SCALE)
        except (OSError, IOError):
            continue
    try:
        return ImageFont.load_default(size=px * SCALE)
    except TypeError:
        return ImageFont.load_default()


FONT_TITLE = load_font(13)   # aproxima a fonte 2 do TFT_eSPI (16 px de altura)
FONT_SMALL = load_font(8)    # aproxima a fonte 1 (GLCD 6x8)
FONT_BIG = load_font(20)     # aproxima a fonte 4


def to_display_text(text):
    """Mesma regra do firmware: tira acentos e descarta o que não é ASCII."""
    nfkd = unicodedata.normalize("NFKD", text)
    return "".join(c for c in nfkd if ord(c) < 128).strip()


def fmt(ms):
    s = max(0, int(ms)) // 1000
    return f"{s // 60}:{s % 60:02d}"


# ============================================================
# Estado
# ============================================================
class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.track = None
        self.cover = None
        self.sync_time = 0.0
        self.sync_ms = 0
        self.title_off = 0
        self.title_pause = MARQUEE_PAUSE
        self.artist_off = 0
        self.artist_pause = MARQUEE_PAUSE
        self.eq = [2] * EQ_BARS
        self.eq_target = [2] * EQ_BARS
        self.demo_index = 0
        self.access_token = ""
        self.token_expiry = 0.0
        self.last_id = ""

    def set_track(self, track, cover):
        with self.lock:
            if track is None or track.get("track_id") != self.last_id:
                self.title_off = self.artist_off = 0
                self.title_pause = self.artist_pause = MARQUEE_PAUSE
                self.cover = cover
            self.last_id = track.get("track_id") if track else ""
            self.track = track
            if track:
                self.sync_time = time.time()
                self.sync_ms = track["progress_ms"]

    def progress(self):
        t = self.track
        if not t:
            return 0
        if t["is_playing"]:
            return min(self.sync_ms + (time.time() - self.sync_time) * 1000, t["duration_ms"])
        return self.sync_ms


state = State()


# ============================================================
# Spotify (modo real)
# ============================================================
def refresh_access_token():
    creds = base64.b64encode(f"{CLIENT_ID}:{CLIENT_SECRET}".encode()).decode()
    try:
        r = requests.post(SPOTIFY_TOKEN_URL, timeout=10,
                          headers={"Authorization": f"Basic {creds}",
                                   "Content-Type": "application/x-www-form-urlencoded"},
                          data={"grant_type": "refresh_token", "refresh_token": REFRESH_TOKEN})
    except requests.RequestException as e:
        print(f"[AUTH] Erro: {e}")
        return False
    if r.status_code != 200:
        print(f"[AUTH] Erro {r.status_code}: {r.text[:200]}")
        return False
    body = r.json()
    state.access_token = body["access_token"]
    state.token_expiry = time.time() + body.get("expires_in", 3600)
    return True


def poll_spotify():
    if not state.access_token or time.time() >= state.token_expiry - 60:
        if not refresh_access_token():
            return
    try:
        r = requests.get(SPOTIFY_NOW_PLAYING_URL, timeout=10,
                         headers={"Authorization": f"Bearer {state.access_token}"})
    except requests.RequestException as e:
        print(f"[POLLER] Erro: {e}")
        return

    if r.status_code == 204:
        state.set_track(None, None)
        return
    if r.status_code == 401:
        refresh_access_token()
        return
    if r.status_code != 200:
        print(f"[POLLER] HTTP {r.status_code}")
        return

    data = r.json()
    item = data.get("item")
    if not item:
        state.set_track(None, None)
        return

    images = item.get("album", {}).get("images", [])
    best = min(images, key=lambda i: abs((i.get("height") or 0) - COVER_SIZE), default=None)
    track = {
        "track_id": item.get("id", ""),
        "track_name": item.get("name", ""),
        "artist_name": ", ".join(a["name"] for a in item.get("artists", [])),
        "duration_ms": item.get("duration_ms", 0),
        "progress_ms": data.get("progress_ms", 0),
        "is_playing": data.get("is_playing", False),
    }

    cover = state.cover
    if track["track_id"] != state.last_id and best:
        try:
            img = Image.open(io.BytesIO(requests.get(best["url"], timeout=10).content)).convert("RGB")
            cover = img.resize((COVER_SIZE, COVER_SIZE), Image.LANCZOS)
            print(f"[POLLER] {track['artist_name']} - {track['track_name']}")
        except Exception as e:
            print(f"[COVER] Erro: {e}")
            cover = None
    state.set_track(track, cover)


# ============================================================
# Demo
# ============================================================
def demo_cover(rgb):
    img = Image.new("RGB", (COVER_SIZE, COVER_SIZE), rgb)
    d = ImageDraw.Draw(img)
    for i in range(0, COVER_SIZE, 8):
        shade = tuple(min(255, c + i) for c in rgb)
        d.ellipse([i // 2, i // 2, COVER_SIZE - i // 2, COVER_SIZE - i // 2], outline=shade)
    return img


def load_demo(index, playing=True):
    t = DEMO_TRACKS[index % len(DEMO_TRACKS)]
    track = {
        "track_id": f"demo-{index}",
        "track_name": t["track_name"],
        "artist_name": t["artist_name"],
        "duration_ms": t["duration_ms"],
        "progress_ms": 34_000,
        "is_playing": playing,
    }
    state.set_track(track, demo_cover(t["color"]))


# ============================================================
# Desenho (coordenadas em pixels da tela; S multiplica)
# ============================================================
S = SCALE


def R(x0, y0, x1, y1):
    return [x0 * S, y0 * S, x1 * S - 1, y1 * S - 1]


def draw_marquee(frame, text, font, color, y, h, offset):
    d = ImageDraw.Draw(frame)
    width = d.textlength(text, font=font) / S
    strip = Image.new("RGB", (TEXT_W * S, h * S), BG)
    sd = ImageDraw.Draw(strip)
    sd.text((-offset * S, 0), text, fill=color, font=font)
    if width > TEXT_W:
        sd.text(((-offset + width + MARQUEE_GAP) * S, 0), text, fill=color, font=font)
    frame.paste(strip, (TEXT_X * S, y * S))
    return width


def step_marquee(off, pause, width):
    if width <= TEXT_W:
        return 0, pause
    if pause > 0:
        return off, pause - 1
    off += MARQUEE_SPEED
    if off >= width + MARQUEE_GAP:
        return 0, MARQUEE_PAUSE
    return off, pause


def draw_controls(d, playing):
    cy = CONTROLS_Y
    c = SECONDARY
    # Anterior
    d.rectangle(R(40 - 7, cy - 5, 40 - 5, cy + 6), fill=c)
    d.polygon([((40 - 5) * S, cy * S), ((40 + 1) * S, (cy - 5) * S), ((40 + 1) * S, (cy + 5) * S)], fill=c)
    d.polygon([((40 + 1) * S, cy * S), ((40 + 7) * S, (cy - 5) * S), ((40 + 7) * S, (cy + 5) * S)], fill=c)
    # Play / pause
    d.ellipse(R(80 - 9, cy - 9, 80 + 10, cy + 10), fill=GREEN)
    if playing:
        d.rectangle(R(80 - 4, cy - 4, 80 - 1, cy + 5), fill=BG)
        d.rectangle(R(80 + 1, cy - 4, 80 + 4, cy + 5), fill=BG)
    else:
        d.polygon([((80 - 3) * S, (cy - 5) * S), ((80 - 3) * S, (cy + 5) * S), ((80 + 5) * S, cy * S)], fill=BG)
    # Próxima
    d.polygon([((120 - 7) * S, (cy - 5) * S), ((120 - 7) * S, (cy + 5) * S), ((120 - 1) * S, cy * S)], fill=c)
    d.polygon([((120 - 1) * S, (cy - 5) * S), ((120 - 1) * S, (cy + 5) * S), ((120 + 5) * S, cy * S)], fill=c)
    d.rectangle(R(120 + 5, cy - 5, 120 + 7, cy + 6), fill=c)


def render_frame():
    frame = Image.new("RGB", (W * S, H * S), BG)
    d = ImageDraw.Draw(frame)

    with state.lock:
        track = dict(state.track) if state.track else None
        cover = state.cover

    if track is None:
        d.text((W * S // 2, 36 * S), "Spotify", fill=GREEN, font=FONT_BIG, anchor="mm")
        d.text((W * S // 2, 70 * S), "Nada tocando", fill=SECONDARY, font=FONT_SMALL, anchor="mm")
        d.text((W * S // 2, 88 * S), "Toque algo no Spotify", fill=MUTED, font=FONT_SMALL, anchor="mm")
        return frame

    playing = track["is_playing"]

    # Cabeçalho
    d.text((4 * S, 3 * S), "NOW PLAYING" if playing else "PAUSED", fill=GREEN, font=FONT_SMALL)
    d.ellipse(R(W - 10, 3, W - 3, 10), fill=GREEN if playing else MUTED)

    # Capa
    if cover:
        frame.paste(cover.resize((COVER_SIZE * S, COVER_SIZE * S), Image.NEAREST), (COVER_X * S, COVER_Y * S))
    else:
        d.rounded_rectangle(R(COVER_X, COVER_Y, COVER_X + COVER_SIZE, COVER_Y + COVER_SIZE), radius=4 * S, fill=COVER_EMPTY)

    # Título e artista com marquee
    title = to_display_text(track["track_name"])
    artist = to_display_text(track["artist_name"])
    tw = draw_marquee(frame, title, FONT_TITLE, WHITE, TITLE_Y, 16, state.title_off)
    aw = draw_marquee(frame, artist, FONT_SMALL, SECONDARY, ARTIST_Y, 10, state.artist_off)
    state.title_off, state.title_pause = step_marquee(state.title_off, state.title_pause, tw)
    state.artist_off, state.artist_pause = step_marquee(state.artist_off, state.artist_pause, aw)

    # Equalizador (animação, igual ao firmware)
    for i in range(EQ_BARS):
        if playing:
            if state.eq[i] == state.eq_target[i] or random.randint(0, 3) == 0:
                boost = 6 - abs(i - EQ_BARS // 2)
                state.eq_target[i] = max(3, min(EQ_H, random.randint(4, EQ_H - 5) + boost))
        else:
            state.eq_target[i] = 2
        h, t = state.eq[i], state.eq_target[i]
        h = min(t, h + 6) if h < t else max(t, h - 3) if h > t else h
        state.eq[i] = h
        x = EQ_X + i * (EQ_BAR_W + EQ_GAP)
        d.rectangle(R(x, EQ_Y + EQ_H - h, x + EQ_BAR_W, EQ_Y + EQ_H), fill=GREEN if playing else BAR_BG)

    # Progresso
    prog = state.progress()
    dur = track["duration_ms"] or 1
    filled = int(BAR_W * min(1.0, prog / dur))
    d.rectangle(R(BAR_X, BAR_Y, BAR_X + BAR_W, BAR_Y + BAR_H), fill=BAR_BG)
    if filled > 0:
        d.rectangle(R(BAR_X, BAR_Y, BAR_X + filled, BAR_Y + BAR_H), fill=GREEN)
    d.ellipse(R(BAR_X + filled - 3, BAR_Y - 2, BAR_X + filled + 4, BAR_Y + 5), fill=WHITE)
    d.text((BAR_X * S, TIME_Y * S), fmt(prog), fill=MUTED, font=FONT_SMALL)
    d.text(((BAR_X + BAR_W) * S, TIME_Y * S), fmt(dur), fill=MUTED, font=FONT_SMALL, anchor="ra")

    draw_controls(d, playing)
    return frame


# ============================================================
# Janela
# ============================================================
class Simulator:
    def __init__(self, root):
        self.root = root
        root.title("Spotify chaveiro - simulador 160x128" + (" (demo)" if DEMO else ""))
        root.resizable(False, False)
        root.configure(bg="#2B2B2B")

        # Moldura vermelha imitando a placa do TFT
        board = tk.Frame(root, bg="#B8352A", padx=14, pady=14)
        board.pack(padx=16, pady=16)
        self.canvas = tk.Canvas(board, width=W * S, height=H * S, bg="black", highlightthickness=0)
        self.canvas.pack()

        hint = "Espaço play/pause | ← → faixas | S print | ESC sair" if DEMO else \
               "R consultar agora | S print | ESC sair | consulta a cada 4 s"
        tk.Label(root, text=hint, bg="#2B2B2B", fg="#9A9A9A").pack(pady=(0, 10))

        root.bind("<Escape>", lambda e: root.destroy())
        root.bind("<space>", lambda e: self.toggle_play())
        root.bind("<Left>", lambda e: self.change_track(-1))
        root.bind("<Right>", lambda e: self.change_track(1))
        root.bind("r", lambda e: self.poll_async())
        root.bind("s", lambda e: self.save_screenshot())

        self.tk_image = None
        self.last_poll = 0.0
        self.frame = None
        self.update()

    def toggle_play(self):
        if not DEMO or not state.track:
            return
        t = dict(state.track)
        t["progress_ms"] = int(state.progress())
        t["is_playing"] = not t["is_playing"]
        state.set_track(t, state.cover)

    def change_track(self, step):
        if not DEMO:
            return
        state.demo_index = (state.demo_index + step) % len(DEMO_TRACKS)
        load_demo(state.demo_index)

    def poll_async(self):
        if DEMO:
            return
        self.last_poll = time.time()
        threading.Thread(target=poll_spotify, daemon=True).start()

    def save_screenshot(self):
        if self.frame is not None:
            path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "simulador.png")
            self.frame.save(path)
            print(f"[SIM] Print salvo em {path}")

    def update(self):
        if not DEMO and time.time() - self.last_poll >= POLL_INTERVAL:
            self.poll_async()
        self.frame = render_frame()
        self.tk_image = ImageTk.PhotoImage(self.frame)
        self.canvas.create_image(0, 0, anchor=tk.NW, image=self.tk_image)
        self.root.after(80, self.update)  # mesmo ritmo do marquee no firmware


def main():
    if DEMO:
        load_demo(0)
    else:
        if requests is None:
            print("ERRO: instale o requests: pip install requests")
            sys.exit(1)
        if not (CLIENT_ID and CLIENT_SECRET and REFRESH_TOKEN):
            print("Faltam credenciais em tools/.env (ou rode com --demo para ver o layout).")
            sys.exit(1)
        if not refresh_access_token():
            print("Falha na autenticação. Confira as credenciais.")
            sys.exit(1)

    root = tk.Tk()
    Simulator(root)
    root.mainloop()


if __name__ == "__main__":
    main()
