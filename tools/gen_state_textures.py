#!/usr/bin/env python3
"""
gen_state_textures.py — Procedural "state surface" textures for the voxel
appearance model (docs/VoxelAppearanceModel.md). State (flaming/charred/...) can
REPLACE a voxel's surface rather than just tint it: a flaming log should look like
burning wood, not orange-tinted wood.

Currently generates:
  burning_wood — charred charcoal bark with a network of glowing orange→yellow
                 ember cracks. Organic, so it slices gracefully across micro/sub-
                 cubes (no grain to misalign). Emissive — the bright bits bloom.

Writes resources/textures/source/burning_wood.png (one tile, reused for all 6
faces). The engine auto-generates the flat normal/roughness sidecar.
"""
import argparse, math, os, random
from PIL import Image


def value_noise(size, cells_x, cells_y, seed):
    """Smooth value noise: random low-res grid upscaled bicubically to size.

    cells_x < cells_y stretches features VERTICALLY (taller than wide) so the
    ember cracks run with a burning log's grain instead of looking like spatter."""
    rng = random.Random(seed)
    small = Image.new("L", (cells_x, cells_y))
    small.putdata([rng.randint(0, 255) for _ in range(cells_x * cells_y)])
    return small.resize((size, size), Image.BICUBIC).load()


def _ember_color(heat):
    """Black-body-ish ramp: deep red -> orange -> yellow -> white as heat 0..1."""
    r = min(255, int(120 + heat * 135))
    g = min(255, int((heat ** 1.7) * 245))
    b = min(255, int((heat ** 3.5) * 235))
    return r, g, b


def gen_burning_wood(size, seed):
    # Vertically-stretched multi-octave noise -> a "heat" field. Hot upper tail
    # becomes a branching network of glowing cracks (a log splitting as it burns);
    # everything below is near-black charcoal. High contrast + bright cores so that
    # even a single 1/9 microcube slice still reads as a glowing coal.
    n1 = value_noise(size, 10,  4, seed + 1)   # broad vertical cracks (tall features)
    n2 = value_noise(size, 28, 12, seed + 2)   # branch detail
    n3 = value_noise(size, 64, 40, seed + 3)   # fine speckle
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            v = (n1[x, y] * 0.50 + n2[x, y] * 0.34 + n3[x, y] * 0.16) / 255.0  # 0..1
            ember = max(0.0, (v - 0.66) / 0.34)          # sparse hot veins, mostly char
            if ember > 0.015:
                heat = ember ** 0.85                      # bias toward hotter
                px[x, y] = _ember_color(heat)
            else:
                # charred wood: near-black charcoal, faint warm ember-glow bleed
                glow = max(0.0, (v - 0.40)) * 30.0        # subtle heat near cracks
                k = int(8 + v * 14)
                px[x, y] = (min(255, k + 6 + int(glow)), k + 1, k)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--texdir", default="resources/textures/source")
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()
    os.makedirs(args.texdir, exist_ok=True)
    out = os.path.join(args.texdir, "burning_wood.png")
    gen_burning_wood(args.size, args.seed).save(out)
    print(f"wrote {out} ({args.size}x{args.size})")


if __name__ == "__main__":
    main()
