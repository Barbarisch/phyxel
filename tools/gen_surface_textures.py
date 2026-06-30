#!/usr/bin/env python3
"""
gen_surface_textures.py — Placeholder "projected surface" textures for Phase 3 of
the voxel appearance model (docs/VoxelAppearanceModel.md §7).

A projected surface stretches ONE image across a whole Tier-2 prop (rug, painting,
banner, mosaic) instead of tiling the material per voxel. To verify the projection
is correct we need an image that is GLOBALLY asymmetric — if it were tiled per cube
you'd see the pattern repeat; if it's projected you see exactly one of each marker.

rug_test — a single 512px image with:
  * four differently-coloured quadrants (so orientation/flips are obvious),
  * a bright diagonal from corner to corner (a repeat would show many diagonals),
  * a numbered marker in each corner + a ring in the centre.
Reused for all 6 faces; the engine auto-generates the flat normal/roughness sidecar.

Writes resources/textures/source/rug_test.png. Swap in real art later (see
docs/MaterialTextureNeeds.md) — the engine path is texture-agnostic.
"""
import argparse, os
from PIL import Image, ImageDraw

OUT_DIR = os.path.join("resources", "textures", "source")


def gen_rug_test(size):
    img = Image.new("RGB", (size, size), (30, 30, 36))
    d = ImageDraw.Draw(img)
    h = size // 2

    # Four quadrants — distinct hues so any mirror/flip is visible at a glance.
    d.rectangle([0, 0, h, h],          fill=(176, 64, 48))    # TL  red
    d.rectangle([h, 0, size, h],       fill=(64, 128, 176))   # TR  blue
    d.rectangle([0, h, h, size],       fill=(72, 160, 88))    # BL  green
    d.rectangle([h, h, size, size],    fill=(200, 168, 64))   # BR  gold

    # Single corner-to-corner diagonal: a tiled repeat would show a sawtooth.
    d.line([0, 0, size, size], fill=(245, 245, 245), width=max(3, size // 96))

    # Centre ring — appears exactly once iff projected (not once-per-cube).
    r = size // 8
    d.ellipse([h - r, h - r, h + r, h + r], outline=(245, 245, 245),
              width=max(3, size // 128))

    # Corner index dots so the four corners are individually identifiable.
    m = size // 12
    for (cx, cy) in [(m, m), (size - m, m), (m, size - m), (size - m, size - m)]:
        rr = size // 32
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=(20, 20, 20))

    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=512)
    args = ap.parse_args()
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, "rug_test.png")
    gen_rug_test(args.size).save(path)
    print(f"wrote {path} ({args.size}x{args.size})")


if __name__ == "__main__":
    main()
