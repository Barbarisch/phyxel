#!/usr/bin/env python3
"""Generate colored nature material textures from the existing oak set.

The engine's atlas blits source PNGs verbatim (no tint pass), so material color
must live in the PNG pixels. The stock leaf_*.png are grayscale structure masks
(they render GRAY in-game — the green colorTint in materials.json was intent
that never got wired). This script:

  1. FIXES the stock Leaf textures in place (colorizes the grayscale mask green).
  2. Derives new leaf color sets from the same mask:   LeafBirch, LeafSpruce,
     LeafJungle, LeafAutumn.
  3. Derives new bark sets from the colored oak log:   LogBirch (pale + dark
     streak bands), LogSpruce (dark cold brown).
  4. Appends the new material entries to resources/materials.json (physics
     copied from Log/Leaf; colorTint kept as documentation of intent).

Deterministic — safe to re-run (idempotent; existing entries are replaced).
Run from the repo root:  python tools/gen_nature_textures.py
After running: restart the engine (atlas is built at startup) or call the
reload_atlas MCP tool.
"""

import json
import os
import sys

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "resources", "textures", "source")
MATERIALS_JSON = os.path.join(REPO, "resources", "materials.json")

FACES = ["side_n", "side_s", "side_e", "side_w", "top", "bottom"]


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def colorize_mask(img, dark, light):
    """Map a grayscale structure mask onto a dark->light color ramp, keep alpha."""
    img = img.convert("RGBA")
    out = Image.new("RGBA", img.size)
    src = img.load()
    dst = out.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = src[x, y]
            v = (r + g + b) / (3 * 255.0)
            dst[x, y] = lerp(dark, light, v) + (a,)
    return out


def recolor(img, sat=1.0, val=1.0, hue_shift=0.0):
    """HSV-space recolor of an already-colored texture, keep alpha."""
    import colorsys
    img = img.convert("RGBA")
    out = Image.new("RGBA", img.size)
    src = img.load()
    dst = out.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = src[x, y]
            h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
            h = (h + hue_shift) % 1.0
            s = max(0.0, min(1.0, s * sat))
            v = max(0.0, min(1.0, v * val))
            r2, g2, b2 = colorsys.hsv_to_rgb(h, s, v)
            dst[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255), a)
    return out


def add_birch_streaks(img):
    """Horizontal dark streak bands — the classic birch bark look.

    Fixed pattern (deterministic): short dark dashes on a handful of rows.
    """
    img = img.convert("RGBA")
    dst = img.load()
    streak = (62, 58, 50)
    # (row, x_start, length) tuples chosen to read as birch at 64x64
    dashes = [
        (6, 4, 10), (6, 34, 7), (13, 20, 12), (13, 50, 9),
        (22, 0, 8), (22, 40, 14), (30, 12, 9), (30, 56, 8),
        (38, 28, 11), (45, 2, 7), (45, 44, 12), (53, 18, 8),
        (53, 52, 10), (60, 8, 12), (60, 36, 6),
    ]
    for row, x0, ln in dashes:
        for t in range(3):  # 3px tall bands
            y = row + t
            if y >= img.height:
                continue
            for x in range(x0, min(x0 + ln, img.width)):
                a = dst[x, y][3]
                dst[x, y] = streak + (a,)
    return img


def save_set(base_name, face_images):
    for face, img in face_images.items():
        path = os.path.join(SRC, f"{base_name}_{face}.png")
        img.save(path)
    print(f"  wrote {base_name}_*.png ({len(face_images)} faces)")


def load(name):
    return Image.open(os.path.join(SRC, name)).convert("RGBA")


def preserve_mask(name):
    """Keep a pristine copy of an in-place-colorized source so re-runs stay
    deterministic (otherwise each run re-derives from its own output and the
    colors drift). Returns the pristine image."""
    orig = os.path.join(SRC, name.replace(".png", "_mask_original.png"))
    cur = os.path.join(SRC, name)
    if not os.path.exists(orig):
        Image.open(cur).save(orig)
    return Image.open(orig).convert("RGBA")


