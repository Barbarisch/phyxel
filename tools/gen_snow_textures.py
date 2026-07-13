#!/usr/bin/env python3
"""Generate snow surface materials for cold biomes + the alpine snow line.

Replaces the "Ice" placeholder the terrain generator used for snow (Ice reads as
a glossy blue frozen lake — wrong for snowy ground). Two distinct, physically
grounded surfaces:

  SnowGrass  snow-DUSTED boreal / subarctic GROUND (the Snow biome surface).
             White crust over dirt on the sides; keeps its conifers.
  Snow       pure settled SNOWPACK (the lapse-rate alpine cap above the
             treeline, any biome). White on every face; blocks flora.

Both are MATTE: we deliberately ship NO `<face>_nr.png` sidecar, so the engine
atlas (AtlasManager.cpp) defaults each face to a flat normal + roughness 0.9.
Fresh/settled snow is a diffuse matte surface, NOT glossy like Ice — so the
default is exactly right and we don't fight it.

Like gen_biome_grass.py / gen_nature_textures.py, the engine atlas blits source
PNGs verbatim (no runtime tint pass), so all colour must live in the pixels.

Deterministic & idempotent (fixed noise seed, no system RNG). Run from repo root:
    python tools/gen_snow_textures.py
After running: restart the engine (atlas is built at startup) or call reload_atlas.
"""

import json
import math
import os
import sys

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "resources", "textures", "source")
MATERIALS_JSON = os.path.join(REPO, "resources", "materials.json")
FACES = ["side_n", "side_s", "side_e", "side_w", "top", "bottom"]
SIDES = ["side_n", "side_s", "side_e", "side_w"]
SIZE = 512  # match the terrain grass sources


def load(name):
    return Image.open(os.path.join(SRC, name)).convert("RGBA")


def save_set(base, faces):
    for f, img in faces.items():
        img.save(os.path.join(SRC, f"{base}_{f}.png"))
    print(f"  wrote {base}_*.png ({len(faces)} faces)")


