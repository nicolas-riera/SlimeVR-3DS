#!/usr/bin/env python3
"""Generates meta/icon.png (48x48), meta/banner.png (256x128) and meta/banner.wav.

Pure standard library (no Pillow needed). Run from the repository root:
    python3 tools/gen_assets.py
"""
import math
import os
import struct
import wave
import zlib

SS = 4  # supersampling factor

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "meta")


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w * SS, h * SS
        self.ow, self.oh = w, h
        self.px = [[0.0, 0.0, 0.0, 0.0] for _ in range(self.w * self.h)]

    def blend(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            p = self.px[y * self.w + x]
            a = c[3]
            for i in range(3):
                p[i] = p[i] * (1 - a) + c[i] * a
            p[3] = a + p[3] * (1 - a)

    def fill(self, inside, color, bbox=None):
        x0, y0, x1, y1 = bbox or (0, 0, self.ow, self.oh)
        for y in range(max(0, int(y0 * SS)), min(self.h, int(math.ceil(y1 * SS)))):
            for x in range(max(0, int(x0 * SS)), min(self.w, int(math.ceil(x1 * SS)))):
                fx, fy = (x + 0.5) / SS, (y + 0.5) / SS
                if inside(fx, fy):
                    self.blend(x, y, color(fx, fy) if callable(color) else color)

    def save_png(self, path):
        rows = []
        for y in range(self.oh):
            row = bytearray([0])
            for x in range(self.ow):
                acc = [0.0] * 4
                for sy in range(SS):
                    for sx in range(SS):
                        p = self.px[(y * SS + sy) * self.w + x * SS + sx]
                        for i in range(3):
                            acc[i] += p[i] * p[3]
                        acc[3] += p[3]
                a = acc[3] / (SS * SS)
                rgb = [acc[i] / acc[3] if acc[3] > 0 else 0 for i in range(3)]
                row += bytes(max(0, min(255, int(round(v * 255)))) for v in rgb + [a])
            rows.append(bytes(row))

        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", self.ow, self.oh, 8, 6, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
        png += chunk(b"IEND", b"")
        with open(path, "wb") as f:
            f.write(png)


def rgb(h, a=1.0):
    return [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)] + [a]


def lerp(c0, c1, t):
    t = max(0.0, min(1.0, t))
    return [c0[i] + (c1[i] - c0[i]) * t for i in range(4)]


def in_round_rect(x0, y0, x1, y1, r):
    def f(x, y):
        if not (x0 <= x <= x1 and y0 <= y <= y1):
            return False
        cx = min(max(x, x0 + r), x1 - r)
        cy = min(max(y, y0 + r), y1 - r)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    return f


def in_ellipse(cx, cy, rx, ry):
    return lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1


def draw_slime(c, cx, cy, r, body_top, body_bottom):
    """Slime blob: dome + flat base, glossy highlight and eyes."""
    top = cy - r * 0.95
    bottom = cy + r * 0.75
    shadow = in_ellipse(cx, bottom + r * 0.05, r * 1.15, r * 0.22)
    c.fill(shadow, [0, 0, 0, 0.35])

    def body(x, y):
        if y < cy:
            return in_ellipse(cx, cy, r, r * 0.95)(x, y)
        if y <= bottom - r * 0.2:
            return abs(x - cx) <= r
        return in_ellipse(cx, bottom - r * 0.2, r, r * 0.2)(x, y) or (abs(x - cx) <= r and y <= bottom - r * 0.2)

    c.fill(body, lambda x, y: lerp(body_top, body_bottom, (y - top) / (bottom - top)))
    c.fill(in_ellipse(cx - r * 0.42, cy - r * 0.5, r * 0.28, r * 0.17), [1, 1, 1, 0.55])
    eye = rgb("1c1530")
    c.fill(in_ellipse(cx - r * 0.36, cy + r * 0.05, r * 0.12, r * 0.17), eye)
    c.fill(in_ellipse(cx + r * 0.36, cy + r * 0.05, r * 0.12, r * 0.17), eye)
    c.fill(in_ellipse(cx - r * 0.33, cy - r * 0.02, r * 0.045, r * 0.06), [1, 1, 1, 0.9])
    c.fill(in_ellipse(cx + r * 0.39, cy - r * 0.02, r * 0.045, r * 0.06), [1, 1, 1, 0.9])
    blush = rgb("ff7aa8", 0.45)
    c.fill(in_ellipse(cx - r * 0.62, cy + r * 0.28, r * 0.13, r * 0.08), blush)
    c.fill(in_ellipse(cx + r * 0.62, cy + r * 0.28, r * 0.13, r * 0.08), blush)


