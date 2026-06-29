#!/usr/bin/env python3
"""
vox_import.py — Convert Barony-style .vox voxel models into Phyxel .voxel templates.

Barony (an open-source voxel roguelike) stores models in a dead-simple format:
  - 12-byte header: 3x int32  (width, height, depth)
  - then width*height*depth bytes, one palette index per voxel (0xFF = empty)
  - then a trailing 256-entry RGB palette (768 bytes, 6-bit VGA values 0..63)
The shared models/palette.dat is only a grayscale fallback; real colors are
per-model and live at the end of each .vox.

This tool parses that grid, maps each palette color to the nearest Phyxel material
(by materials.json colorTint), and emits a Phyxel .voxel template. By default each
source voxel becomes a 1/3-scale SUBCUBE (3 source voxels == 1 Phyxel cube), which
keeps imported models a sane physical size and leans on Phyxel's sub-voxel detail.

NOTE ON LICENSING: Barony's *code* is open source, but its *art assets* are
copyrighted by Turning Wheel LLC. Use imported models for local testing/learning
only — do not ship them in a distributed Phyxel game.

Usage:
  python tools/vox_import.py INPUT.vox -o out.voxel [options]
  python tools/vox_import.py INPUT.vox            # prints to stdout

Options:
  --palette PATH     palette.dat (default: alongside INPUT, or models/palette.dat)
  --materials PATH   Phyxel materials.json (default: resources/materials.json)
  --scale {cube,sub,micro}  output primitive: micro (9/cube, default) keeps fine
                     Barony detail; sub (3/cube); cube (1/cube).
  --downsample N     merge N^3 source voxels per output cell (default 2). Barony art
                     is ~16-18 voxels/"meter", so micro+downsample 2 (=18 src vox per
                     Phyxel cube) lands a humanoid near the ~1.75-cube character. Use 1
                     for full source resolution (4x larger).
  --up {x,y,z}       which source axis is vertical/up -> Phyxel +Y (default: z).
                     Barony is z-up-pointing-DOWN; the z branch flips it upright.
  --name NAME        template name in metadata (default: input stem)
  --map A=Mat,...    force palette-index or hex-color overrides, e.g. 12=Wood,#ff0000=Bricks
  --only MATS        comma list: keep only these mapped materials (drop others)
  --report           print a palette-index -> material usage table to stderr
"""
import argparse
import json
import os
import struct
import sys
from collections import Counter


