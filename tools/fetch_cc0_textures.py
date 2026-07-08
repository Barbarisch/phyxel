#!/usr/bin/env python3
"""
fetch_cc0_textures.py — Re-source Phyxel voxel material textures from ambientCG (CC0).

Downloads 1K PNG material bundles from ambientCG, extracts the albedo (Color) map,
downscales to 512x512, and writes per-face source PNGs into resources/textures/source/
(<material>_<face>.png) consumed by AtlasManager. The accompanying PBR maps
(NormalGL / Roughness / AmbientOcclusion) are cached at 512 under
resources/textures/pbr/ for the Phase 2 PBR shading work.

All ambientCG assets are CC0 (public domain) — no attribution required, but the chosen
asset IDs + license are recorded in resources/textures/source/CC0_SOURCES.json for
provenance.

Usage:
    python tools/fetch_cc0_textures.py            # fetch all mapped materials
    python tools/fetch_cc0_textures.py Stone Wood # only specific materials
    python tools/fetch_cc0_textures.py --list     # print the material->asset map

Requires: Pillow (pip install pillow). Restart the engine after running to reload textures.
"""
import io
import json
import os
import sys
import zipfile
import urllib.request

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MATERIALS_JSON = os.path.join(REPO, "resources", "materials.json")
SOURCE_DIR = os.path.join(REPO, "resources", "textures", "source")
PBR_DIR = os.path.join(REPO, "resources", "textures", "pbr")
CACHE_DIR = os.path.join(REPO, ".texwork", "acg_cache")
PROVENANCE = os.path.join(SOURCE_DIR, "CC0_SOURCES.json")

# Albedo is cached at the hi-res class size; each material is written at its own
# resolution (materials.json "resolution": 512 default or 1024 for hi-res/objects).
ALBEDO_NATIVE = 1024
PBR_SIZE = 512
UA = "PhyxelTextureBot/1.0 (bpeterson926@gmail.com)"
API = "https://ambientcg.com/api/v2/full_json?id={id}&type=Material&include=downloadData"

# Per-face mapping. Values are ambientCG asset IDs.
#   "all"    -> all 6 faces        "side" -> the 4 side faces
#   "top" / "bottom"               (most natural-stone tiles use one tile for everything)
# Grass uses a grass tile on top and dirt on the sides/bottom (Minecraft convention) — a
# pure grass tile on a vertical face reads as grass growing sideways.
ASSETS = {
    "Dirt":         {"all": "Ground003"},
    "Grass":        {"top": "Grass004", "side": "Ground003", "bottom": "Ground003"},
    "Stone":        {"all": "Rock030"},
    # Rounded street setts with grassy joints — reads as cobbles (was misassigned to StoneBricks
    # until 2026-07-07; PavingStones128's smooth cut tiles now live on StoneTiles).
    "Cobblestone":  {"all": "PavingStones070"},
    # StoneBricks moved to Poly Haven (medieval_blocks_03) — see tools/fetch_polyhaven.py.
    # Smooth rectangular cut-stone tiles — interior floors (finish_forge).
    "StoneTiles":   {"all": "PavingStones128"},
    "Sand":         {"all": "Ground027"},
    "Gravel":       {"all": "Gravel022"},
    "Wood":         {"all": "WoodFloor007"},
    # Furniture hardwood — dark espresso walnut, distinct from the light oak floor (Wood).
    # Wood051 is CC0 and explicitly "furniture"-tagged; its roughness map reads polished, so
    # materials.json overrides roughness to 0.60 for the oiled/waxed medieval look.
    "WoodWalnut":   {"all": "Wood051"},
    "Bricks":       {"all": "Bricks097"},
    "Sandstone":    {"all": "Rock035"},
    "Metal":        {"all": "MetalPlates006"},
    "Ice":          {"all": "Ice001"},
    # Logs -> CC0 bark (sides + caps share the bark tile; distinct assets differentiate species).
    "Log":          {"all": "Bark007"},
    # LogBirch: Bark011 is only the structural base — no true birch bark exists on ambientCG.
    # tools/gen_birch_bark.py post-processes the fetched faces into white paper bark with
    # lenticel bands. Re-run it after re-fetching LogBirch or the bark reverts to grey-green.
    "LogBirch":     {"all": "Bark011"},
    "LogSpruce":    {"all": "Bark006"},
    "LogPine":      {"all": "Bark004"},
    "LogJungle":    {"all": "Bark005"},
    "LogPalm":      {"all": "Bark002"},
    "LogRedwood":   {"all": "Bark012"},
    # Biome grass variants: same layout as Grass (shader currently ignores colorTint, so
    # these share the base grass/dirt tiles to stay sharp rather than upscaled-blurry).
    "GrassForest":  {"top": "Grass004", "side": "Ground003", "bottom": "Ground003"},
    "GrassSavanna": {"top": "Grass004", "side": "Ground003", "bottom": "Ground003"},
}

FACE_GROUPS = {
    "all":    ["side_n", "side_s", "side_e", "side_w", "top", "bottom"],
    "side":   ["side_n", "side_s", "side_e", "side_w"],
    "top":    ["top"],
    "bottom": ["bottom"],
}

# ambientCG map suffix -> (local PBR map name). Color is handled separately (albedo).
PBR_MAPS = {
    "NormalGL":          "normal",
    "Roughness":         "roughness",
    "AmbientOcclusion":  "ao",
}


