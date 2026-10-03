#!/usr/bin/env python3
"""Before/after contact sheet for tools/debris_settle_bench.py --frames runs.

Same scenario, same camera, same simulation ticks: the BEFORE run's frames on the top row,
the AFTER run's frames below, cropped to the editor viewport. One PNG per scenario.

  python tools/debris_settle_contact_sheet.py lab-baseline fixed-2026-10-03 [--out DIR]
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EVID = os.path.join(ROOT, "docs", "evidence", "debris_settle")
TICKS = [0, 20, 60, 120, 240, 480]
VIEWPORT = (242, 44, 1197, 672)       # editor viewport inside a 1600x900 capture
THUMB_W = 380


def load(tag, scen, tick):
    p = os.path.join(EVID, tag, "frames", f"{scen}_t{tick:04d}.png")
    if not os.path.exists(p):
        return None
    im = Image.open(p).convert("RGB")
    if im.size == (1600, 900):
        im = im.crop(VIEWPORT)
    h = int(im.size[1] * THUMB_W / im.size[0])
    return im.resize((THUMB_W, h))


def sheet(before, after, scen, out_dir):
    rows = [("BEFORE  " + before, before), ("AFTER  " + after, after)]
    thumbs = [[load(tag, scen, t) for t in TICKS] for _, tag in rows]
    th = next((im.size[1] for r in thumbs for im in r if im), 240)
    label_h, pad = 26, 6
    W = len(TICKS) * (THUMB_W + pad) + pad
    H = len(rows) * (th + label_h + pad) + label_h + pad
    img = Image.new("RGB", (W, H), (24, 24, 28))
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("arial.ttf", 18)
    except OSError:
        font = ImageFont.load_default()
    d.text((pad, 4), f"{scen}  —  same camera, same ticks (60 Hz)", fill=(230, 230, 230), font=font)
    for c, t in enumerate(TICKS):
        d.text((pad + c * (THUMB_W + pad), label_h - 2), f"t = {t / 60:.2f} s (tick {t})",
               fill=(180, 180, 190), font=font)
    for r, (label, _) in enumerate(rows):
        y0 = label_h + 20 + r * (th + label_h + pad)
        d.text((pad, y0), label, fill=(255, 200, 120) if r == 0 else (140, 220, 140), font=font)
        for c, im in enumerate(thumbs[r]):
            x = pad + c * (THUMB_W + pad)
            if im:
                img.paste(im, (x, y0 + label_h))
            else:
                d.rectangle([x, y0 + label_h, x + THUMB_W, y0 + label_h + th], outline=(90, 90, 90))
                d.text((x + 10, y0 + label_h + 10), "no frame", fill=(150, 150, 150), font=font)
    os.makedirs(out_dir, exist_ok=True)
    p = os.path.join(out_dir, f"compare_{scen}.png")
    img.save(p)
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("--out", default=None)
    ap.add_argument("--scenarios", nargs="*",
                    default=["drop_layer", "drop_pile", "packed", "crater", "crater_subcube", "blast"])
    a = ap.parse_args()
    out = a.out or os.path.join(EVID, a.after, "compare")
    for s in a.scenarios:
        print(sheet(a.before, a.after, s, out))


if __name__ == "__main__":
    main()
