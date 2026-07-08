#!/usr/bin/env python3
"""
fetch_polyhaven.py — source Phyxel voxel material textures from Poly Haven (CC0).

Companion to fetch_cc0_textures.py (ambientCG): same output contract — per-face albedo
PNGs (<material>_<face>.png) plus normal+roughness sidecars (<face>_nr.png) in
resources/textures/source/, PBR maps cached in resources/textures/pbr/, provenance
recorded in CC0_SOURCES.json (source: "Poly Haven").

Extra feature for ROOF materials: `courses` crop. Stepped voxel roofs expose the tile in
1/3-voxel subcube steps, so course lines must land on the 1/3 grid — the tile must hold a
multiple-of-3 number of horizontal courses. For those assets we crop the seamless photo
tile to `crop_frac` of its height (chosen per asset so the crop holds a multiple-of-3
course count and cuts on course lines), then feather the new wrap seam.

Usage:
    python tools/fetch_polyhaven.py               # fetch all mapped materials
    python tools/fetch_polyhaven.py Slate Thatch  # only specific materials
    python tools/fetch_polyhaven.py --list

Requires: Pillow. Restart the engine (or reload_atlas) after running.
"""
import io
import json
import os
import sys
import urllib.request

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MATERIALS_JSON = os.path.join(REPO, "resources", "materials.json")
SOURCE_DIR = os.path.join(REPO, "resources", "textures", "source")
PBR_DIR = os.path.join(REPO, "resources", "textures", "pbr")
CACHE_DIR = os.path.join(REPO, ".texwork", "ph_cache")
PROVENANCE = os.path.join(SOURCE_DIR, "CC0_SOURCES.json")

UA = "PhyxelTextureBot/1.0 (bpeterson926@gmail.com)"
FILES_API = "https://api.polyhaven.com/files/{id}"
FETCH_RES = "2k"          # download res; each material is written at its materials.json res

# material -> Poly Haven asset + optional course crop. "crop_px" = (y0, y1) in FETCH_RES
# pixel rows, measured (tools: row-luminance autocorrelation) so the kept region holds a
# multiple-of-3 course count and both cuts land on course-gap lines. It also strips
# non-repeating photo features (the slate's metal ridge flashing, the clay ridge course).
ASSETS = {
    # Large rough-hewn dressed blocks — the "ashlar" wall role (replaces ambientCG Bricks089).
    "StoneBricks":  {"asset": "medieval_blocks_03"},
    # Roofs: courses per kept tile noted; each subcube roof step shows courses/3 of them.
    "Slate":        {"asset": "roof_slates_03",     "crop_px": (136, 1840)},  # 24 courses, drops flashing
    "StoneSlab":    {"asset": "grey_roof_tiles_02", "crop_px": (12, 1686)},   # 9 courses
    "WoodShingle":  {"asset": "roof_slates_02",     "crop_px": (3, 1875)},    # 24 courses
    "ClayTile":     {"asset": "ceramic_roof_01",    "crop_px": (55, 1567)},   # 9 courses, drops ridge+eave
    "Thatch":       {"asset": "thatch_roof_angled"},   # no crisp courses; no crop needed
}

FACE_GROUPS = ["side_n", "side_s", "side_e", "side_w", "top", "bottom"]


def http_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def download_map(asset_id, map_key):
    """Download (cached) one map (Diffuse/nor_gl/Rough) as a PIL image, or None."""
    cache = os.path.join(CACHE_DIR, f"{asset_id}_{map_key}_{FETCH_RES}.png")
    if os.path.exists(cache) and os.path.getsize(cache) > 0:
        return Image.open(cache).convert("RGBA")
    files = http_json(FILES_API.format(id=asset_id))
    if map_key not in files:
        return None
    entry = files[map_key].get(FETCH_RES) or files[map_key].get("1k")
    fmt = entry.get("png") or entry.get("jpg")
    url = fmt["url"]
    print(f"    downloading {url}")
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=180) as r:
        blob = r.read()
    img = Image.open(io.BytesIO(blob)).convert("RGBA")
    os.makedirs(CACHE_DIR, exist_ok=True)
    img.save(cache)
    return img