def draw_waves(c, cx, cy, radii, width, color):
    """Wi-Fi style arcs opening towards the top-right."""
    for rr in radii:
        def arc(x, y, rr=rr):
            d = math.hypot(x - cx, y - cy)
            ang = math.atan2(cy - y, x - cx)
            return abs(d - rr) <= width / 2 and 0.1 < ang < 1.47
        c.fill(arc, color)
    c.fill(in_ellipse(cx, cy, width * 0.75, width * 0.75), color)


FONT = {
    "S": [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "],
    "L": ["#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"],
    "I": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"],
    "M": ["#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"],
    "E": ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"],
    "V": ["#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "],
    "R": ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"],
    "3": ["#### ", "    #", "    #", " ### ", "    #", "    #", "#### "],
    "D": ["#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "],
    "T": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    "A": [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    "C": [" ####", "#    ", "#    ", "#    ", "#    ", "#    ", " ####"],
    "K": ["#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"],
    " ": ["     "] * 7,
}


def draw_text(c, x, y, s, scale, color, shadow=None):
    for dx, dy, col in ([(scale * 0.35, scale * 0.35, shadow)] if shadow else []) + [(0, 0, color)]:
        cx = x
        for ch in s:
            g = FONT[ch]
            for gy, line in enumerate(g):
                for gx, p in enumerate(line):
                    if p == "#":
                        px, py = cx + gx * scale + dx, y + gy * scale + dy
                        c.fill(in_round_rect(px, py, px + scale * 0.92, py + scale * 0.92, scale * 0.25), col,
                               (px, py, px + scale, py + scale))
            cx += 6 * scale


def text_width(s, scale):
    return (6 * len(s) - 1) * scale


def make_icon():
    c = Canvas(48, 48)
    bg = in_round_rect(1, 1, 47, 47, 9)
    c.fill(bg, lambda x, y: lerp(rgb("3a2470"), rgb("120e2a"), (x + y) / 96))
    draw_waves(c, 33, 15, [5, 9.5], 2.4, rgb("7ff0b0"))
    draw_slime(c, 21, 28, 13, rgb("c9a8ff"), rgb("7a4fe0"))
    c.save_png(os.path.join(ROOT, "icon.png"))


def make_banner():
    c = Canvas(256, 128)
    c.fill(lambda x, y: True, lambda x, y: lerp(rgb("2b1a5c"), rgb("0e0b22"), y / 128))
    # soft glow behind the slime
    for i in range(6):
        c.fill(in_ellipse(62, 70, 52 - i * 7, 46 - i * 6), rgb("8a5cff", 0.06))
    draw_slime(c, 62, 72, 34, rgb("c9a8ff"), rgb("7a4fe0"))
    draw_waves(c, 94, 34, [10, 18], 4, rgb("7ff0b0"))
    title, sub = "SLIMEVR", "3DS TRACKER"
    tx = 176 - text_width(title, 3.4) / 2
    draw_text(c, tx, 40, title, 3.4, rgb("efe6ff"), rgb("000000", 0.45))
    draw_text(c, 176 - text_width(sub, 1.9) / 2, 72, sub, 1.9, rgb("7ff0b0"), rgb("000000", 0.45))
    c.save_png(os.path.join(ROOT, "banner.png"))


def make_jingle():
    rate = 22050
    notes = [(0.00, 0.18, 523.25, 698.46), (0.20, 0.45, 783.99, 1046.5)]  # boing-ish slides
    total = 1.0
    data = bytearray()
    for i in range(int(total * rate)):
        t = i / rate
        v = 0.0
        for start, dur, f0, f1 in notes:
            if start <= t < start + dur:
                u = (t - start) / dur
                f = f0 + (f1 - f0) * min(1.0, u * 3)
                env = min(1.0, (t - start) * 200) * (1 - u) ** 2
                ph = 2 * math.pi * f * (t - start)
                v += env * (0.6 * math.sin(ph) + 0.25 * math.sin(2 * ph))
        data += struct.pack("<h", int(max(-1, min(1, v * 0.7)) * 32767))
    with wave.open(os.path.join(ROOT, "banner.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(data))


if __name__ == "__main__":
    os.makedirs(ROOT, exist_ok=True)
    make_icon()
    make_banner()
    make_jingle()
    print("assets written to", os.path.normpath(ROOT))
