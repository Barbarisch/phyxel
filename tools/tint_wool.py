#!/usr/bin/env python3
"""Dye the Wool faces madder red.

ambientCG's Fabric019 is a genuine wool KNIT but undyed white, and the voxel shader
currently ignores materials.json colorTint — so the albedo itself must carry the dye.
This multiplies the fetched white knit by a warm madder red (the common medieval red
dye), preserving the knit's luminance detail. Re-run AFTER any Wool re-fetch
(tools/fetch_cc0_textures.py Wool) or the coverlets revert to white.
Deterministic; same precedent as tools/gen_birch_bark.py (procedural on a fetched base).
"""
import os

from PIL import Image

SRC = "resources/textures/source"
# Madder red, lifted so the knit's highlights survive the multiply.
DYE = (0.72, 0.26, 0.20)

for face in ["side_n", "side_s", "side_e", "side_w", "top", "bottom"]:
    p = os.path.join(SRC, f"wool_{face}.png")
    img = Image.open(p).convert("RGB")
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            px[x, y] = (min(255, int(r * DYE[0] * 1.25)),
                        min(255, int(g * DYE[1] * 1.25)),
                        min(255, int(b * DYE[2] * 1.25)))
    img.save(p)
    print(f"dyed {p}")
print("Wool dyed madder red. Restart the engine (or reload_atlas) to see it.")