def course_crop(img, crop_px, feather=16):
    """Crop rows [y0, y1) (both on course-gap lines) and feather the new wrap seam.

    Because both cuts land in the dark course gap, the bottom edge meets the top edge
    gap-to-gap when tiled; the feather just blends the last rows toward the first rows
    to hide any residual luminance step."""
    if not crop_px:
        return img
    y0, y1 = crop_px
    w, h = img.size
    out = img.crop((0, y0, w, y1))
    kh = y1 - y0
    px = out.load()
    top = out.crop((0, 0, w, 1)).load()
    for i in range(feather):
        y = kh - feather + i
        a = (i + 1) / (feather + 1) * 0.5    # blend up to 50% into the wrap row
        for x in range(w):
            b, t = px[x, y], top[x, 0]
            px[x, y] = tuple(int(bc * (1 - a) + tc * a) for bc, tc in zip(b, t))
    return out


def make_nr(normal, rough, size):
    if normal is None:
        nr = Image.new("RGBA", (size, size), (128, 128, 255, 230))
        return nr
    nr = normal.convert("RGB").convert("RGBA")
    alpha = rough.convert("L") if rough is not None else Image.new("L", nr.size, 230)
    nr.putalpha(alpha)
    return nr


def load_materials_meta():
    data = json.load(open(MATERIALS_JSON))
    out = {}
    for mat in data["materials"]:
        out[mat["name"]] = {
            "faces": dict(mat.get("textures", {})),
            "resolution": int(mat.get("resolution", 512)),
        }
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--list" in sys.argv:
        for m, cfg in ASSETS.items():
            print(f"{m:14} {cfg}")
        return
    targets = args if args else list(ASSETS.keys())
    meta = load_materials_meta()
    os.makedirs(SOURCE_DIR, exist_ok=True)
    os.makedirs(PBR_DIR, exist_ok=True)
    provenance = json.load(open(PROVENANCE)) if os.path.exists(PROVENANCE) else {}

    for mat in targets:
        if mat not in ASSETS:
            print(f"[skip] no mapping for '{mat}'")
            continue
        if mat not in meta:
            print(f"[skip] '{mat}' not in materials.json")
            continue
        cfg = ASSETS[mat]
        asset = cfg["asset"]
        res = meta[mat]["resolution"]
        print(f"[{mat}] <- {asset}")
        albedo = download_map(asset, "Diffuse")
        normal = download_map(asset, "nor_gl")
        rough = download_map(asset, "Rough")
        crop = cfg.get("crop_px")
        albedo = course_crop(albedo, crop)
        if normal is not None:
            normal = course_crop(normal, crop)
        if rough is not None:
            rough = course_crop(rough, crop)
        for k, img in (("normal", normal), ("roughness", rough)):
            if img is not None:
                img.convert("RGB").resize((512, 512), Image.LANCZOS).save(
                    os.path.join(PBR_DIR, f"{asset}_{k}.png"))
        albedo = albedo.resize((res, res), Image.LANCZOS)
        nr = make_nr(normal, rough, res).resize((res, res), Image.LANCZOS)
        for face in FACE_GROUPS:
            fname = meta[mat]["faces"].get(face)
            if not fname:
                print(f"    [warn] no '{face}' filename for {mat}")
                continue
            albedo.convert("RGB").save(os.path.join(SOURCE_DIR, fname))
            nr.save(os.path.join(SOURCE_DIR, os.path.splitext(fname)[0] + "_nr.png"))
        provenance[mat] = {"assets": {"all": asset}, "license": "CC0",
                           "source": "Poly Haven", "resolution": res,
                           **({"crop_px": list(crop)} if crop else {})}
        print(f"    wrote faces @ {res}px")

    json.dump(provenance, open(PROVENANCE, "w"), indent=2, sort_keys=True)
    print(f"\nDone. Provenance -> {PROVENANCE}")
    print("Restart the engine (or reload_atlas) to reload textures.")


if __name__ == "__main__":
    main()
