#!/usr/bin/env python3
"""
gen_trade_signs.py — default hanging-sign board art for the settlement TRADES
(CityForgePlan M3c; closes the sign_* rows in resources/asset_requests.json).

Medieval trade signs were SYMBOL-FIRST (an illiterate street reads the pretzel,
not the word), so each board is the plank-and-frame style of the Prancing Pony
sign (tools/gen_surface_demo_assets.py) carrying the trade's emblem plus one
small caption word. Deterministic, no LLM.

Run from the repo root:  python tools/gen_trade_signs.py
Then: gen_items.py emits the matching flat item boards; materials.json /
items.json / room_program.json carry the registrations; relaunch the engine
(the atlas rebuilds from the new PNGs).
"""
import os

from PIL import Image, ImageDraw, ImageFilter

TEX_DIR = os.path.join("resources", "textures", "source")
SZ = 512

DARK, MID, LITE = (52, 32, 16), (96, 62, 32), (120, 82, 44)
GOLD, CREAM, IRON = (206, 170, 80), (236, 224, 198), (58, 58, 64)


def font(size):
    from PIL import ImageFont
    for p in (r"C:\Windows\Fonts\georgiab.ttf", r"C:\Windows\Fonts\timesbd.ttf",
              r"C:\Windows\Fonts\georgia.ttf", r"C:\Windows\Fonts\arialbd.ttf"):
        try:
            return ImageFont.truetype(p, size)
        except Exception:
            pass
    return ImageFont.load_default()


def board():
    """Weathered plank board + carved gold-lined frame (the Pony sign's family look)."""
    img = Image.new("RGB", (SZ, SZ), (74, 48, 26))
    d = ImageDraw.Draw(img)
    nb = 6
    bw = SZ / nb
    for i in range(nb):
        x0 = int(i * bw)
        d.rectangle([x0, 0, int(x0 + bw) - 2, SZ], fill=MID if i % 2 == 0 else LITE)
        for g in range(6):
            gy = 20 + g * 80 + (i * 13 % 30)
            d.arc([x0 - 10, gy, x0 + bw + 10, gy + 36], 200, 340, fill=DARK)
    for i in range(14):
        d.rectangle([i, i, SZ - 1 - i, SZ - 1 - i], outline=DARK if i < 8 else GOLD)
    d.rectangle([26, 26, SZ - 27, SZ - 27], outline=GOLD, width=2)
    return img, d


def caption(d, word):
    d.text((SZ / 2, SZ * 0.86), word, font=font(52), fill=GOLD, anchor="mm")


def save(img, name):
    img = img.filter(ImageFilter.GaussianBlur(0.4))
    img.save(os.path.join(TEX_DIR, name + ".png"))
    print("wrote", name + ".png")


def sign_blacksmith():
    # Emblem: the ANVIL, cream, with a crossed hammer above it.
    img, d = board()
    cx, cy, s = SZ * 0.5, SZ * 0.50, SZ * 0.28
    d.polygon([  # anvil silhouette: horn -> face -> heel -> waist -> foot
        (cx - 1.00 * s, cy - 0.30 * s), (cx - 0.55 * s, cy - 0.46 * s),
        (cx + 0.80 * s, cy - 0.46 * s), (cx + 0.80 * s, cy - 0.10 * s),
        (cx + 0.28 * s, cy - 0.02 * s), (cx + 0.28 * s, cy + 0.34 * s),
        (cx + 0.52 * s, cy + 0.54 * s), (cx + 0.52 * s, cy + 0.70 * s),
        (cx - 0.52 * s, cy + 0.70 * s), (cx - 0.52 * s, cy + 0.54 * s),
        (cx - 0.28 * s, cy + 0.34 * s), (cx - 0.28 * s, cy - 0.02 * s),
        (cx - 0.62 * s, cy - 0.10 * s), (cx - 1.00 * s, cy - 0.12 * s),
    ], fill=CREAM)
    # Hammer poised over the face: stout gold haft, big iron head with a cream edge highlight.
    d.line([(cx + 0.72 * s, cy - 0.62 * s), (cx + 0.10 * s, cy - 1.25 * s)], fill=GOLD, width=20)
    d.polygon([(cx - 0.28 * s, cy - 1.52 * s), (cx + 0.34 * s, cy - 1.52 * s),
               (cx + 0.34 * s, cy - 1.06 * s), (cx - 0.28 * s, cy - 1.06 * s)], fill=IRON)
    d.rectangle([cx - 0.28 * s, cy - 1.52 * s, cx - 0.18 * s, cy - 1.06 * s], fill=CREAM)
    caption(d, "SMITHY")
    save(img, "sign_blacksmith")


