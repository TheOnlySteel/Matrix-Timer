#!/usr/bin/env python3
"""Render design mockups for the Matrix-Timer hourglass.

This file doubles as the reference implementation of the rendering model in
docs/design.md: geometry, grain order tables, the remaining-time -> pixels
mapping, the falling-grain animation and the LED-bar gauge. Firmware should
port these functions rather than reinvent them, so the images and the device
agree.

Usage: python3 tools/mockups/render.py   (writes into docs/images/)
Requires Pillow.
"""
from __future__ import annotations

import math
import os
from dataclasses import dataclass

from PIL import Image, ImageDraw, ImageFilter, ImageFont

W = H = 16
OUT = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "images")

# ---------------------------------------------------------------------------
# Geometry. Row 0 is the top of the matrix *after* gravity rotation.
#
# Top chamber: rows 0..6, row r spans cols r..15-r   (16,14,...,4 = 70 cells)
# Neck:        rows 7..8, cols 7..8                  (stream channel only)
# Bottom:      rows 9..15, row r spans cols 15-r..r   (4,6,...,16 = 70 cells)
# ---------------------------------------------------------------------------
TOP = [(r, c) for r in range(0, 7) for c in range(r, 16 - r)]
BOTTOM = [(r, c) for r in range(9, 16) for c in range(15 - r, r + 1)]
GRAINS = len(TOP)
assert GRAINS == len(BOTTOM) == 70

CENTER = 7.5


def _top_drain_order():
    """Order in which top-chamber cells go dark: the surface falls row by row
    with a shallow dip at the centre (sand funnels toward the neck)."""
    return sorted(TOP, key=lambda rc: (rc[0] + 0.25 * abs(rc[1] - CENTER), rc[1]))


def _bottom_fill_order():
    """Order in which bottom-chamber cells light: a mound that grows from the
    centre of the floor."""
    return sorted(BOTTOM, key=lambda rc: ((15 - rc[0]) + 0.5 * abs(rc[1] - CENTER), rc[1]))


TOP_DRAIN = _top_drain_order()
BOTTOM_FILL = _bottom_fill_order()

# Level face: all 256 pixels, draining row by row from the top, left to right.
LEVEL_DRAIN = [(r, c) for r in range(16) for c in range(16)]


def grains_left(remaining_s: float, total_s: float, n: int = GRAINS) -> int:
    """ceil() so a lit grain means time remains; 0 only at exactly zero."""
    if remaining_s <= 0:
        return 0
    return min(n, math.ceil(n * remaining_s / total_s))


def hourglass(remaining_s: float, total_s: float, t_anim_s: float | None = None):
    """Return a 16x16 bool frame for the hourglass face.

    t_anim_s drives the decorative falling grain; None means stream stopped
    (paused / done / setup)."""
    f = [[False] * W for _ in range(H)]
    left = grains_left(remaining_s, total_s)
    for r, c in TOP_DRAIN[GRAINS - left:]:
        f[r][c] = True
    for r, c in BOTTOM_FILL[: GRAINS - left]:
        f[r][c] = True
    if t_anim_s is not None and left > 0:
        _falling_grain(f, t_anim_s)
    return f


FALL_ROW_MS = 70  # one row per 70 ms; a grain falls in ~0.5 s


