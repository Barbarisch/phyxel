#!/usr/bin/env python3
"""
Bake a Middle-earth relief world.db from the imported terrain PNGs.

Reads import_terrain.py output (me_height_*.png + me_biome_*.png + legend + meta),
downsamples the 96 km continent to a viewable world of --size blocks across, and
writes a surface voxel shell (materials by height + biome) into a Phyxel world.db
using the legacy sparse `cubes` table (the engine loads v1 rows when no v2 blob
exists). Produces a whole-continent relief you can fly over and recognise.

PROVENANCE: terrain is IMPORTED from the Middle-earth mod heightmap (ARR + Tolkien
IP) — it is NOT Phyxel's procedural generator output. Personal test-bed only.

Usage:
  python bake_test_world.py --terrain-dir <import_out> --out world.db \
      --size 512 --vscale 0.30 --skirt-cap 48
"""
import os, sys, json, argparse, sqlite3
import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None
CHUNK = 32

def pick(terrain_dir, stem):
    # newest matching me_<stem>_<N>.png (prefer the largest N = full res)
    best = None
    for f in os.listdir(terrain_dir):
        if f.startswith(f"me_{stem}_") and f.endswith(".png"):
            n = int(f[len(f"me_{stem}_"):-4])
            if best is None or n > best[0]:
                best = (n, f)
    if not best:
        sys.exit(f"no me_{stem}_*.png in {terrain_dir}")
    return os.path.join(terrain_dir, best[1])

