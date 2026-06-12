#!/usr/bin/env python3
"""Parametric voxel tree generator — token-free biome flora (v2, sub-voxel).

Generates trees as .voxel templates using ALL THREE voxel resolutions:
  C  full cube  (1.0)   — canopy interior mass, lower trunk
  S  subcube    (1/3)   — dithered canopy shells, branches, trunk taper,
                          root flares, palm fronds, willow strands
  M  microcube  (1/9)   — twig tips and leaf sprigs on the canopy surface

Geometry is computed in SUB-space (1 unit = 1/3 cube) and compressed on
write-out: any 3x3x3 block of same-material subs that fills a whole cube is
emitted as one C line, so interiors stay cheap while silhouettes stay organic.

Deterministic per seed: same args -> same tree; new --seed -> natural variation.

Archetypes (silhouette + default materials):
  oak      blob canopy, sub-branches, root flare      Log       + Leaf
  autumn   oak silhouette, fall colors                Log       + LeafAutumn
  birch    slim tapering trunk, small high canopy     LogBirch  + LeafBirch
  spruce   smooth sub-resolution cone                 LogSpruce + LeafSpruce
  jungle   tall trunk, buttress roots, wide crown     Log       + LeafJungle
  acacia   sub-diagonal kinked trunk, flat pads       Log       + Leaf
  palm     curved slender trunk, thin fronds          Log       + LeafJungle
  willow   dome + hanging 1/3-thick strands           Log       + Leaf
  dead     bare tapering branches, micro twigs        LogSpruce (no leaves)
  bush     dithered leaf ball with sprigs             Leaf

Examples:
  python tools/gen_tree.py --type oak --height 7 --name tree_oak_m
  python tools/gen_tree.py --type spruce --height 12 --fullness 0.85 --seed 4
  python tools/gen_tree.py --batch tools/tree_library.json
"""

import argparse
import datetime
import json
import math
import os
import random
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE_DIR = os.path.join(REPO, "resources", "templates")
CATALOG = os.path.join(TEMPLATE_DIR, "template_catalog.json")

ARCHETYPES = {
    "oak":    {"log": "Log",       "leaf": "Leaf",       "height": 7},
    "autumn": {"log": "Log",       "leaf": "LeafAutumn", "height": 7},
    "birch":  {"log": "LogBirch",  "leaf": "LeafBirch",  "height": 8},
    "spruce": {"log": "LogSpruce", "leaf": "LeafSpruce", "height": 9},
    "jungle": {"log": "Log",       "leaf": "LeafJungle", "height": 13},
    "acacia": {"log": "Log",       "leaf": "Leaf",       "height": 6},
    "palm":   {"log": "Log",       "leaf": "LeafJungle", "height": 8},
    "willow": {"log": "Log",       "leaf": "Leaf",       "height": 8},
    "dead":   {"log": "LogSpruce", "leaf": None,         "height": 6},
    "bush":   {"log": None,        "leaf": "Leaf",       "height": 2},
}

# Shell band: subs with normalized canopy distance <= SOLID are always kept
# (interiors compress to C); SOLID..FUZZ is the dithered organic rim.
SOLID, FUZZ = 0.92, 1.06


class Tree:
    """Sub-space voxel accumulator. Logs always win over leaves."""

    def __init__(self):
        self.sub = {}     # (sx,sy,sz) -> (mat, is_log)
        self.micro = {}   # (sx,sy,sz,mx,my,mz) -> mat

    def put(self, sx, sy, sz, mat, is_log):
        if sy < 0:
            return
        k = (sx, sy, sz)
        cur = self.sub.get(k)
        if cur is None or (is_log and not cur[1]):
            self.sub[k] = (mat, is_log)

    def log(self, sx, sy, sz, mat):
        self.put(sx, sy, sz, mat, True)

    def leaf(self, sx, sy, sz, mat):
        self.put(sx, sy, sz, mat, False)

    def sprig(self, sx, sy, sz, mx, my, mz, mat):
        self.micro[(sx, sy, sz, mx, my, mz)] = mat

    def fill_cube(self, cx, cy, cz, mat, is_log=True):
        for dx in range(3):
            for dy in range(3):
                for dz in range(3):
                    self.put(cx * 3 + dx, cy * 3 + dy, cz * 3 + dz, mat, is_log)


# ------------------------------------------------------------ shape helpers

