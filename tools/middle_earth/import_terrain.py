#!/usr/bin/env python3
"""
Middle-earth map -> Phyxel Layer-0 terrain importer  (READ-ONLY reference use).

Assembles the mod's 8x8 i_3 tile pyramid (24000x24000 px, PIXEL_WEIGHT=4 -> a
96000x96000-block continent) and decodes it into Phyxel-native coarse fields that
CoarseWorldModel can bilinearly sample:

  me_height_<N>.png   16-bit grayscale = Phyxel world-Y base elevation (sea @ seaLevelY)
                      Decoded from the GREEN channel (mod's mountain/relief master),
                      with ocean regions from the biome map pushed below sea level.
  me_biome_<N>.png    8-bit palette index into the biome legend
  me_biome_legend.json  index -> {rgb, temperature, moisture, isOcean}  (178 regions)
  me_terrain_meta.json  scale + remap constants for the C++ SourceFunc

Mod ground truth (javap of MiddleEarthMapConfigs / MiddleEarthHeightMap):
  PIXEL_WEIGHT=4  REGION_SIZE=3000  MAP_ITERATION=3  -> FULL_MAP_SIZE=24000 px
  sea 'HEIGHT'=62 ; green -> layered-Perlin mountain amplitude (PERLIN_HEIGHT_RANGE=53,
  exponential above MOUNTAIN_START_HEIGHT=32) ; blue/red -> base+water (WATER_MULTIPLIER=0.65).

We remap into PHYXEL's scale (kSeaLevelY=16). Green 0..255 -> Phyxel Y via a linear
'heightScale' (tunable in meta): Y = seaLevelY + (green/255)*heightScale. Set heightScale
so the tallest ranges (~green 227) land near Phyxel's grandest-peak budget (~+384).

Usage:  python me_terrain_import.py [--downsample N] [--height-scale H]
  --downsample 8  -> fast 3000x3000 verification pass (default)
  --downsample 1  -> full 24000x24000 (writes ~1GB-class arrays; slower)
"""
import zipfile, io, json, os, sys, argparse
import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None  # allow >178Mpx

# Source mod is ARR + Tolkien IP; decoded terrain is a personal reference/test-bed only.
INNER = "assets/middle-earth/textures/map_data/1.0.0-1.21.8-beta.zip"
MAPPNG = "assets/middle-earth/textures/map.png"

# mod + phyxel constants
PIXEL_WEIGHT = 4
REGION = 3000
GRID   = 8                 # 2^MAP_ITERATION
FULL   = REGION * GRID     # 24000
PHYXEL_SEA = 16            # kSeaLevelY

# --- biome color -> climate legend --------------------------------------------
# We derive climate (temp/moisture) heuristically from region hue so Phyxel biome
# selection has something to key on. Ocean detected by strong-blue hue.
def build_legend(map_rgb):
    colors, counts = np.unique(map_rgb.reshape(-1, 3), axis=0, return_counts=True)
    order = np.argsort(-counts)
    legend = []
    for i in order:
        r, g, b = [int(v) for v in colors[i]]
        is_ocean = (b > 120 and b > r + 30 and b > g + 10)
        is_snow  = (r > 200 and g > 200 and b > 200)
        is_desert= (r > 190 and g > 170 and b < 140)
        # crude climate proxies in [0,1]
        temp = 0.05 if is_snow else (0.9 if is_desert else min(1.0, max(0.0, (r + b) / 400.0)))
        moist = 0.9 if is_ocean else min(1.0, max(0.0, g / 255.0))
        legend.append(dict(rgb=[r, g, b], count=int(counts[i]),
                           temperature=round(temp, 3), moisture=round(moist, 3),
                           isOcean=bool(is_ocean), isSnow=bool(is_snow)))
    return legend

def color_to_index(rgb_tile, legend_colors):
    """Map each pixel to nearest legend color. Exact matches via dict; rare misses
    resolved by nearest on the small set of unique missed colors."""
    h, w, _ = rgb_tile.shape
    flat = rgb_tile.reshape(-1, 3).astype(np.int16)
    key = (flat[:, 0].astype(np.int32) << 16) | (flat[:, 1] << 8) | flat[:, 2]
    lut = {}
    for idx, c in enumerate(legend_colors):
        lut[(int(c[0]) << 16) | (int(c[1]) << 8) | int(c[2])] = idx
    out = np.full(key.shape, -1, dtype=np.int32)
    uk, inv = np.unique(key, return_inverse=True)
    resolved = np.empty(uk.shape, dtype=np.int32)
    lc = np.array(legend_colors, dtype=np.int32)
    for j, k in enumerate(uk):
        if k in lut:
            resolved[j] = lut[k]
        else:
            r = (k >> 16) & 255; g = (k >> 8) & 255; b = k & 255
            d = ((lc[:, 0] - r) ** 2 + (lc[:, 1] - g) ** 2 + (lc[:, 2] - b) ** 2)
            resolved[j] = int(np.argmin(d))
    out = resolved[inv]
    return out.reshape(h, w).astype(np.uint16)