def material_for(surfaceY, seaY, peakY, is_ocean, is_snow, temp, moist):
    if is_ocean:
        return "Ice"                       # flat sea (light blue)
    snowline = seaY + (peakY - seaY) * 0.68
    rockline = seaY + (peakY - seaY) * 0.42
    if is_snow or surfaceY >= snowline:
        return "Snow"
    if surfaceY >= rockline:
        return "Stone"
    if temp > 0.85 and moist < 0.5:
        return "Sand"                      # Harad / dry
    if surfaceY <= seaY + 2:
        return "Sand"                      # beach / shore
    if moist > 0.6 and temp < 0.6:
        return "GrassForest"               # damp temperate
    if temp > 0.75:
        return "GrassSavanna"
    return "Grass"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--terrain-dir", required=True)
    ap.add_argument("--out", required=True, help="output world.db path")
    ap.add_argument("--size", type=int, default=512, help="world width/depth in blocks")
    ap.add_argument("--vscale", type=float, default=0.14,
                    help="vertical exaggeration: worldY = seaY + (mapY-seaY)*vscale")
    ap.add_argument("--smooth", type=int, default=2,
                    help="box-blur passes on the downsampled height (kills needle cliffs)")
    ap.add_argument("--skirt-cap", type=int, default=48,
                    help="max voxels to fill down a cliff face (watertight slopes)")
    args = ap.parse_args()

    meta = json.load(open(os.path.join(args.terrain_dir, "me_terrain_meta.json")))
    legend = json.load(open(os.path.join(args.terrain_dir, "me_biome_legend.json")))
    seaY = int(meta["seaLevelY"])
    S = args.size

    H = np.asarray(Image.open(pick(args.terrain_dir, "height")))         # uint16 world-Y
    B = np.asarray(Image.open(pick(args.terrain_dir, "biome")))          # uint8 index
    # downsample to SxS: height bilinear (smooth relief), biome nearest (keep regions)
    Himg = Image.fromarray(H).resize((S, S), Image.BILINEAR)
    Bimg = Image.fromarray(B).resize((S, S), Image.NEAREST)
    mapY = np.asarray(Himg).astype(np.float32)
    bidx = np.asarray(Bimg).astype(np.int32)

    # box-blur the height a few times (separable) so the huge horizontal compression
    # doesn't leave single-block needle cliffs; biome index is left crisp.
    def box_blur(a):
        k = np.array([1, 2, 1], np.float32) / 4.0
        for _ in range(args.smooth):
            a = np.apply_along_axis(lambda m: np.convolve(m, k, mode="same"), 0, a)
            a = np.apply_along_axis(lambda m: np.convolve(m, k, mode="same"), 1, a)
        return a
    if args.smooth > 0:
        mapY = box_blur(mapY)
    peakY = float(mapY.max())

    ocean = np.array([e["isOcean"] for e in legend], dtype=bool)[bidx]
    snow  = np.array([e["isSnow"]  for e in legend], dtype=bool)[bidx]
    temp  = np.array([e["temperature"] for e in legend], dtype=np.float32)[bidx]
    moist = np.array([e["moisture"]    for e in legend], dtype=np.float32)[bidx]

    # world surface height (vertical exaggeration); oceans pinned to sea
    surf = np.rint(seaY + (mapY - seaY) * args.vscale).astype(np.int32)
    surf[ocean] = seaY
    surf = np.clip(surf, 1, None)
    peakWorldY = int(surf.max())

    # adaptive skirt: fill each column down to just below its lowest 4-neighbour so
    # cliffs are watertight, capped so a mountain face doesn't fill the whole volume.
    nb = np.full_like(surf, 1 << 30)
    nb[1:, :]  = np.minimum(nb[1:, :],  surf[:-1, :])
    nb[:-1, :] = np.minimum(nb[:-1, :], surf[1:, :])
    nb[:, 1:]  = np.minimum(nb[:, 1:],  surf[:, :-1])
    nb[:, :-1] = np.minimum(nb[:, :-1], surf[:, 1:])
    bottom = np.maximum(surf - args.skirt_cap, np.minimum(surf, nb) - 0)
    bottom = np.clip(bottom, 0, None)

    if os.path.exists(args.out):
        os.remove(args.out)
    db = sqlite3.connect(args.out)
    db.executescript("""
        PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;
        CREATE TABLE chunks (chunk_x INT, chunk_y INT, chunk_z INT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            modified_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY(chunk_x,chunk_y,chunk_z));
        CREATE TABLE cubes (chunk_x INT, chunk_y INT, chunk_z INT,
            local_x INT, local_y INT, local_z INT,
            is_subdivided INT DEFAULT 0, is_visible INT DEFAULT 1,
            material TEXT DEFAULT 'Default',
            PRIMARY KEY(chunk_x,chunk_y,chunk_z,local_x,local_y,local_z));
        CREATE TABLE world_meta (key TEXT PRIMARY KEY, value TEXT);
    """)

    chunks = set()
    rows = []
    total = 0
    for z in range(S):
        for x in range(S):
            top = int(surf[z, x])
            bot = int(bottom[z, x])
            mat_top = material_for(top, seaY, peakWorldY, bool(ocean[z, x]),
                                   bool(snow[z, x]), float(temp[z, x]), float(moist[z, x]))
            for y in range(bot, top + 1):
                mat = mat_top if y == top else ("Ice" if ocean[z, x] else
                      ("Stone" if y < seaY else "Dirt"))
                cx, cy, cz = x // CHUNK, y // CHUNK, z // CHUNK
                chunks.add((cx, cy, cz))
                rows.append((cx, cy, cz, x % CHUNK, y % CHUNK, z % CHUNK, 0, 1, mat))
                total += 1
        if len(rows) > 200000:
            db.executemany("INSERT OR REPLACE INTO cubes VALUES(?,?,?,?,?,?,?,?,?)", rows)
            rows.clear()
    if rows:
        db.executemany("INSERT OR REPLACE INTO cubes VALUES(?,?,?,?,?,?,?,?,?)", rows)
    db.executemany("INSERT OR REPLACE INTO chunks(chunk_x,chunk_y,chunk_z) VALUES(?,?,?)",
                   list(chunks))
    db.execute("INSERT OR REPLACE INTO world_meta VALUES('source',?)",
               ("middle-earth-import (ARR reference)",))
    db.commit(); db.close()

    cx = (S // 2)
    print(f"baked {args.out}")
    print(f"  world: {S}x{S} blocks, {len(chunks)} chunks, {total:,} voxels")
    print(f"  height: seaY={seaY} peakWorldY={peakWorldY} (vscale {args.vscale})")
    print(f"  suggested camera: high above centre, e.g. pos ({cx},{peakWorldY+S//2},{cx}) look at ({cx},{seaY},{cx})")

if __name__ == "__main__":
    main()