def shell_keep(rng, d, fullness):
    """Dithered keep-probability across the SOLID..FUZZ canopy rim."""
    if d <= SOLID:
        return True
    t = (d - SOLID) / (FUZZ - SOLID)          # 0 at solid edge, 1 at fuzz edge
    return rng.random() < fullness * (1.0 - 0.8 * t)


def ellipsoid_canopy(t, rng, cx, cy, cz, rx, ry, mat, fullness, sprigs=True):
    """Center and radii in SUB units. Solid core + dithered shell + sprigs."""
    rzx = rx
    for sx in range(int(cx - rx - 2), int(cx + rx + 3)):
        for sy in range(int(cy - ry - 2), int(cy + ry + 3)):
            for sz in range(int(cz - rzx - 2), int(cz + rzx + 3)):
                d = math.sqrt(((sx - cx) / rx) ** 2 + ((sy - cy) / ry) ** 2 +
                              ((sz - cz) / rzx) ** 2)
                if d > FUZZ:
                    continue
                if shell_keep(rng, d, fullness):
                    t.leaf(sx, sy, sz, mat)
                    # occasional microcube sprig poking out of the surface
                    if sprigs and d > 0.97 and rng.random() < 0.05:
                        ox = sx + (1 if sx > cx else -1 if sx < cx else 0)
                        oy = sy + (1 if sy > cy else 0)
                        oz = sz + (1 if sz > cz else -1 if sz < cz else 0)
                        t.sprig(ox, oy, oz, 1, 1, 1, mat)


def disc_canopy(t, rng, cy, r, mat, fullness, thick=2, cx=0, cz=0, sprigs=True):
    """Flat pad in SUB units: dithered rim, micro sprigs on the edge."""
    for sx in range(int(cx - r - 2), int(cx + r + 3)):
        for sz in range(int(cz - r - 2), int(cz + r + 3)):
            d = math.hypot(sx - cx, sz - cz) / r
            if d > FUZZ:
                continue
            for dy in range(thick):
                if shell_keep(rng, d, fullness):
                    t.leaf(sx, cy + dy, sz, mat)
            if sprigs and 0.9 < d <= FUZZ and rng.random() < 0.08:
                ox = sx + (1 if sx > cx else -1)
                oz = sz + (1 if sz > cz else -1)
                t.sprig(ox, cy, oz, 1, 2, 1, mat)


def cube_trunk(t, h_cubes, mat, taper_from=0.62, root_flare=True, rng=None):
    """Trunk centered on cube (0,*,0): full cubes below, plus-shaped taper above.

    The taper cross-section is a symmetric plus (center sub + 4 edge subs) so
    the narrower upper trunk stays centered on the lower cube — a 2x2 column
    can't center on the 3-sub grid and reads as a sideways kink.
    """
    taper_y = max(1, round(h_cubes * taper_from))
    for cy in range(taper_y):
        t.fill_cube(0, cy, 0, mat)
    for sy in range(taper_y * 3, h_cubes * 3):
        for (dx, dz) in ((1, 1), (0, 1), (2, 1), (1, 0), (1, 2)):
            t.log(dx, sy, dz, mat)
    if root_flare and rng:
        for (fx, fz) in [(-1, 0), (3, 0), (0, -1), (0, 3),
                         (-1, -1), (3, 3), (-1, 3), (3, -1)]:
            if rng.random() < 0.6:
                hgt = rng.randint(1, 2)
                for sy in range(hgt):
                    t.log(fx if fx >= 0 else -1, sy, fz if fz >= 0 else -1, mat)
    return h_cubes * 3                           # trunk top in subs


def sub_branch(t, rng, start, direction, length, mat, thickness=1,
               droop=0.0, rise=0.35, twig=True):
    """Random-walk branch in SUB space; returns end point."""
    x, y, z = start
    dx, dz = direction
    for i in range(length):
        r = rng.random()
        if r < 0.42:
            x += dx
        elif r < 0.84:
            z += dz
        else:
            y += 1 if rng.random() < rise else -1
        if droop and rng.random() < droop:
            y -= 1
        th = thickness if i < length * 0.6 else 1
        for bx in range(th):
            for by in range(th):
                t.log(x + bx, y + by, z, mat)
    if twig:                                     # micro twig at the tip
        tx, tz = x + dx, z + dz
        t.sprig(tx, y, tz, 1, 1, 1, mat)
        t.sprig(tx + dx, y + 1, tz, 0, 0, 1, mat)
    return (x, y, z)