def http_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def download_zip(asset_id):
    """Download (cached) the 1K-PNG bundle for an asset; return the zip bytes."""
    cache = os.path.join(CACHE_DIR, f"{asset_id}_1K-PNG.zip")
    if os.path.exists(cache) and os.path.getsize(cache) > 0:
        return open(cache, "rb").read()
    data = http_json(API.format(id=asset_id))
    if data.get("numberOfResults", 0) < 1:
        raise RuntimeError(f"asset '{asset_id}' not found on ambientCG")
    cats = data["foundAssets"][0]["downloadFolders"]["default"]["downloadFiletypeCategories"]
    link = None
    for dl in cats.get("zip", {}).get("downloads", []):
        if "1K-PNG" in dl["downloadLink"]:
            link = dl["downloadLink"]
            break
    if not link:
        raise RuntimeError(f"no 1K-PNG bundle for '{asset_id}'")
    print(f"    downloading {link}")
    req = urllib.request.Request(link, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=120) as r:
        blob = r.read()
    os.makedirs(CACHE_DIR, exist_ok=True)
    open(cache, "wb").write(blob)
    return blob


def extract_maps(asset_id):
    """Return {'albedo': PIL.Image@512, 'normal':..., 'roughness':..., 'ao':...} for an asset."""
    blob = download_zip(asset_id)
    zf = zipfile.ZipFile(io.BytesIO(blob))
    out = {}
    for name in zf.namelist():
        if not name.lower().endswith(".png"):
            continue
        base = os.path.basename(name)
        if base.endswith("_Color.png"):
            key = "albedo"
        else:
            key = None
            for suf, local in PBR_MAPS.items():
                if base.endswith(f"_{suf}.png"):
                    key = local
                    break
            if key is None:
                continue
        img = Image.open(io.BytesIO(zf.read(name))).convert("RGBA")
        # Keep everything at the hi-res class size; per-material resize happens at write time.
        out[key] = img.resize((ALBEDO_NATIVE, ALBEDO_NATIVE), Image.LANCZOS)
    if "albedo" not in out:
        raise RuntimeError(f"no _Color map in '{asset_id}' bundle")
    return out


def make_nr(maps):
    """Pack a normal+roughness image: RGB = tangent-space normal, A = roughness.
    Sampled in linear space by the shader (BC7-UNORM on the GPU)."""
    normal = maps.get("normal")
    if normal is None:
        # Flat normal (0,0,1) -> (128,128,255); roughness 0.9.
        nr = Image.new("RGBA", (ALBEDO_NATIVE, ALBEDO_NATIVE), (128, 128, 255, 230))
        return nr
    nr = normal.convert("RGB")
    rough = maps.get("roughness")
    alpha = rough.convert("L") if rough is not None else Image.new("L", nr.size, 230)
    nr = nr.convert("RGBA")
    nr.putalpha(alpha)
    return nr


def load_materials_meta():
    """material name -> {"faces": {face: filename}, "resolution": int} from materials.json."""
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
    provenance = {}
    if os.path.exists(PROVENANCE):
        provenance = json.load(open(PROVENANCE))

    asset_cache = {}  # asset_id -> extracted maps

    def get(asset_id):
        if asset_id not in asset_cache:
            asset_cache[asset_id] = extract_maps(asset_id)
            # cache PBR maps to disk for Phase 2
            for k in ("normal", "roughness", "ao"):
                if k in asset_cache[asset_id]:
                    asset_cache[asset_id][k].save(os.path.join(PBR_DIR, f"{asset_id}_{k}.png"))
        return asset_cache[asset_id]

    for mat in targets:
        if mat not in ASSETS:
            print(f"[skip] no mapping for '{mat}'")
            continue
        print(f"[{mat}]")
        if mat not in meta:
            print(f"    [skip] '{mat}' not in materials.json")
            continue
        res = meta[mat]["resolution"]
        faces = meta[mat]["faces"]
        used = {}
        for group, asset_id in ASSETS[mat].items():
            maps = get(asset_id)
            albedo = maps["albedo"]
            nr = make_nr(maps)  # normal (RGB) + roughness (A)
            if albedo.size[0] != res:
                albedo = albedo.resize((res, res), Image.LANCZOS)
            if nr.size[0] != res:
                nr = nr.resize((res, res), Image.LANCZOS)
            for face in FACE_GROUPS[group]:
                fname = faces.get(face)
                if not fname:
                    print(f"    [warn] no '{face}' filename for {mat}")
                    continue
                albedo.save(os.path.join(SOURCE_DIR, fname))
                # Normal+roughness sidecar: <albedo-base>_nr.png
                base = os.path.splitext(fname)[0]
                nr.save(os.path.join(SOURCE_DIR, base + "_nr.png"))
            used[group] = asset_id
        provenance[mat] = {"assets": used, "license": "CC0", "source": "ambientCG", "resolution": res}
        print(f"    wrote faces @ {res}px from {used}")

    json.dump(provenance, open(PROVENANCE, "w"), indent=2, sort_keys=True)
    print(f"\nDone. Provenance -> {PROVENANCE}")
    print("Restart the engine to reload textures.")


if __name__ == "__main__":
    main()
