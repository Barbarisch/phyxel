#!/usr/bin/env python3
"""forge_core — the shared multi-resolution voxel substrate for procedural generators.

Extracted from tree_forge (roadmap step 5, 2026-07-05) so every organic generator — trees,
rocks, vines, roots, stalagmites, coral — builds on the same primitives:

  * MicroVoxels — a sparse voxel canvas addressed in MICROCUBE units (1 cube = 9 micro/axis).
    Generators draw into it at whatever precision they like.
  * emit()      — hierarchical compression to the engine's C/S/M template lines: a fully-filled
    uniform cube collapses to one C, a fully-filled subcube to one S, everything else stays M.
    This is what makes "detailed by default" cheap: solid INTERIORS compress to greedy-merged
    cubes automatically, so fine SURFACE detail costs only the shell.
  * rasterize_capsule() / rasterize_sphere() / fill_voxel() — grid-snapped primitives that draw
    on the canvas at a chosen resolution (9 = cube, 3 = subcube, 1 = micro). Choose the grid one
    level finer than the feature's bulk and emit() gives you cube interior + fine shell for free.

Determinism contract: these functions are pure geometry — same inputs, same voxels, byte-identical
emit. tree_forge's committed template library is regenerated through this module; any change here
MUST keep existing bakes byte-identical or be treated as a breaking regen of the library.
"""

import math

MICRO_PER_CUBE = 9          # 3 (cube->sub) * 3 (sub->micro); the engine's finest grid
SUB_PER_CUBE = 3


# --------------------------------------------------------------- vec helpers
def _add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def _sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _len(a): return math.sqrt(_dot(a, a))
def _norm(a):
    m = _len(a) or 1.0
    return (a[0] / m, a[1] / m, a[2] / m)


# ============================================================ multi-res voxels

class MicroVoxels:
    """Sparse voxel accumulator addressed in MICROCUBE units (1 cube = 9 micro per axis).
    Everything a generator draws lands here at micro precision; emit() compresses it back up
    to cubes/subcubes wherever a region is fully + uniformly filled."""

    __slots__ = ("v",)

    def __init__(self):
        self.v = {}            # (mx,my,mz) -> material   (integer micro coords, may be negative)

    def set(self, mx, my, mz, mat, overwrite=True):
        k = (mx, my, mz)
        if overwrite or k not in self.v:
            self.v[k] = mat

    def __len__(self):
        return len(self.v)