# ---------------------------------------------------------------------------
# Deterministic value noise -> a matte snow field (fine sparkle + soft drifts).
# ---------------------------------------------------------------------------
def _hash01(ix, iy, seed):
    h = (ix * 374761393 + iy * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    h ^= h >> 16
    return (h & 0xFFFFFF) / float(0x1000000)


def _smooth(t):
    return t * t * (3.0 - 2.0 * t)


def _value_layer(size, cells, seed):
    """One octave of tileable value noise in [0,1] at `cells` resolution."""
    out = [[0.0] * size for _ in range(size)]
    step = size / cells
    for y in range(size):
        fy = y / step
        y0 = int(math.floor(fy)) % cells
        y1 = (y0 + 1) % cells
        ty = _smooth(fy - math.floor(fy))
        for x in range(size):
            fx = x / step
            x0 = int(math.floor(fx)) % cells
            x1 = (x0 + 1) % cells
            tx = _smooth(fx - math.floor(fx))
            v00 = _hash01(x0, y0, seed)
            v10 = _hash01(x1, y0, seed)
            v01 = _hash01(x0, y1, seed)
            v11 = _hash01(x1, y1, seed)
            top = v00 + (v10 - v00) * tx
            bot = v01 + (v11 - v01) * tx
            out[y][x] = top + (bot - top) * ty
    return out


def snow_face(seed, shadow=(212, 221, 233), lit=(250, 252, 255), sparkle=10):
    """Matte snow face: soft drift shading (cool-blue) + faint white sparkle.

    shadow/lit = the cool-white value ramp (snow's spectral albedo is very high
    and slightly blue in shadow). `sparkle` = amplitude of fine high-freq grains.
    """
    drift = _value_layer(SIZE, 8, seed)        # broad undulating drifts
    grain = _value_layer(SIZE, 96, seed + 101)  # fine snow crystal grain
    img = Image.new("RGBA", (SIZE, SIZE))
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            t = 0.72 * drift[y][x] + 0.28 * grain[y][x]
            r = int(shadow[0] + (lit[0] - shadow[0]) * t)
            g = int(shadow[1] + (lit[1] - shadow[1]) * t)
            b = int(shadow[2] + (lit[2] - shadow[2]) * t)
            s = int((grain[y][x] - 0.5) * 2 * sparkle)  # +/- sparkle
            r = max(0, min(255, r + s))
            g = max(0, min(255, g + s))
            b = max(0, min(255, b + s))
            px[x, y] = (r, g, b, 255)
    return img


def crust_side(side_img, snow_top, crust_frac=0.40):
    """Snow-dusted dirt side: bare dirt below, a snow crust blended over the top
    band (a subarctic ground profile: frozen soil with snow lying on top). The
    snow line is perturbed by 2D value noise so the boundary is ragged, not a
    clean horizontal cut -- and NOT vertically streaked (a per-column-only noise
    would band vertically)."""
    side = side_img.convert("RGBA").resize((SIZE, SIZE))
    snow = snow_top.resize((SIZE, SIZE))
    edge = _value_layer(SIZE, 20, 7)  # 2D, so the ragged edge varies in x AND y
    out = Image.new("RGBA", (SIZE, SIZE))
    d, s, o = side.load(), snow.load(), out.load()
    band = SIZE * crust_frac
    for y in range(SIZE):
        for x in range(SIZE):
            dr, dg, db, da = d[x, y]
            # Local snow-line depth wobbles +/-30% via 2D noise -> ragged edge.
            local = band * (0.7 + 0.6 * edge[y][x])
            if y < local:
                # Solid snow near the top, fading to bare dirt at the ragged line.
                a = min(1.0, (1.0 - y / local) * 1.8)
                sr, sg, sb, _ = s[x, y]
                o[x, y] = (
                    int(dr + (sr - dr) * a),
                    int(dg + (sg - dg) * a),
                    int(db + (sb - db) * a),
                    da,
                )
            else:
                o[x, y] = (dr, dg, db, da)
    return out


# ---------------------------------------------------------------------------
# Physics/appearance numbers -- reconciled with a grounding-auditor pass.
# We model SETTLED seasonal snowpack (not powder, not glacial firn). Sources:
#   density   settled snow 200-400 kg/m^3 vs Ice 917 (mass 0.9) -> ~0.30
#             (NSIDC firn glossary; UBC ATSC113 snow-density compilation)
#   friction  boot-on-packed-snow kinetic COF ~0.2, clearly ABOVE smooth ice 0.1
#             (Gao & Abeysekera 2004, Ergonomics 47(5):573-598)
#   restit.   snow is plastic/absorptive: e~0.1 (near Dirt), NOT Ice's 0.4
#             (snow-snow granular e~=0.141, avalanche-flow literature)
#   albedo    fresh snow spectral albedo ~0.96-0.99, near-NEUTRAL with only a
#             slight blue bias -- Ice's strong [0.8,0.9,1.0] blue is glacial
#             bulk-volume absorption, NOT snow's shallow-scattering grains
#             (Warren 1982; Wiscombe & Warren 1980)
#   roughness matte, near-Lambertian scattering -> 0.85-0.95 band (authored
#             PBR choice bounded by Warren 1982 / Painter & Dozier 2004);
#             also enforced by shipping no _nr sidecar (engine default 0.9).
# A thin snow dusting is mechanically SUBSTRATE-dominated, so SnowGrass takes
# frozen-soil (Dirt) mass/restitution rather than an average with Snow.
# SnowGrass friction/roughness/tint are bounded-by-analogy / authored (labeled).
# ---------------------------------------------------------------------------
SNOW = {
    "matname": "Snow",
    "desc": "Settled snowpack (alpine snow line / permanent snow above treeline)",
    "mass": 0.30,          # settled snowpack density vs Ice 0.9
    "friction": 0.20,      # packed-snow kinetic COF, above ice's 0.1
    "restitution": 0.10,   # inelastic/absorptive, ~Dirt (reject Ice's 0.4)
    "roughness": 0.95,     # matte (bounded-by-analogy; also default via no _nr)
    "tint": [0.94, 0.96, 0.99],  # near-neutral, slight blue (Warren 1982)
}
SNOWGRASS = {
    "matname": "SnowGrass",
    "desc": "Snow-dusted subarctic/boreal ground (Snow biome surface)",
    "mass": 2.0,           # substrate-dominated == Dirt (thin dusting)
    "friction": 0.60,      # ~frozen-soil grip, minor snow-film cut (analogy)
    "restitution": 0.10,   # == Dirt substrate
    "roughness": 0.90,     # analogy (between Grass 0.9 and Snow 0.95)
    "tint": [0.90, 0.92, 0.96],  # authored near-neutral cool blend
}


def build_material(base_grass, cfg, textures):
    entry = json.loads(json.dumps(base_grass))  # deep copy of Grass shape
    entry["name"] = cfg["matname"]
    entry["description"] = cfg["desc"]
    p = entry["physics"]
    p["mass"] = cfg["mass"]
    p["friction"] = cfg["friction"]
    p["restitution"] = cfg["restitution"]
    p["roughness"] = cfg["roughness"]
    p["metallic"] = 0.0
    p["colorTint"] = cfg["tint"]
    entry["textures"] = textures
    # Snow reads best WITHOUT per-voxel hash rotation: `varied` breaks the soft
    # continuous drift pattern at voxel edges. Keep it off (see CLAUDE.md rule).
    entry["varied"] = False
    return entry


def main():
    # ---- textures ----
    print("Snow textures:")
    # Pure snowpack: independent noise seed per face so a cube's faces aren't clones.
    snow_faces = {f: snow_face(seed=1000 + i) for i, f in enumerate(FACES)}
    save_set("snow", snow_faces)

    # Snow-dusted ground: snow top, dirt sides with a snow crust, dirt bottom.
    sg_top = snow_face(seed=2000)
    side_srcs = {f: load(f"grass_{f}.png") for f in SIDES}
    dirt_bottom = load("grass_bottom.png").resize((SIZE, SIZE))
    sg_faces = {"top": sg_top, "bottom": dirt_bottom}
    for f in SIDES:
        sg_faces[f] = crust_side(side_srcs[f], sg_top)
    save_set("snowgrass", sg_faces)

    # ---- materials.json ----
    with open(MATERIALS_JSON, encoding="utf-8") as f:
        data = json.load(f)
    mats = data["materials"]
    by_name = {m["name"]: m for m in mats}
    grass = by_name["Grass"]

    added = replaced = 0
    for cfg in (SNOW, SNOWGRASS):
        base = "snow" if cfg["matname"] == "Snow" else "snowgrass"
        textures = {f: f"{base}_{f}.png" for f in FACES}
        entry = build_material(grass, cfg, textures)
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
