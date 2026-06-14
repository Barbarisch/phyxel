#!/usr/bin/env python3
"""Generate visually distinct grass surface materials for biomes.

Plains keeps the stock green "Grass". This derives extra grass sets so biomes
read differently at a glance:
  - GrassForest   deep, slightly blue-green
  - GrassSavanna  dry golden / tan grass

Like gen_nature_textures.py, the engine atlas blits source PNGs verbatim (no
tint pass) so color must live in the pixels. We:
  1. Recolor the grass TOP from the pristine mask (grass_top_mask_original.png)
     onto a per-biome dark->light ramp.
  2. Recolor only the GREEN vegetation strip on the side faces (keep the dirt).
  3. Reuse grass_bottom.png (dirt) unchanged.
  4. Append/replace the material entries in resources/materials.json.

Deterministic & idempotent. Run from repo root: python tools/gen_biome_grass.py
After running: restart the engine (atlas built at startup) or reload_atlas.
"""

import colorsys
import json
import os
import sys

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "resources", "textures", "source")
MATERIALS_JSON = os.path.join(REPO, "resources", "materials.json")
FACES = ["side_n", "side_s", "side_e", "side_w", "top", "bottom"]
SIDES = ["side_n", "side_s", "side_e", "side_w"]


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def colorize_mask(img, dark, light):
    """Grayscale structure mask -> dark..light color ramp, keep alpha."""
    img = img.convert("RGBA")
    out = Image.new("RGBA", img.size)
    src, dst = img.load(), out.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = src[x, y]
            v = (r + g + b) / (3 * 255.0)
            dst[x, y] = lerp(dark, light, v) + (a,)
    return out


def recolor_green(img, hue_shift=0.0, sat=1.0, val=1.0):
    """HSV-recolor only the green (vegetation) pixels; leave dirt/brown alone."""
    img = img.convert("RGBA")
    out = Image.new("RGBA", img.size)
    src, dst = img.load(), out.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = src[x, y]
            if g > r + 8 and g > b + 8:  # greenish vegetation pixel
                h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
                h = (h + hue_shift) % 1.0
                s = max(0.0, min(1.0, s * sat))
                v = max(0.0, min(1.0, v * val))
                r2, g2, b2 = colorsys.hsv_to_rgb(h, s, v)
                dst[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255), a)
            else:
                dst[x, y] = (r, g, b, a)
    return out


def load(name):
    return Image.open(os.path.join(SRC, name)).convert("RGBA")


def save_set(base, faces):
    for f, img in faces.items():
        img.save(os.path.join(SRC, f"{base}_{f}.png"))
    print(f"  wrote {base}_*.png ({len(faces)} faces)")


VARIANTS = {
    "grass_forest": {
        "matname": "GrassForest",
        "desc": "Deep forest grass",
        "top_ramp": ((16, 48, 20), (54, 116, 50)),
        "side": dict(hue_shift=0.0, sat=1.15, val=0.62),
        "tint": [0.18, 0.42, 0.18],
    },
    "grass_savanna": {
        "matname": "GrassSavanna",
        "desc": "Dry golden savanna grass",
        # Saturated wheat-gold with strong contrast (olive/low-contrast read as muddy).
        "top_ramp": ((128, 96, 28), (236, 206, 96)),
        "side": dict(hue_shift=-0.12, sat=0.90, val=1.22),
        "tint": [0.72, 0.62, 0.26],
    },
}


def main():
    grass_mask = os.path.join(SRC, "grass_top_mask_original.png")
    if not os.path.exists(grass_mask):
        print("ERROR: grass_top_mask_original.png missing — run gen_nature_textures.py first.")
        return 1
    mask = Image.open(grass_mask).convert("RGBA")
    side_imgs = {f: load(f"grass_{f}.png") for f in SIDES}
    bottom = load("grass_bottom.png")

    print("Grass variants:")
    for base, cfg in VARIANTS.items():
        faces = {}
        faces["top"] = colorize_mask(mask, *cfg["top_ramp"])
        for f in SIDES:
            faces[f] = recolor_green(side_imgs[f], **cfg["side"])
        faces["bottom"] = bottom.copy()
        save_set(base, faces)

    # ----- materials.json -----
    with open(MATERIALS_JSON, encoding="utf-8") as f:
        data = json.load(f)
    mats = data["materials"]
    by_name = {m["name"]: m for m in mats}
    grass = by_name["Grass"]

    added = replaced = 0
    for base, cfg in VARIANTS.items():
        entry = json.loads(json.dumps(grass))  # deep copy of Grass
        entry["name"] = cfg["matname"]
        entry["description"] = cfg["desc"]
        entry["physics"]["colorTint"] = cfg["tint"]
        entry["textures"] = {f: f"{base}_{f}.png" for f in FACES}
        if cfg["matname"] in by_name:
            mats[[m["name"] for m in mats].index(cfg["matname"])] = entry
            replaced += 1
        else:
            mats.append(entry)
            added += 1

    with open(MATERIALS_JSON, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    print(f"materials.json: {added} added, {replaced} replaced ({len(mats)} total)")
    print("NOTE: restart the engine (or reload_atlas) to rebuild the atlas.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