def emit(mv):
    """Compress the micro grid to C/S/M primitive lines. Returns (lines, counts, bounds_cubes).

    Hierarchy: a cube whose 729 micros are all present + one material -> C; else a subcube whose
    27 micros are all present + one material -> S; else each micro -> M."""
    if not mv.v:
        return [], (0, 0, 0), (0, 0, 0)

    def fdiv(a, b):
        return a // b                     # Python floor-division: correct for negatives

    # bucket micros by cube, storing in-cube micro index (0..8 per axis) -> material
    by_cube = {}
    for (mx, my, mz), mat in mv.v.items():
        ck = (fdiv(mx, MICRO_PER_CUBE), fdiv(my, MICRO_PER_CUBE), fdiv(mz, MICRO_PER_CUBE))
        by_cube.setdefault(ck, {})[(mx % MICRO_PER_CUBE, my % MICRO_PER_CUBE, mz % MICRO_PER_CUBE)] = mat

    ox = min(c[0] for c in by_cube)
    oy = min(c[1] for c in by_cube)
    oz = min(c[2] for c in by_cube)
    mxc = max(c[0] for c in by_cube)
    myc = max(c[1] for c in by_cube)
    mzc = max(c[2] for c in by_cube)

    c_lines, s_lines, m_lines = [], [], []
    for (cx, cy, cz), micros in sorted(by_cube.items(), key=lambda kv: (kv[0][1], kv[0][0], kv[0][2])):
        rx, ry, rz = cx - ox, cy - oy, cz - oz
        mats = set(micros.values())
        if len(micros) == MICRO_PER_CUBE ** 3 and len(mats) == 1:
            c_lines.append(f"C {rx} {ry} {rz} {next(iter(mats))}")
            continue
        # group this cube's micros by subcube (ux,uy,uz in 0..2)
        by_sub = {}
        for (mi, mj, mk), mat in micros.items():
            sk = (mi // SUB_PER_CUBE, mj // SUB_PER_CUBE, mk // SUB_PER_CUBE)
            by_sub.setdefault(sk, {})[(mi % SUB_PER_CUBE, mj % SUB_PER_CUBE, mk % SUB_PER_CUBE)] = mat
        for (ux, uy, uz), smicros in sorted(by_sub.items()):
            smats = set(smicros.values())
            if len(smicros) == SUB_PER_CUBE ** 3 and len(smats) == 1:
                s_lines.append(f"S {rx} {ry} {rz} {ux} {uy} {uz} {next(iter(smats))}")
            else:
                for (px, py, pz), mat in sorted(smicros.items()):
                    m_lines.append(f"M {rx} {ry} {rz} {ux} {uy} {uz} {px} {py} {pz} {mat}")

    counts = (len(c_lines), len(s_lines), len(m_lines))
    bounds = (mxc - ox + 1, myc - oy + 1, mzc - oz + 1)
    return c_lines + s_lines + m_lines, counts, bounds


# ============================================================ raster primitives

def fill_voxel(mv, ox, oy, oz, vs, mat):
    """Fill every micro inside the vs-sized voxel whose min corner is (ox,oy,oz) (grid-aligned)."""
    v = mv.v
    for ix in range(vs):
        for iy in range(vs):
            for iz in range(vs):
                v[(ox + ix, oy + iy, oz + iz)] = mat


def rasterize_capsule(mv, a, b, ra, rb, vs, mat):
    """Draw a tapered capsule from point a (radius ra) to b (radius rb), all in MICRO units,
    rasterized on the vs grid (9/3/1). Voxel centers within the lerped radius (+30% of a half
    cell of slack) fill whole vs-voxels — pick vs one level finer than the feature's bulk and
    emit() re-compresses the solid interior to cubes while the curved surface keeps a fine shell."""
    ab = _sub(b, a)
    abl2 = _dot(ab, ab) or 1.0
    rmax = max(ra, rb) + vs
    lo = [int(math.floor((min(a[i], b[i]) - rmax) / vs)) * vs for i in range(3)]
    hi = [int(math.ceil((max(a[i], b[i]) + rmax) / vs)) * vs for i in range(3)]
    half = vs * 0.5
    for vx in range(lo[0], hi[0] + 1, vs):
        for vy in range(lo[1], hi[1] + 1, vs):
            for vz in range(lo[2], hi[2] + 1, vs):
                cx = vx + half; cy = vy + half; cz = vz + half   # voxel center
                apx = cx - a[0]; apy = cy - a[1]; apz = cz - a[2]
                tt = (apx * ab[0] + apy * ab[1] + apz * ab[2]) / abl2
                tt = 0.0 if tt < 0 else (1.0 if tt > 1 else tt)
                dx = apx - ab[0] * tt; dy = apy - ab[1] * tt; dz = apz - ab[2] * tt
                r = ra + (rb - ra) * tt
                if dx * dx + dy * dy + dz * dz <= (r + half * 0.3) ** 2:
                    fill_voxel(mv, vx, vy, vz, vs, mat)


def rasterize_sphere(mv, center, r, vs, mat, overwrite=True):
    """Fill a sphere (MICRO units) on the vs grid. overwrite=False = draw-behind: existing
    voxels win (e.g. foliage around wood — twigs poke through the canopy)."""
    cx, cy, cz = center
    half = vs * 0.5
    r2 = (r + half) ** 2
    lo = [int(math.floor((c - r - vs) / vs)) * vs for c in (cx, cy, cz)]
    hi = [int(math.ceil((c + r + vs) / vs)) * vs for c in (cx, cy, cz)]
    fv = mv.v
    for vx in range(lo[0], hi[0] + 1, vs):
        ddx = vx + half - cx
        for vy in range(lo[1], hi[1] + 1, vs):
            ddy = vy + half - cy
            for vz in range(lo[2], hi[2] + 1, vs):
                ddz = vz + half - cz
                if ddx * ddx + ddy * ddy + ddz * ddz <= r2:
                    for ix in range(vs):
                        for iy in range(vs):
                            for iz in range(vs):
                                k = (vx + ix, vy + iy, vz + iz)
                                if overwrite or k not in fv:
                                    fv[k] = mat