def main():
    # ------------------------------------------------------------------ leaves
    leaf_mask = preserve_mask("leaf_side_n.png")  # stock leaf faces share one mask

    leaf_ramps = {
        # name           dark            light            tint (intent doc)
        None:            ((20, 64, 14),  (96, 186, 60)),   # stock Leaf fix (oak green)
        "leaf_birch":    ((44, 84, 22),  (148, 200, 90)),  # light yellow-green
        "leaf_spruce":   ((10, 44, 28),  (52, 110, 78)),   # dark blue-green needles
        "leaf_jungle":   ((12, 78, 16),  (70, 200, 50)),   # vivid saturated green
        "leaf_autumn":   ((96, 38, 8),   (228, 138, 36)),  # orange/red fall foliage
    }
    print("Leaf sets:")
    for name, (dark, light) in leaf_ramps.items():
        colored = colorize_mask(leaf_mask, dark, light)
        base = name if name else "leaf"
        save_set(base, {f: colored for f in FACES})

    # ------------------------------------------------------------- grass top
    # Same engine bug as the leaves: grass_top.png is a grayscale mask and the
    # green colorTint in materials.json is never applied to textured voxels, so
    # grass tops rendered gray-white. Colorize in place (sides already carry
    # the authored dirt+green-strip art and are left alone).
    grass_top = preserve_mask("grass_top.png")
    colorize_mask(grass_top, (40, 70, 22), (134, 182, 74)).save(
        os.path.join(SRC, "grass_top.png"))
    print("grass_top.png colorized green (in place; pristine mask kept)")

    # ------------------------------------------------------------------ barks
    log_sides = {f: load(f"log_{f}.png") for f in ["side_n", "side_s", "side_e", "side_w"]}
    log_top = load("log_top.png")
    log_bottom = load("log_bottom.png")

    print("Bark sets:")
    # Birch: strongly lightened + desaturated bark with dark streak bands;
    # pale end-grain rings.
    birch = {}
    for f, img in log_sides.items():
        birch[f] = add_birch_streaks(recolor(img, sat=0.18, val=2.6))
    birch["top"] = recolor(log_top, sat=0.30, val=1.5)
    birch["bottom"] = recolor(log_bottom, sat=0.30, val=1.5)
    save_set("log_birch", birch)

    # Spruce: darker, colder brown bark.
    spruce = {}
    for f, img in log_sides.items():
        spruce[f] = recolor(img, sat=1.1, val=0.55, hue_shift=-0.01)
    spruce["top"] = recolor(log_top, sat=1.05, val=0.7)
    spruce["bottom"] = recolor(log_bottom, sat=1.05, val=0.7)
    save_set("log_spruce", spruce)

    # ------------------------------------------------- materials.json entries
    with open(MATERIALS_JSON, encoding="utf-8") as f:
        data = json.load(f)
    mats = data["materials"]
    by_name = {m["name"]: m for m in mats}

    def derived(base, name, desc, tint, tex_base):
        entry = json.loads(json.dumps(by_name[base]))  # deep copy
        entry["name"] = name
        entry["description"] = desc
        entry["physics"]["colorTint"] = tint
        entry["textures"] = {f: f"{tex_base}_{f}.png" for f in FACES}
        return entry

    new_entries = [
        derived("Log", "LogBirch", "Pale birch log with dark streak bands",
                [0.82, 0.80, 0.72], "log_birch"),
        derived("Log", "LogSpruce", "Dark spruce log bark",
                [0.30, 0.21, 0.12], "log_spruce"),
        derived("Leaf", "LeafBirch", "Light yellow-green birch foliage",
                [0.55, 0.75, 0.33], "leaf_birch"),
        derived("Leaf", "LeafSpruce", "Dark blue-green spruce needles",
                [0.16, 0.40, 0.28], "leaf_spruce"),
        derived("Leaf", "LeafJungle", "Vivid jungle canopy foliage",
                [0.25, 0.75, 0.18], "leaf_jungle"),
        derived("Leaf", "LeafAutumn", "Orange autumn foliage",
                [0.85, 0.50, 0.13], "leaf_autumn"),
    ]

    added, replaced = 0, 0
    for e in new_entries:
        if e["name"] in by_name:
            mats[[m["name"] for m in mats].index(e["name"])] = e
            replaced += 1
        else:
            mats.append(e)
            added += 1

    with open(MATERIALS_JSON, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    print(f"materials.json: {added} added, {replaced} replaced "
          f"({len(mats)} total materials)")
    print("NOTE: restart the engine (or reload_atlas) to rebuild the atlas.")


if __name__ == "__main__":
    sys.exit(main())