def load_tiles(iz, kind):
    """Return dict (col,row)->3000x3000x3 uint8 (or None missing)."""
    tiles = {}
    for n in iz.namelist():
        if f"/{kind}/i_3/" in n and n.endswith(".png"):
            fn = n.split("/")[-1][:-4]
            cx, cy = fn.split("_"); cx, cy = int(cx), int(cy)
            tiles[(cx, cy)] = n
    return tiles

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jar", required=True, help="path to the Middle-earth mod .jar")
    ap.add_argument("--out", default="me_terrain", help="output directory")
    ap.add_argument("--downsample", type=int, default=8)
    ap.add_argument("--height-scale", type=float, default=420.0,
                    help="green 255 -> seaLevelY + this many Phyxel Y units")
    ap.add_argument("--ocean-depth", type=float, default=24.0)
    args = ap.parse_args()
    ds = args.downsample
    OUT = args.out
    os.makedirs(OUT, exist_ok=True)

    z = zipfile.ZipFile(args.jar)
    map_rgb = np.array(Image.open(io.BytesIO(z.read(MAPPNG))).convert("RGB"))
    legend = build_legend(map_rgb)
    legend_colors = [e["rgb"] for e in legend]
    with open(os.path.join(OUT, "me_biome_legend.json"), "w") as f:
        json.dump(legend, f, indent=1)
    print(f"legend: {len(legend)} regions")

    iz = zipfile.ZipFile(io.BytesIO(z.read(INNER)))
    htiles = load_tiles(iz, "heights")
    btiles = load_tiles(iz, "biomes")

    # ceil so non-divisor downsamples (e.g. 16, since 3000/16=187.5) stay consistent:
    # a strided tile t[::ds] has exactly ceil(REGION/ds) rows.
    tsz = (REGION + ds - 1) // ds
    outsz = GRID * tsz
    green = np.zeros((outsz, outsz), dtype=np.uint8)   # relief master
    blue  = np.zeros((outsz, outsz), dtype=np.uint8)   # base/water
    bidx  = np.zeros((outsz, outsz), dtype=np.uint8)   # biome index (<256 regions)
    ocean = np.zeros((outsz, outsz), dtype=bool)
    ocean_flag = np.array([e["isOcean"] for e in legend], dtype=bool)

    # tile filename orientation: assemble as world[row=cy, col=cx]; verify vs base later.
    for (cx, cy), name in sorted(htiles.items()):
        t = np.array(Image.open(io.BytesIO(iz.read(name))).convert("RGB"))
        if ds > 1:
            t = t[::ds, ::ds]
        y0, x0 = cy * tsz, cx * tsz
        green[y0:y0 + tsz, x0:x0 + tsz] = t[..., 1]
        blue[y0:y0 + tsz, x0:x0 + tsz] = t[..., 2]
    for (cx, cy), name in sorted(btiles.items()):
        t = np.array(Image.open(io.BytesIO(iz.read(name))).convert("RGB"))
        if ds > 1:
            t = t[::ds, ::ds]
        idx = color_to_index(t, legend_colors)
        y0, x0 = cy * tsz, cx * tsz
        bidx[y0:y0 + tsz, x0:x0 + tsz] = idx.astype(np.uint8)
        ocean[y0:y0 + tsz, x0:x0 + tsz] = ocean_flag[idx]
    print(f"assembled {outsz}x{outsz} (downsample {ds})")

    # decode -> Phyxel world-Y (16-bit). land: sea + green/255*scale ; ocean: below sea.
    # Integer path keeps peak memory low on the full 24k run (no 2GB float array).
    Y = (PHYXEL_SEA + (green.astype(np.uint32) * int(round(args.height_scale)) // 255)).astype(np.uint16)
    Y[ocean] = max(0, int(PHYXEL_SEA - args.ocean_depth))
    ymin, ymax = int(Y.min()), int(Y.max())
    Image.fromarray(Y).save(os.path.join(OUT, f"me_height_{outsz}.png"))   # native uint16 -> I;16 PNG
    # Raw uint16 LE (row-major, outsz*outsz) for the engine loader — the repo's stb_image
    # is too old for 16-bit PNG, so MapCoarseData reads this directly. Dims == outsz (square).
    Y.astype("<u2").tofile(os.path.join(OUT, f"me_height_{outsz}.u16"))
    Image.fromarray(bidx).save(os.path.join(OUT, f"me_biome_{outsz}.png"))

    # 8-bit visual previews (not for sampling; for eyeballing). Downsample huge outputs.
    pstep = max(1, outsz // 3000)
    pv = Y[::pstep, ::pstep].astype(np.float32)
    Image.fromarray((255 * (pv - ymin) / max(1, (ymax - ymin))).clip(0, 255).astype(np.uint8)) \
        .save(os.path.join(OUT, f"preview_height_{outsz}.png"))
    pal = np.array(legend_colors, dtype=np.uint8)
    Image.fromarray(pal[np.clip(bidx[::pstep, ::pstep], 0, len(pal) - 1)]) \
        .save(os.path.join(OUT, f"preview_biome_{outsz}.png"))

    meta = dict(
        source="Middle-earth mod 1.0.0-1.21.8-beta (ARR; reference use only)",
        pixelWeight=PIXEL_WEIGHT, fullMapSizePx=FULL, worldSizeBlocks=FULL * PIXEL_WEIGHT,
        downsample=ds, imageSizePx=outsz,
        blocksPerImagePixel=PIXEL_WEIGHT * ds,
        seaLevelY=PHYXEL_SEA, heightScale=args.height_scale, oceanDepth=args.ocean_depth,
        heightEncoding="uint16 PNG value == Phyxel world Y directly (no rescale)",
        heightYRange=[ymin, ymax],
        biomeLegend="me_biome_legend.json (index -> rgb/temp/moisture/isOcean)",
        note="World col X = image X * blocksPerImagePixel; row Z = image Y * blocksPerImagePixel.",
    )
    with open(os.path.join(OUT, "me_terrain_meta.json"), "w") as f:
        json.dump(meta, f, indent=1)
    print("wrote outputs ->", OUT)
    print(f"  height Y range: {ymin}..{ymax}   world size: {FULL*PIXEL_WEIGHT} blocks")
    print(json.dumps(meta, indent=1))

if __name__ == "__main__":
    main()