# ---------------------------------------------------------------- archetypes

def gen_oak(t, rng, h, radius, fullness, log, leaf):
    r = (radius or max(2, round(h * 0.5))) * 3            # sub units
    ry = max(5, round(r * 0.8))
    top = cube_trunk(t, h, log, rng=rng)
    cy = top - round(ry * 0.45)                            # sink canopy on trunk
    # sub-branches reaching from upper trunk into the canopy
    for _ in range(rng.randint(2, 4)):
        by = rng.randint(round(top * 0.55), top - 2)
        sub_branch(t, rng, (1, by, 1),
                   (rng.choice([-1, 1]), rng.choice([-1, 1])),
                   rng.randint(r // 3, r // 2 + 2), log, thickness=2, rise=0.5)
    ellipsoid_canopy(t, rng, 1, cy, 1, r, ry, leaf, fullness)


def gen_birch(t, rng, h, radius, fullness, log, leaf):
    r = (radius or max(2, round(h * 0.3))) * 3
    top = cube_trunk(t, h, log, taper_from=0.4, root_flare=False, rng=rng)
    for _ in range(rng.randint(1, 2)):
        by = rng.randint(round(top * 0.6), top - 3)
        sub_branch(t, rng, (1, by, 1),
                   (rng.choice([-1, 1]), rng.choice([-1, 1])),
                   rng.randint(2, 4), log, thickness=1, rise=0.6)
    ellipsoid_canopy(t, rng, 1, top - round(r * 0.3), 1, r,
                     max(4, round(r * 1.15)), leaf, fullness)


def gen_spruce(t, rng, h, radius, fullness, log, leaf):
    base_r = (radius or max(2, round(h * 0.38))) * 3
    top = cube_trunk(t, h, log, taper_from=0.5, root_flare=True, rng=rng)
    lo = max(5, round(top * 0.22))
    # smooth cone: per-sub-layer radius from base_r to 1
    for sy in range(lo, top + 3):
        f = (sy - lo) / max(1, (top + 2 - lo))
        r = max(1.4, base_r * (1.0 - f) ** 1.1)
        # slight per-layer jitter keeps tiers from looking machined
        r += rng.uniform(-0.4, 0.4)
        for sx in range(int(1 - r - 1), int(1 + r + 2)):
            for sz in range(int(1 - r - 1), int(1 + r + 2)):
                d = math.hypot(sx - 1, sz - 1) / max(r, 0.1)
                if d <= FUZZ and shell_keep(rng, d, fullness):
                    t.leaf(sx, sy, sz, leaf)
    t.leaf(1, top + 3, 1, leaf)                            # tip spike
    t.sprig(1, top + 4, 1, 1, 0, 1, leaf)


def gen_jungle(t, rng, h, radius, fullness, log, leaf):
    r = (radius or max(3, round(h * 0.35))) * 3
    big = h >= 14
    if big:
        for cy in range(h):                                # 2x2-cube trunk
            for cx in range(2):
                for cz in range(2):
                    t.fill_cube(cx, cy, cz, log)
        c, top = 3, h * 3
    else:
        top = cube_trunk(t, h, log, taper_from=0.75, rng=rng)
        c = 1
    # buttress roots: tall thin sub-wedges
    for (fx, fz) in [(-1, c // 2), (c * 2 if big else 3, c // 2),
                     (c // 2, -1), (c // 2, c * 2 if big else 3)]:
        for sy in range(rng.randint(3, 5)):
            t.log(fx, sy, fz, log)
    # branch arms with leaf blobs partway up
    for _ in range(rng.randint(2, 3)):
        by = rng.randint(round(top * 0.5), top - 4)
        end = sub_branch(t, rng, (c, by, c),
                         (rng.choice([-1, 1]), rng.choice([-1, 1])),
                         rng.randint(4, 7), log, thickness=2, rise=0.45)
        ellipsoid_canopy(t, rng, end[0], end[1] + 2, end[2], 6, 4, leaf,
                         fullness, sprigs=False)
    ellipsoid_canopy(t, rng, c, top + 1, c, r, max(5, round(r * 0.5)),
                     leaf, fullness)


def gen_acacia(t, rng, h, radius, fullness, log, leaf):
    r = (radius or max(4, round(h * 0.85))) * 3
    # kinked trunk at SUB resolution: 2x2-sub column drifting diagonally
    x = z = 0
    drift_x = rng.choice([-1, 1])
    drift_z = rng.choice([-1, 0, 1])
    kink = rng.randint(5, max(6, h * 3 - 8))
    top = h * 3
    for sy in range(top):
        if sy >= kink and sy % 2 == 0:
            x += drift_x
            z += drift_z
        for bx in range(2):
            for bz in range(2):
                t.log(x + bx, sy, z + bz, log)
    # base: widen to 3x3 subs for footing
    for bx in range(-1, 3):
        for bz in range(-1, 3):
            t.log(bx, 0, bz, log)
    disc_canopy(t, rng, top, r, leaf, fullness, thick=2, cx=x + 1, cz=z + 1)
    if rng.random() < 0.5:
        disc_canopy(t, rng, max(6, top - 7), max(5, r // 2), leaf, fullness,
                    thick=1, cx=-x, cz=-z)
    # short support branches under the pad rim
    for ang in (45, 135, 225, 315):
        fx = math.cos(math.radians(ang))
        fz = math.sin(math.radians(ang))
        bx, bz = x + 1 + round(fx * r * 0.5), z + 1 + round(fz * r * 0.5)
        for i in range(3):
            t.log(round(x + 1 + fx * r * 0.5 * i / 3),
                  top - 3 + i, round(z + 1 + fz * r * 0.5 * i / 3), log)
        t.log(bx, top - 1, bz, log)


def gen_palm(t, rng, h, radius, fullness, log, leaf):
    r = (radius or max(3, round(h * 0.55))) * 3
    # curved slender trunk: 2x2 subs, lean grows quadratically
    lean = rng.choice([-1, 1])
    top_y = h * 3
    pts = []
    for sy in range(top_y):
        off = round(lean * 3.0 * (sy / top_y) ** 2)
        for bx in range(2):
            for bz in range(2):
                t.log(off + bx, sy, bz, log)
        pts.append(off)
    tx, tz = pts[-1], 0
    # fronds: drooping arcs rasterized as CONTINUOUS 3D lines (the quadratic
    # droop drops several subs per step at the tip — interpolate every segment
    # so fronds never break into floating dots), thicker base, micro tip
    for ang in range(0, 360, 40):
        fx, fz = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        px, py, pz = tx + 1, top_y + 1, tz + 1
        for i in range(1, r + 1):
            lx = tx + 1 + round(fx * i)
            lz = tz + 1 + round(fz * i)
            ly = top_y + 1 - max(0, round((i * i) / (r * 1.2)))
            steps = max(abs(lx - px), abs(ly - py), abs(lz - pz), 1)
            for s in range(1, steps + 1):
                ix = round(px + (lx - px) * s / steps)
                iy = round(py + (ly - py) * s / steps)
                iz = round(pz + (lz - pz) * s / steps)
                t.leaf(ix, iy, iz, leaf)
                if i <= r // 2:                   # thicker frond base half
                    t.leaf(ix, iy + 1, iz, leaf)
            px, py, pz = lx, ly, lz
        t.sprig(px + (1 if fx > 0 else -1), py - 1, pz + (1 if fz > 0 else -1),
                1, 2, 1, leaf)
    # coconut cluster: micros under the crown
    for _ in range(3):
        t.sprig(tx + rng.randint(0, 1), top_y - 1, rng.randint(0, 1),
                rng.randint(0, 2), 0, rng.randint(0, 2), log)
    t.leaf(tx + 1, top_y + 1, tz + 1, leaf)


def gen_willow(t, rng, h, radius, fullness, log, leaf):
    r = (radius or max(3, round(h * 0.55))) * 3
    top = cube_trunk(t, h, log, taper_from=0.55, rng=rng)
    for _ in range(rng.randint(2, 3)):
        by = rng.randint(round(top * 0.6), top - 2)
        sub_branch(t, rng, (1, by, 1),
                   (rng.choice([-1, 1]), rng.choice([-1, 1])),
                   rng.randint(3, 6), log, thickness=1, rise=0.55)
    cy = top - round(r * 0.2)
    ellipsoid_canopy(t, rng, 1, cy, 1, r, max(4, round(r * 0.55)), leaf, fullness)
    # hanging strands: 1/3-thick leaf columns from the rim, micro tips
    for ang in range(0, 360, 24):
        fx, fz = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        lx = 1 + round(fx * (r - 1))
        lz = 1 + round(fz * (r - 1))
        drop = rng.randint(round(top * 0.45), max(3, cy - 2))
        wob = rng.choice([-1, 0, 0, 1])
        for d in range(drop):
            wx = lx + (wob if d > drop * 0.6 else 0)
            t.leaf(wx, cy - d, lz, leaf)
        t.sprig(lx, cy - drop, lz, 1, 0, 1, leaf)


def gen_dead(t, rng, h, radius, fullness, log, leaf):
    top = cube_trunk(t, h, log, taper_from=0.4, rng=rng)
    for _ in range(rng.randint(3, 5)):
        by = rng.randint(round(top * 0.4), top - 1)
        end = sub_branch(t, rng, (1, by, 1),
                         (rng.choice([-1, 1]), rng.choice([-1, 1])),
                         rng.randint(4, 8), log, thickness=2, rise=0.55)
        # secondary twig off the branch end
        sub_branch(t, rng, end, (rng.choice([-1, 1]), rng.choice([-1, 1])),
                   rng.randint(2, 3), log, thickness=1, rise=0.5)
    t.log(1, top, 1, log)
    t.sprig(1, top + 1, 1, 1, 0, 1, log)


def gen_bush(t, rng, h, radius, fullness, log, leaf):
    r = (radius or 2) * 3
    ellipsoid_canopy(t, rng, 0, max(2, r - 2), 0, r, max(3, r - 2),
                     leaf, fullness)


GENERATORS = {
    "oak": gen_oak, "autumn": gen_oak, "birch": gen_birch, "spruce": gen_spruce,
    "jungle": gen_jungle, "acacia": gen_acacia, "palm": gen_palm,
    "willow": gen_willow, "dead": gen_dead, "bush": gen_bush,
}


# --------------------------------------------------------------- emission

def emit(tree):
    """Compress sub-space to C/S/M lines. Returns (lines, counts, bounds)."""
    by_cube = {}
    for (sx, sy, sz), (mat, _) in tree.sub.items():
        ck = (sx // 3, sy // 3, sz // 3)
        by_cube.setdefault(ck, {})[(sx % 3, sy % 3, sz % 3)] = mat

    micro_cubes = [(k[0] // 3, k[1] // 3, k[2] // 3) for k in tree.micro]
    all_cubes = list(by_cube) + micro_cubes
    ox = min(c[0] for c in all_cubes)
    oy = min(c[1] for c in all_cubes)
    oz = min(c[2] for c in all_cubes)
    mx = max(c[0] for c in all_cubes)
    my = max(c[1] for c in all_cubes)
    mz = max(c[2] for c in all_cubes)
    bounds = (mx - ox + 1, my - oy + 1, mz - oz + 1)

    c_lines, s_lines, m_lines = [], [], []
    for (cx, cy, cz), subs in sorted(by_cube.items(),
                                     key=lambda kv: (kv[0][1], kv[0][0], kv[0][2])):
        rx, ry, rz = cx - ox, cy - oy, cz - oz
        mats = set(subs.values())
        if len(subs) == 27 and len(mats) == 1:
            c_lines.append(f"C {rx} {ry} {rz} {next(iter(mats))}")
        else:
            for (ux, uy, uz), mat in sorted(subs.items()):
                s_lines.append(f"S {rx} {ry} {rz} {ux} {uy} {uz} {mat}")

    for (sx, sy, sz, ux, uy, uz), mat in sorted(tree.micro.items()):
        rx, ry, rz = sx // 3 - ox, sy // 3 - oy, sz // 3 - oz
        if ry < 0:
            continue
        m_lines.append(f"M {rx} {ry} {rz} {sx % 3} {sy % 3} {sz % 3} "
                       f"{ux} {uy} {uz} {mat}")

    counts = (len(c_lines), len(s_lines), len(m_lines))
    return c_lines + s_lines + m_lines, counts, bounds


def write_voxel(name, display, desc, lines, counts, bounds, materials, args_str):
    nc, ns, nm = counts
    w, hh, d = bounds
    header = [
        "# ==========================================================",
        "# ASSET METADATA",
        f"# name:         {name}",
        f"# display_name: {display}",
        f"# description:  {desc}",
        "# category:     nature",
        "# subcategory:  trees",
        "# tags:         tree, nature, biome, forest, procedural",
        f"# materials:    {', '.join(sorted(materials))}",
        "# facing:       +Z",
        f"# bounds:       {w}W x {hh}H x {d}D cubes",
        f"# primitives:   {nc} C + {ns} S + {nm} M = {nc + ns + nm}",
        f"# generator:    gen_tree.py {args_str}",
        "# ==========================================================",
        "",
    ]
    path = os.path.join(TEMPLATE_DIR, f"{name}.voxel")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(header + lines) + "\n")
    return path


def register_catalog(name, display, desc, counts, materials, height):
    nc, ns, nm = counts
    with open(CATALOG, encoding="utf-8") as f:
        cat = json.load(f)
    cat[name] = {
        "display_name": display,
        "description": desc,
        "prompt": None,
        "model": "procedural/gen_tree.py",
        "material": sorted(materials)[0],
        "materials": sorted(materials),
        "size": float(height),
        "cubes": nc,
        "subcubes": ns,
        "microcubes": nm,
        "total": nc + ns + nm,
        "cost": 0.0,
        "created": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "category": "nature",
        "subcategory": "trees",
        "tags": ["tree", "nature", "biome", "forest", "procedural"],
    }
    with open(CATALOG, "w", encoding="utf-8", newline="\n") as f:
        json.dump(cat, f, indent=2)
        f.write("\n")


def generate_one(ttype, height=None, radius=None, fullness=0.85, seed=0,
                 name=None, display=None, register=True):
    spec = ARCHETYPES[ttype]
    h = height or spec["height"]
    rng = random.Random(f"{ttype}:{h}:{radius}:{fullness}:{seed}")

    t = Tree()
    GENERATORS[ttype](t, rng, h, radius, fullness, spec["log"], spec["leaf"])
    if not t.sub:
        raise SystemExit(f"{ttype}: generated zero voxels?!")

    lines, counts, bounds = emit(t)
    name = name or f"tree_{ttype}_{h}_s{seed}"
    display = display or f"{ttype.capitalize()} Tree (h{h})"
    desc = (f"Procedural {ttype} tree, height {h}, fullness {fullness}, "
            f"seed {seed}.")
    materials = {m for m, _ in t.sub.values()} | set(t.micro.values())
    args_str = (f"--type {ttype} --height {h} --radius {radius or 'auto'} "
                f"--fullness {fullness} --seed {seed}")
    path = write_voxel(name, display, desc, lines, counts, bounds,
                       materials, args_str)
    if register:
        register_catalog(name, display, desc, counts, materials, h)
    nc, ns, nm = counts
    print(f"  {name}: {nc}C + {ns}S + {nm}M, bounds "
          f"{bounds[0]}x{bounds[1]}x{bounds[2]} -> {os.path.relpath(path, REPO)}")
    return name


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--type", choices=sorted(ARCHETYPES), help="tree archetype")
    ap.add_argument("--height", type=int, help="trunk height in cubes")
    ap.add_argument("--radius", type=int, help="canopy radius in cubes (default: from height)")
    ap.add_argument("--fullness", type=float, default=0.85, help="leaf density 0..1 (default 0.85)")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--name", help="template name (default tree_<type>_<h>_s<seed>)")
    ap.add_argument("--display-name", dest="display")
    ap.add_argument("--no-catalog", action="store_true", help="skip catalog registration")
    ap.add_argument("--batch", help="JSON manifest: [{type, height, fullness, seed, name, display}, ...]")
    args = ap.parse_args()

    if args.batch:
        with open(args.batch, encoding="utf-8") as f:
            entries = json.load(f)
        print(f"Batch: {len(entries)} trees")
        for e in entries:
            generate_one(e["type"], e.get("height"), e.get("radius"),
                         e.get("fullness", 0.85), e.get("seed", 0),
                         e.get("name"), e.get("display"),
                         register=not args.no_catalog)
        return 0

    if not args.type:
        ap.error("--type or --batch required")
    generate_one(args.type, args.height, args.radius, args.fullness, args.seed,
                 args.name, args.display, register=not args.no_catalog)
    return 0


if __name__ == "__main__":
    sys.exit(main())
