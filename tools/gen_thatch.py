#!/usr/bin/env python3
"""Procedural THATCH roof texture (v1) for the Phyxel material 'Thatch'.

Dried-straw thatch: golden colour, horizontal COURSES (overlapping bundles, each
with a shadow line under its leading edge), and fine vertical STRAND striations.
64x64 RGBA to match the existing source textures in resources/textures/source/.

This is a basic generated texture, not hand-painted art — improve later by editing
the colour/course/strand parameters here, or drop in real art with the same names.

    python tools/gen_thatch.py    # writes resources/textures/source/thatch_*.png
"""
import random
from pathlib import Path
from PIL import Image

SIZE = 64
OUT = Path(__file__).resolve().parents[1] / "resources" / "textures" / "source"
STRAW = (190, 150, 70)          # straw gold base
COURSE = 16                     # pixels per thatch course (bundle row)


def clamp(v):
    return max(0, min(255, int(v)))


def thatch_face(seed, courses=True):
    rng = random.Random(seed)
    img = Image.new("RGBA", (SIZE, SIZE))
    px = img.load()
    col = [rng.uniform(0.80, 1.12) for _ in range(SIZE)]   # per-strand brightness
    streak = {x for x in range(SIZE) if rng.random() < 0.10}  # darker strands
    for y in range(SIZE):
        cy = 0.0
        if courses:
            band = y % COURSE
            if band == COURSE - 1:   cy = -0.38   # shadow line under each course
            elif band == COURSE - 2: cy = -0.20
            elif band <= 1:          cy = 0.12    # lit leading edge of next course
        for x in range(SIZE):
            b = col[x] + cy + rng.uniform(-0.06, 0.06)
            if x in streak:
                b -= 0.18
            px[x, y] = (clamp(STRAW[0] * b), clamp(STRAW[1] * b), clamp(STRAW[2] * b), 255)
    return img


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    faces = {
        "side_n": thatch_face(7001, True),  "side_s": thatch_face(7002, True),
        "side_e": thatch_face(7003, True),  "side_w": thatch_face(7004, True),
        "top":    thatch_face(7005, False), "bottom": thatch_face(7006, False),
    }
    for face, img in faces.items():
        p = OUT / f"thatch_{face}.png"
        img.save(p)
        print("wrote", p.name, img.size, img.mode)


if __name__ == "__main__":
    main()
