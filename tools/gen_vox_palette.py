#!/usr/bin/env python3
"""
gen_vox_palette.py — Generate a flat-color "vox_*" material palette for Phyxel,
sized to the actual color gamut of a set of .vox models (default: Barony).

Why: imported voxel models (see tools/vox_import.py) carry arbitrary per-model
colors. Snapping them to Phyxel's ~29 mostly-textured materials by nearest RGB
gives wonky results (everything goes grey). Instead we build a palette of
solid-color materials by voxel-frequency-weighted k-means over the source art,
so imports reproduce their real colors with a uniform flat-shaded look.

Each palette entry becomes:
  - a small solid-color PNG in resources/textures/source/vox_NN.png
    (the engine auto-generates flat normal/roughness for faces lacking a _nr
     sidecar, so one albedo PNG per color is enough; all 6 faces reuse it)
  - a "vox_NN" material in resources/materials.json (colorTint = the color)

The tool is idempotent: it strips any existing vox_* materials/PNGs first, so
re-running with a different -k just rebuilds the palette cleanly.

Usage:
  python tools/gen_vox_palette.py                       # 48 colors from Barony
  python tools/gen_vox_palette.py -k 32 --models DIR    # custom size/source
"""
import argparse, glob, json, os, struct, random
from collections import Counter
from PIL import Image

BARONY = "G:/SteamLibrary/steamapps/common/Barony/models"
MATERIALS = "resources/materials.json"
TEXDIR = "resources/textures/source"
PREFIX = "vox_"
FACES = ("top", "bottom", "side_n", "side_s", "side_e", "side_w")


def scan_colors(models_dir, quant=4):
    """Return {(r,g,b): voxel_count} over all .vox under models_dir (quantized)."""
    files = []
    for sub in ("decorations", "items", "creatures"):
        files += glob.glob(f"{models_dir}/{sub}/*.vox")
        files += glob.glob(f"{models_dir}/{sub}/*/*.vox")
    if not files:
        files = glob.glob(f"{models_dir}/**/*.vox", recursive=True)
    weights = Counter()
    n = 0
    for f in files:
        try:
            d = open(f, "rb").read()
            w, h, dp = struct.unpack_from("<iii", d, 0)
            need = w * h * dp
            grid, tail = d[12:12 + need], d[12 + need:]
            if len(tail) < 768 or len(grid) < need:
                continue
            pal = [(min(255, tail[i*3]*255//63), min(255, tail[i*3+1]*255//63),
                    min(255, tail[i*3+2]*255//63)) for i in range(256)]
            for idx, c in Counter(b for b in grid if b != 0xFF).items():
                r, g, b = pal[idx]
                weights[(r//quant*quant, g//quant*quant, b//quant*quant)] += c
            n += 1
        except Exception:
            pass
    print(f"[palette] scanned {len(files)} files ({n} parsed), "
          f"{len(weights)} distinct colors, {sum(weights.values())} voxels")
    return weights


def kmeans(weights, k, iters=40, seed=1):
    """Weighted k-means in RGB. Deterministic (seeded k-means++ init)."""
    pts = list(weights.items())  # ((r,g,b), w)
    rng = random.Random(seed)
    d2 = lambda a, b: (a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2
    # k-means++ weighted init
    centers = [rng.choices([p for p, _ in pts], weights=[w for _, w in pts])[0]]
    while len(centers) < k:
        dist = [min(d2(p, c) for c in centers) * w for (p, w) in pts]
        centers.append(rng.choices([p for p, _ in pts], weights=dist)[0])
    for _ in range(iters):
        sums = [[0, 0, 0, 0] for _ in range(k)]  # r,g,b,weight
        for (p, w) in pts:
            i = min(range(k), key=lambda i: d2(p, centers[i]))
            s = sums[i]; s[0]+=p[0]*w; s[1]+=p[1]*w; s[2]+=p[2]*w; s[3]+=w
        moved = 0
        for i, s in enumerate(sums):
            if s[3]:
                nc = (round(s[0]/s[3]), round(s[1]/s[3]), round(s[2]/s[3]))
                moved += d2(nc, centers[i]); centers[i] = nc
        if moved == 0:
            break
    # weight each center for stable ordering (most-used first)
    cw = [0]*k
    for (p, w) in pts:
        cw[min(range(k), key=lambda i: d2(p, centers[i]))] += w
    order = sorted(range(k), key=lambda i: -cw[i])
    return [centers[i] for i in order if cw[i] > 0]


def make_material(name, rgb):
    r, g, b = rgb
    return {
        "name": name,
        "category": "vox_palette",
        "description": f"Flat voxel-palette color rgb({r},{g},{b})",
        "physics": {
            "mass": 1.5, "friction": 0.6, "restitution": 0.1,
            "metallic": 0.0, "roughness": 0.85,
            "linearDamping": 0.2, "angularDamping": 0.3,
            "angularVelocityScale": 1.0, "bondStrength": 0.5,
            "breakForceMultiplier": 0.8,
            "colorTint": [round(r/255, 4), round(g/255, 4), round(b/255, 4)],
        },
        "textures": {f: f"{name}.png" for f in FACES},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", type=int, default=48, help="palette size (default 48)")
    ap.add_argument("--models", default=BARONY)
    ap.add_argument("--materials", default=MATERIALS)
    ap.add_argument("--texdir", default=TEXDIR)
    ap.add_argument("--size", type=int, default=512, help="PNG size (matches 512 res class)")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    weights = scan_colors(args.models)
    palette = kmeans(weights, args.k, seed=args.seed)
    print(f"[palette] {len(palette)} colors")

    # --- regenerate solid-color PNGs (strip old vox_* first) ---
    os.makedirs(args.texdir, exist_ok=True)
    for old in glob.glob(os.path.join(args.texdir, f"{PREFIX}*.png")):
        os.remove(old)
    names = []
    for i, rgb in enumerate(palette):
        name = f"{PREFIX}{i:02d}"
        names.append((name, rgb))
        Image.new("RGB", (args.size, args.size), tuple(rgb)).save(
            os.path.join(args.texdir, f"{name}.png"))

    # --- patch materials.json (strip old vox_*, append new) ---
    doc = json.load(open(args.materials, encoding="utf-8"))
    mats = doc["materials"] if isinstance(doc, dict) else doc
    mats[:] = [m for m in mats if not str(m.get("name", "")).startswith(PREFIX)]
    for name, rgb in names:
        mats.append(make_material(name, rgb))
    json.dump(doc, open(args.materials, "w", encoding="utf-8"), indent=2)

    # reference dump for vox_import / debugging
    json.dump([{"name": n, "rgb": list(c)} for n, c in names],
              open("resources/vox_palette.json", "w"), indent=2)
    print(f"[palette] wrote {len(names)} vox_* materials -> {args.materials}")
    print(f"[palette] wrote {len(names)} PNGs -> {args.texdir}")
    print(f"[palette] total materials now: {len(mats)}")


if __name__ == "__main__":
    main()