def sign_bakery():
    # Emblem: the guild PRETZEL, golden — the historic baker's sign.
    img, d = board()
    cx, cy, r = SZ * 0.5, SZ * 0.42, SZ * 0.24
    ring = [cx - r, cy - r, cx + r, cy + r]
    for w, col in ((34, DARK), (24, GOLD)):
        d.arc(ring, 150, 30, fill=col, width=w)              # the pretzel bow (open at the bottom)
        # crossed arms from the ring's shoulders down to the base
        a0 = (cx - 0.72 * r, cy - 0.42 * r)
        a1 = (cx + 0.72 * r, cy - 0.42 * r)
        b0 = (cx - 0.82 * r, cy + 1.05 * r)
        b1 = (cx + 0.82 * r, cy + 1.05 * r)
        d.line([a0, b1], fill=col, width=w)
        d.line([a1, b0], fill=col, width=w)
    caption(d, "BAKERY")
    save(img, "sign_bakery")


def sign_general_store():
    # Emblem: the merchant's BALANCE — beam, chains, two pans.
    img, d = board()
    cx, cy, s = SZ * 0.5, SZ * 0.30, SZ * 0.30
    d.line([(cx, cy - 0.30 * s), (cx, cy + 1.05 * s)], fill=CREAM, width=12)   # column
    d.polygon([(cx - 0.5 * s, cy + 1.30 * s), (cx + 0.5 * s, cy + 1.30 * s),
               (cx, cy + 0.95 * s)], fill=CREAM)                                # foot
    d.line([(cx - 1.0 * s, cy), (cx + 1.0 * s, cy)], fill=CREAM, width=12)      # beam
    for sgn in (-1, 1):
        px = cx + sgn * 1.0 * s
        for dx in (-0.34, 0.34):
            d.line([(px, cy), (px + dx * s, cy + 0.62 * s)], fill=GOLD, width=6)
        d.chord([px - 0.44 * s, cy + 0.38 * s, px + 0.44 * s, cy + 0.98 * s],
                0, 180, fill=GOLD, outline=DARK)                                # pan
    caption(d, "GOODS")
    save(img, "sign_general_store")


def sign_apothecary():
    # Emblem: MORTAR AND PESTLE, cream bowl, gold pestle, a sprig of herb.
    img, d = board()
    cx, cy, s = SZ * 0.5, SZ * 0.48, SZ * 0.30
    d.polygon([(cx - 0.95 * s, cy - 0.20 * s), (cx + 0.95 * s, cy - 0.20 * s),
               (cx + 0.60 * s, cy + 0.62 * s), (cx - 0.60 * s, cy + 0.62 * s)], fill=CREAM)  # bowl
    d.rectangle([cx - 0.72 * s, cy + 0.62 * s, cx + 0.72 * s, cy + 0.78 * s], fill=CREAM)    # foot
    d.line([(cx + 0.10 * s, cy - 0.16 * s), (cx + 0.78 * s, cy - 1.05 * s)], fill=GOLD, width=22)  # pestle
    d.ellipse([cx + 0.62 * s, cy - 1.22 * s, cx + 0.95 * s, cy - 0.90 * s], fill=GOLD)
    for k, (lx, ly) in enumerate([(-0.55, -0.72), (-0.75, -0.95), (-0.35, -1.02)]):          # herb sprig
        d.ellipse([cx + lx * s - 14, cy + ly * s - 26, cx + lx * s + 14, cy + ly * s + 26],
                  fill=(96, 130, 70))
    d.line([(cx - 0.30 * s, cy - 0.22 * s), (cx - 0.75 * s, cy - 0.95 * s)],
           fill=(70, 96, 52), width=8)
    caption(d, "HERBS")
    save(img, "sign_apothecary")


def sign_butcher():
    # Emblem: the CLEAVER, iron blade, wood grip.
    img, d = board()
    cx, cy, s = SZ * 0.5, SZ * 0.42, SZ * 0.32
    STEEL, EDGE = (108, 112, 122), (210, 214, 222)
    d.polygon([(cx - 1.05 * s, cy - 0.50 * s), (cx + 0.30 * s, cy - 0.50 * s),
               (cx + 0.30 * s, cy + 0.50 * s), (cx - 0.85 * s, cy + 0.50 * s),
               (cx - 1.05 * s, cy + 0.25 * s)], fill=STEEL)                     # blade
    d.polygon([(cx - 0.85 * s, cy + 0.34 * s), (cx + 0.30 * s, cy + 0.34 * s),
               (cx + 0.30 * s, cy + 0.50 * s), (cx - 0.85 * s, cy + 0.50 * s)],
              fill=EDGE)                                                        # honed edge band
    d.ellipse([cx - 0.90 * s, cy - 0.40 * s, cx - 0.70 * s, cy - 0.20 * s], fill=(74, 48, 26))  # hang hole
    d.rectangle([cx + 0.30 * s, cy - 0.14 * s, cx + 1.05 * s, cy + 0.14 * s], fill=(96, 62, 32))  # grip
    for rx in (0.48, 0.66, 0.84):                                               # grip rivets
        d.ellipse([cx + rx * s - 6, cy - 6, cx + rx * s + 6, cy + 6], fill=GOLD)
    caption(d, "BUTCHER")
    save(img, "sign_butcher")


if __name__ == "__main__":
    sign_blacksmith()
    sign_bakery()
    sign_general_store()
    sign_apothecary()
    sign_butcher()