def _falling_grain(f, t_s: float):
    """One pixel travels from the neck to the pile surface, alternating
    columns 7/8 each trip. Purely an 'it is running' cue."""
    step = int(t_s * 1000 // FALL_ROW_MS)
    # landing row: first lit cell going down from the neck in that column
    col = 7 + ((step // 9) % 2)
    land = 16
    for r in range(9, 16):
        if f[r][col]:
            land = r
            break
    path = list(range(7, land))  # rows the grain visits
    if not path:
        return
    pos = step % (len(path) + 2)  # +2 frames of rest at the neck = rhythm
    if pos < len(path):
        f[path[pos]][col] = True


def level(remaining_s: float, total_s: float):
    f = [[True] * W for _ in range(H)]
    left = grains_left(remaining_s, total_s, 256)
    for r, c in LEVEL_DRAIN[: 256 - left]:
        f[r][c] = False
    return f


# ---------------------------------------------------------------------------
# Tiny font: 3x5 digits, drawn at 2x for minutes (6x10 glyphs) and at
# 1x wide / 2x tall for H:MM so every duration fits 16 px.
# ---------------------------------------------------------------------------
FONT3x5 = {
    "0": ["111", "101", "101", "101", "111"],
    "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"],
    "3": ["111", "001", "111", "001", "111"],
    "4": ["101", "101", "111", "001", "001"],
    "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"],
    "7": ["111", "001", "010", "010", "010"],
    "8": ["111", "101", "111", "101", "111"],
    "9": ["111", "101", "111", "001", "111"],
    ":": ["0", "1", "0", "1", "0"],
}


def text_frame(s: str, sx: int, sy: int):
    f = [[False] * W for _ in range(H)]
    glyphs = [FONT3x5[ch] for ch in s]
    widths = [len(g[0]) * sx for g in glyphs]
    total = sum(widths) + (len(glyphs) - 1)
    x = (W - total) // 2
    y = (H - 5 * sy) // 2
    for g, w in zip(glyphs, widths):
        for gy, row in enumerate(g):
            for gx, bit in enumerate(row):
                if bit == "1":
                    for dy in range(sy):
                        for dx in range(sx):
                            f[y + gy * sy + dy][x + gx * sx + dx] = True
        x += w + 1
    return f


def duration_frame(minutes: int):
    if minutes < 100:
        return text_frame(f"{minutes:d}", 2, 2)
    h, m = divmod(minutes, 60)
    return text_frame(f"{h}:{m:02d}", 1, 2)


def invert(f):
    return [[not v for v in row] for row in f]


# ---------------------------------------------------------------------------
# LED bars (M5GO Bottom2, 10 x SK6812). Logical model: two 5-LED columns,
# index 0 = bottom. Physical chain order must be mapped at build time.
# ---------------------------------------------------------------------------
TEAL = (0, 190, 170)
AMBER = (255, 140, 0)
RED = (255, 30, 10)
GREEN = (40, 220, 60)
OFF = (0, 0, 0)


def gauge(fraction: float, color, cap: float = 1.0):
    """5-LED analogue gauge; the topmost lit LED carries the remainder as
    brightness, so the bar moves continuously."""
    out = []
    x = max(0.0, min(1.0, fraction)) * 5
    for i in range(5):
        level_ = max(0.0, min(1.0, x - i))
        if i == 0 and 0 < x < 0.15:
            level_ = 0.15  # floor: never let the last LED fade to invisible
        out.append(tuple(int(ch * level_ * cap) for ch in color))
    return out


def run_color(fraction: float):
    if fraction <= 0.05:
        return RED
    if fraction <= 0.20:
        return AMBER
    return TEAL


# ---------------------------------------------------------------------------
# Drawing
# ---------------------------------------------------------------------------
BG = (20, 22, 26)
BODY = (58, 60, 66)
PANEL = (8, 8, 10)
LED_ON = (255, 250, 238)
LED_OFF = (34, 34, 38)
CAPTION = (225, 228, 235)
SUB = (150, 155, 165)

try:
    FONT = ImageFont.truetype("/usr/share/fonts/opentype/inter/Inter-SemiBold.otf", 30)
    FONT_S = ImageFont.truetype("/usr/share/fonts/opentype/inter/Inter-Regular.otf", 22)
except OSError:  # pragma: no cover
    FONT = FONT_S = ImageFont.load_default()


@dataclass
class Scene:
    frame: list
    bars: list  # 10 RGB tuples: left[0..4] bottom->top, right[0..4]
    title: str
    sub: str = ""
    brightness: float = 1.0  # matrix global brightness (TM1680 duty)


def draw_device(s: Scene, scale: int = 26) -> Image.Image:
    pitch = scale
    led_r = int(pitch * 0.34)
    panel = pitch * 16 + pitch
    bar_w = int(pitch * 1.1)
    body_pad = int(pitch * 1.3)
    body = panel + 2 * body_pad
    Wimg = body + 2 * (bar_w + 30) + 60
    Himg = body + 150
    img = Image.new("RGB", (Wimg, Himg), BG)
    glow = Image.new("RGB", (Wimg, Himg), (0, 0, 0))
    d = ImageDraw.Draw(img)
    g = ImageDraw.Draw(glow)

    bx = (Wimg - body) // 2
    by = 30
    d.rounded_rectangle([bx, by, bx + body, by + body], radius=int(pitch * 1.2), fill=BODY)
    px, py = bx + body_pad, by + body_pad
    d.rounded_rectangle([px, py, px + panel, py + panel], radius=int(pitch * 0.4), fill=PANEL)

    on = tuple(int(c * (0.35 + 0.65 * s.brightness)) for c in LED_ON)
    for r in range(16):
        for c in range(16):
            cx = px + pitch + c * pitch
            cy = py + pitch + r * pitch
            if s.frame[r][c]:
                d.ellipse([cx - led_r, cy - led_r, cx + led_r, cy + led_r], fill=on)
                gr = int(led_r * 1.9)
                gc = tuple(int(v * 0.55 * s.brightness) for v in LED_ON)
                g.ellipse([cx - gr, cy - gr, cx + gr, cy + gr], fill=gc)
            else:
                d.ellipse([cx - led_r, cy - led_r, cx + led_r, cy + led_r], fill=LED_OFF)

    # bars: drawn flanking the body, as seen with the stack standing upright
    seg_h = body / 5
    for side, x0 in ((0, bx - bar_w - 18), (1, bx + body + 18)):
        d.rounded_rectangle([x0, by, x0 + bar_w, by + body], radius=bar_w // 2, fill=(42, 44, 50))
        for i in range(5):
            col = s.bars[side * 5 + i]
            y1 = by + body - (i + 1) * seg_h + 8
            y2 = by + body - i * seg_h - 8
            if col != OFF and sum(col) > 0:
                d.rounded_rectangle([x0 + 5, y1, x0 + bar_w - 5, y2], radius=8, fill=_blend(col, (42, 44, 50)))
                g.rounded_rectangle([x0 - 10, y1 - 10, x0 + bar_w + 10, y2 + 10], radius=18,
                                    fill=tuple(int(v * 0.6) for v in col))
            else:
                d.rounded_rectangle([x0 + 5, y1, x0 + bar_w - 5, y2], radius=8, fill=(30, 31, 36))

    glow = glow.filter(ImageFilter.GaussianBlur(pitch * 0.45))
    img = Image.fromarray(_add(img, glow))
    d = ImageDraw.Draw(img)
    d.text((Wimg // 2, by + body + 52), s.title, font=FONT, fill=CAPTION, anchor="mm")
    if s.sub:
        d.text((Wimg // 2, by + body + 94), s.sub, font=FONT_S, fill=SUB, anchor="mm")
    return img


def _blend(col, base):
    # LED colour as perceived through a diffuser: never darker than the base
    a = max(col) / 255
    return tuple(int(base[i] * (1 - a) + col[i] * 1.0) if max(col) else base[i] for i in range(3))


def _add(a: Image.Image, b: Image.Image):
    import numpy as np
    x = np.asarray(a, dtype=np.int16) + np.asarray(b, dtype=np.int16)
    return np.clip(x, 0, 255).astype("uint8")


def grid(images, cols):
    w, h = images[0].size
    rows = math.ceil(len(images) / cols)
    out = Image.new("RGB", (w * cols, h * rows), BG)
    for i, im in enumerate(images):
        out.paste(im, ((i % cols) * w, (i // cols) * h))
    return out


def save(img: Image.Image, name: str, width: int | None = None):
    if width and img.width > width:
        img = img.resize((width, int(img.height * width / img.width)), Image.LANCZOS)
    path = os.path.join(OUT, name)
    img.save(path, optimize=True)
    print("wrote", os.path.relpath(path))


# ---------------------------------------------------------------------------
# Scenes
# ---------------------------------------------------------------------------
def battery_bars(level_: float, charging: bool = False):
    b = gauge(level_, GREEN, cap=0.35)
    return b + b


def run_scene(minutes: float, frac: float, t_anim: float, title: str, sub: str = ""):
    total = minutes * 60
    rem = total * frac
    bars = gauge(frac, run_color(frac), cap=0.6)
    return Scene(hourglass(rem, total, t_anim), bars + bars, title, sub)


def main():
    os.makedirs(OUT, exist_ok=True)
    T = 25

    scenes = {
        "01-setup": Scene(duration_frame(25), battery_bars(0.8), "SET  25 min",
                          "A / C adjust  ·  B start  ·  bars = battery"),
        "02-setup-long": Scene(duration_frame(105), battery_bars(0.8), "SET  1:45",
                               "≥ 100 min shows H:MM"),
        "03-start": run_scene(T, 1.0, 0.21, "RUN  100 %", "70 grains, one every 21 s"),
        "04-run-60": run_scene(T, 0.60, 0.35, "RUN  60 %  (15 min left)", "bars fade continuously"),
        "05-run-15": run_scene(T, 0.15, 0.14, "RUN  15 %", "bars turn amber ≤ 20 %"),
        "06-run-3": run_scene(T, 0.03, 0.07, "RUN  3 %", "red ≤ 5 %"),
    }
    paused = run_scene(T, 0.45, None, "PAUSED", "stream stops, matrix breathes")
    paused.brightness = 0.45
    paused.bars = [AMBER if i == 0 else OFF for i in range(5)] * 2
    paused.bars = [tuple(int(v * 0.4) for v in c) for c in paused.bars]
    scenes["07-paused"] = paused

    done_a = Scene(hourglass(0, T * 60), [RED] * 10, "DONE", "flashes 2 Hz + chirp")
    done_b = Scene(invert(hourglass(0, T * 60)), [OFF] * 10, "DONE (alt frame)", "any key acknowledges")
    scenes["08-done-a"] = done_a
    scenes["09-done-b"] = done_b

    peek = Scene(text_frame("11", 2, 2), gauge(0.45, TEAL, 0.6) * 2, "PEEK  11 min left",
                 "short A while running, 2 s")
    scenes["10-peek"] = peek
    lvl = Scene(level(T * 60 * 0.6, T * 60), gauge(0.6, TEAL, 0.6) * 2, "LEVEL FACE  60 %",
                "256 steps, one row = 1/16 of the block")
    scenes["11-level"] = lvl

    imgs = {k: draw_device(v) for k, v in scenes.items()}
    for k, im in imgs.items():
        save(im, f"{k}.png", width=520)

    lifecycle = grid([imgs[k] for k in ["01-setup", "03-start", "04-run-60", "05-run-15",
                                        "06-run-3", "08-done-a"]], 3)
    save(lifecycle, "lifecycle.png", width=1500)
    others = grid([imgs[k] for k in ["02-setup-long", "07-paused", "09-done-b", "10-peek",
                                     "11-level"]], 5)
    save(others, "other-states.png", width=1800)

    # Animated GIF: a 25-minute block compressed to ~12 s, plus real-time stream
    frames = []
    n = 110
    for i in range(n + 1):
        frac = 1 - i / n
        sc = run_scene(T, frac, i * 0.08, f"{math.ceil(T * frac):d} min left", "time-lapse")
        if frac == 0:
            sc = Scene(hourglass(0, T * 60), [RED] * 10, "DONE", "time-lapse")
        frames.append(draw_device(sc, scale=14).convert("P", palette=Image.ADAPTIVE, colors=64))
    for j in range(8):
        sc = Scene(invert(hourglass(0, T * 60)) if j % 2 else hourglass(0, T * 60),
                   [OFF] * 10 if j % 2 else [RED] * 10, "DONE", "time-lapse")
        frames.append(draw_device(sc, scale=14).convert("P", palette=Image.ADAPTIVE, colors=64))
    gif = os.path.join(OUT, "timelapse.gif")
    frames[0].save(gif, save_all=True, append_images=frames[1:], duration=[80] * (n + 1) + [250] * 8,
                   loop=0, optimize=True)
    print("wrote", os.path.relpath(gif))

    # Real-time close-up of the falling grain at 60 % (shows the cadence)
    frames = []
    for i in range(40):
        sc = run_scene(T, 0.6, i * 0.07, "Falling grain, real time", "one row per 70 ms")
        frames.append(draw_device(sc, scale=14).convert("P", palette=Image.ADAPTIVE, colors=64))
    gif = os.path.join(OUT, "falling-grain.gif")
    frames[0].save(gif, save_all=True, append_images=frames[1:], duration=70, loop=0, optimize=True)
    print("wrote", os.path.relpath(gif))


if __name__ == "__main__":
    main()