def load_palette(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 768:
        raise ValueError(f"palette {path} is {len(data)} bytes, expected >=768")
    return [(data[i * 3], data[i * 3 + 1], data[i * 3 + 2]) for i in range(256)]


def load_materials(path, exclude=("Default", "Mirror"), prefix=None, exclude_prefix=None):
    """Return [(name, rgb)] of candidate materials.

    prefix         restrict to names starting with it (e.g. 'vox_' = flat palette).
    exclude_prefix drop names starting with it (e.g. 'vox_' to match against the
                   real, physics-bearing substances only — used by --emit tint)."""
    doc = json.load(open(path, encoding="utf-8"))
    mats = doc["materials"] if isinstance(doc, dict) else doc
    out = []
    for m in mats:
        if not isinstance(m, dict):
            continue
        name = m["name"]
        if name in exclude:
            continue
        if prefix and not name.startswith(prefix):
            continue
        if exclude_prefix and name.startswith(exclude_prefix):
            continue
        tint = m.get("physics", {}).get("colorTint")
        if not tint:
            continue
        rgb = tuple(int(round(c * 255)) for c in tint[:3])
        out.append((name, rgb))
    if not out:
        raise ValueError("no materials with colorTint found")
    return out


def nearest_material(rgb, mat_table):
    r, g, b = rgb
    best, bestd = None, 1 << 30
    for name, (mr, mg, mb) in mat_table:
        d = (r - mr) ** 2 + (g - mg) ** 2 + (b - mb) ** 2
        if d < bestd:
            best, bestd = name, d
    return best


def parse_vox(path, palette_override=None):
    """Return (w, h, d, grid_bytes, palette[256] as 0..255 RGB).

    The real palette is the trailing 768 bytes of the file (6-bit VGA, 0..63),
    scaled up to 8-bit. palette_override (from palette.dat) is used only if asked.
    """
    with open(path, "rb") as f:
        data = f.read()
    w, h, d = struct.unpack_from("<iii", data, 0)
    if min(w, h, d) <= 0 or max(w, h, d) > 4096:
        raise ValueError(f"implausible dims {w}x{h}x{d} in {path}")
    need = w * h * d
    grid = data[12:12 + need]
    if len(grid) < need:
        raise ValueError(f"{path}: {len(grid)} voxel bytes, expected {need}")
    if palette_override is not None:
        palette = palette_override
    else:
        tail = data[12 + need:]
        if len(tail) >= 768:
            # 6-bit VGA values -> 8-bit
            palette = [(min(255, tail[i * 3] * 255 // 63),
                        min(255, tail[i * 3 + 1] * 255 // 63),
                        min(255, tail[i * 3 + 2] * 255 // 63)) for i in range(256)]
        else:
            raise ValueError(f"{path}: no embedded palette (tail {len(tail)}B); pass --palette")
    return w, h, d, grid, palette


def remap_axes(x, y, z, dims, up):
    """Map source (x,y,z) with the chosen up-axis onto Phyxel (X right, Y up, Z toward viewer).

    Each branch is a PROPER rotation (determinant +1) — never a bare axis swap,
    which is a reflection that mirrors handedness (a chair would face backward /
    a left limb would become a right limb). The result may contain negative
    coordinates; convert() normalizes the min corner back to the origin.

    Barony's vertical axis is +z pointing DOWN (limbs.txt: head z-offset -1.5,
    legs +2). So for up="z" we negate z to land it on Phyxel's +Y (up); without
    that flip every imported model comes in upside-down.
    """
    if up == "z":   # source +Z is vertical (Barony: pointing DOWN) -> rotate -90deg about X
        return x, -z, y, None
    if up == "y":   # source already Y-up -> identity
        return x, y, z, None
    if up == "x":   # source +X is vertical -> rotate +90deg about Z
        return -y, x, z, None
    raise ValueError(up)


def convert(args):
    palette_override = load_palette(args.palette) if args.palette else None
    # --emit tint (default): each voxel -> a real substance (physics) + an exact
    #   per-voxel tint color, so wood is Wood (with wood physics) tinted to its real
    #   hue. The flat vox_* palette is excluded so substances carry real material
    #   props. See docs/VoxelAppearanceModel.md.
    # --emit palette: legacy — map straight to the flat vox_* palette, no tint.
    if args.emit == "tint":
        mat_table = load_materials(args.materials, exclude_prefix="vox_")
    else:
        mat_table = load_materials(args.materials,
                                  prefix="vox_" if args.matset == "vox" else None)
    w, h, d, grid, palette = parse_vox(args.input, palette_override)

    # Build per-palette-index -> material, honoring overrides.
    index_override = {}   # palette index -> material
    color_override = {}   # (r,g,b) -> material
    if args.map:
        for tok in args.map.split(","):
            k, _, v = tok.partition("=")
            k = k.strip()
            v = v.strip()
            if k.startswith("#") and len(k) == 7:
                color_override[(int(k[1:3], 16), int(k[3:5], 16), int(k[5:7], 16))] = v
            else:
                index_override[int(k)] = v

    keep = set(s.strip() for s in args.only.split(",")) if args.only else None

    idx_to_mat = {}
    for i in range(256):
        if i in index_override:
            idx_to_mat[i] = index_override[i]
        elif palette[i] in color_override:
            idx_to_mat[i] = color_override[palette[i]]
        else:
            idx_to_mat[i] = nearest_material(palette[i], mat_table)

    # Exact per-index tint hex (only attached in tint mode; None otherwise).
    def hex_of(i):
        r, g, b = palette[i]
        return f"#{r:02x}{g:02x}{b:02x}"
    idx_to_tint = {i: (hex_of(i) if args.emit == "tint" else None) for i in range(256)}

    # Gather occupied source voxels, remapped to Phyxel-oriented coords (proper rotation).
    raw = []  # (px, py, pz, material, tint)
    usage = Counter()
    for x in range(w):
        for y in range(h):
            for z in range(d):
                pidx = grid[x * h * d + y * d + z]
                if pidx == 0xFF:
                    continue
                mat = idx_to_mat[pidx]
                if keep and mat not in keep:
                    continue
                px, py, pz, _ = remap_axes(x, y, z, (w, h, d), args.up)
                raw.append((px, py, pz, mat, idx_to_tint[pidx]))
                usage[(pidx, mat)] += 1

    if not raw:
        raise ValueError("no occupied voxels after mapping/filtering")

    # Normalize so min corner sits at origin (rotation may produce negatives).
    minx = min(c[0] for c in raw)
    miny = min(c[1] for c in raw)
    minz = min(c[2] for c in raw)
    raw = [(x - minx, y - miny, z - minz, m, t) for (x, y, z, m, t) in raw]

    # Optional integer downsample: merge f^3 source voxels into one output cell
    # (occupied if any constituent is; appearance = most common (material,tint)).
    # Barony art is ~16-18 voxels/"meter"; with microcubes (9 cells/cube) a factor
    # of 2 lands a humanoid near Phyxel's ~1.75-cube character height.
    f = max(1, args.downsample)
    if f > 1:
        blocks = {}  # (X,Y,Z) -> Counter((material, tint))
        for (x, y, z, m, t) in raw:
            blocks.setdefault((x // f, y // f, z // f), Counter())[(m, t)] += 1
        cells = [(x, y, z) + c.most_common(1)[0][0] for (x, y, z), c in blocks.items()]
    else:
        cells = raw

    # Emit at the chosen primitive resolution (cube=1, sub=3, micro=9 cells/cube).
    res = {"cube": 1, "sub": 3, "micro": 9}[args.scale]

    def appearance(m, t):
        return f"{m}  tint={t}" if t else f"{m}"

    lines = []
    for (x, y, z, m, t) in sorted(cells, key=lambda c: (c[0], c[1], c[2])):
        cx, cy, cz = x // res, y // res, z // res
        if res == 1:
            lines.append(f"C {cx} {cy} {cz}  {appearance(m, t)}")
        elif res == 3:
            lines.append(f"S {cx} {cy} {cz}  {x % 3} {y % 3} {z % 3}  {appearance(m, t)}")
        else:  # micro: split the in-cube 0..8 coord into sub(//3) + micro(%3)
            ix, iy, iz = x % 9, y % 9, z % 9
            lines.append(f"M {cx} {cy} {cz}  {ix // 3} {iy // 3} {iz // 3}  "
                         f"{ix % 3} {iy % 3} {iz % 3}  {appearance(m, t)}")

    # Bounds in cube units.
    bx = (max(c[0] for c in cells) // res) + 1
    by = (max(c[1] for c in cells) // res) + 1
    bz = (max(c[2] for c in cells) // res) + 1

    name = args.name or os.path.splitext(os.path.basename(args.input))[0]
    mats_used = sorted({c[3] for c in cells})
    header = [
        "# ==========================================================",
        "# ASSET METADATA",
        f"# name:         {name}",
        f"# description:  Imported from Barony .vox ({os.path.basename(args.input)})",
        "# category:     imported",
        f"# materials:    {', '.join(mats_used)}",
        f"# bounds:       {bx}W x {by}H x {bz}D cubes  (scale={args.scale}, up={args.up})",
        f"# primitives:   {len(lines)} {'C' if res == 1 else 'S' if res == 3 else 'M'} (downsample={f})",
        "# author:       vox_import.py (source asset (c) Turning Wheel LLC — local use only)",
        "# ==========================================================",
        "",
    ]
    out = "\n".join(header + lines) + "\n"

    if args.report:
        print(f"[vox_import] {w}x{h}x{d} src -> {len(lines)} prims, "
              f"{bx}x{by}x{bz} cubes", file=sys.stderr)
        for (pidx, mat), n in usage.most_common(20):
            print(f"  idx {pidx:3d} rgb{palette[pidx]} -> {mat:12s} x{n}", file=sys.stderr)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(out)
        print(f"wrote {args.output}  ({len(lines)} primitives, {bx}x{by}x{bz} cubes)")
    else:
        sys.stdout.write(out)


def main():
    ap = argparse.ArgumentParser(description="Convert Barony .vox -> Phyxel .voxel")
    ap.add_argument("input")
    ap.add_argument("-o", "--output")
    ap.add_argument("--palette", help="override palette (8-bit RGB .dat); default = per-model embedded palette")
    ap.add_argument("--materials", default="resources/materials.json")
    ap.add_argument("--emit", choices=["tint", "palette"], default="tint",
                    help="tint (default) = real substance material + exact per-voxel "
                         "tint color (physics + faithful color); palette = legacy flat "
                         "vox_* palette materials, no tint")
    ap.add_argument("--matset", choices=["vox", "all"], default="vox",
                    help="(palette mode only) vox = map to flat vox_* palette; "
                         "all = nearest among every material incl. textured")
    ap.add_argument("--scale", choices=["cube", "sub", "micro"], default="micro",
                    help="output primitive: cube (1/cube), sub (3/cube), micro (9/cube, default)")
    ap.add_argument("--downsample", type=int, default=2,
                    help="merge N^3 source voxels per output cell (default 2; "
                         "micro+2 ~= Barony scale). Use 1 for full source resolution.")
    ap.add_argument("--up", choices=["x", "y", "z"], default="z")
    ap.add_argument("--name")
    ap.add_argument("--map")
    ap.add_argument("--only")
    ap.add_argument("--report", action="store_true")
    args = ap.parse_args()
    convert(args)


if __name__ == "__main__":
    main()
