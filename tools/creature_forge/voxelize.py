"""Voxelization: stamp swept sections / sheets into a world-aligned grid,
color per voxel (palette x arcs x gradient x hash noise, quantized so greedy
merging survives), then 3-axis greedy-merge per (bone, color) into Box shapes.

Determinism: dict insertion order + sorted merges + integer-hash noise; no
RNG, no timestamps. First writer wins on voxel collisions (volumes in spec
order, samples in ascending t) — hosts stamped before limbs merge cleanly.
"""
from __future__ import annotations

import math

from .spec import hex_to_rgb
from .sweep import inside_section, _dot, _sub

QUANT = 32  # color quantization steps per channel (merge-friendliness)


class Grid:
    def __init__(self, voxel_size: float):
        self.vs = voxel_size
        self.cells = {}  # (ix,iy,iz) -> [bone_name, r, g, b]

    def key_of(self, p):
        vs = self.vs
        return (math.floor(p[0] / vs), math.floor(p[1] / vs), math.floor(p[2] / vs))

    def center_of(self, key):
        vs = self.vs
        return ((key[0] + 0.5) * vs, (key[1] + 0.5) * vs, (key[2] + 0.5) * vs)

    def stamp(self, key, bone, rgb):
        if key not in self.cells:
            self.cells[key] = [bone, rgb[0], rgb[1], rgb[2]]


# ---------------------------------------------------------------------------
# color helpers
# ---------------------------------------------------------------------------

def arc_color(base_rgb, arcs, sym_deg):
    """colors.arcs: symmetric about the spine-belly plane; last match wins."""
    c = base_rgb
    for arc in arcs or []:
        if arc["from"] <= sym_deg <= arc["to"]:
            c = hex_to_rgb(arc["color"])
    return c


def _hash01(ix, iy, iz):
    """Deterministic integer hash -> [0,1). FNV-ish, no RNG."""
    h = (ix * 374761393 + iy * 668265263 + iz * 2147483647) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    h ^= h >> 16
    return h / 4294967296.0


def apply_shading(grid: Grid, shading: dict):
    """Global vertical lightness ramp + per-voxel value noise, then quantize.
    Port of anyCreature applyShading (defaults top=0.30 bottom=-0.88,
    noise size 0.018 of bbox diagonal, matched here by voxel-hash noise)."""
    if not grid.cells:
        return
    grad = (shading or {}).get("gradient", {})
    top = grad.get("top", 0.30)
    bottom = grad.get("bottom", -0.88)
    noise = (shading or {}).get("noise", {})
    amount = noise.get("amount", 0.0)

    ys = [k[1] for k in grid.cells]
    y0, y1 = min(ys), max(ys)
    span = (y1 - y0) or 1

    for key, cell in grid.cells.items():
        f = (key[1] - y0) / span
        k = 1.0 + top * f + bottom * (1.0 - f)
        r, g, b = cell[1] * k, cell[2] * k, cell[3] * k
        if amount:
            n = 1.0 + (_hash01(*key) - 0.5) * 2.0 * amount
            r, g, b = r * n, g * n, b * n
        cell[1] = round(max(0.0, min(1.0, r)) * QUANT) / QUANT
        cell[2] = round(max(0.0, min(1.0, g)) * QUANT) / QUANT
        cell[3] = round(max(0.0, min(1.0, b)) * QUANT) / QUANT


# ---------------------------------------------------------------------------
# stamping
# ---------------------------------------------------------------------------

def stamp_volume(grid: Grid, samples, base_rgb, arcs, step, sections=None):
    """Stamp each sample's cross-section as an axial slab of +-step/2.
    `sections` supplies named 2D outlines (spec['sections']) when used."""
    sections = sections or {}
    half_slab = step * 0.5 + 1e-9
    for s in samples:
        # AABB half-extents: superellipse bounded by (rw, rh*(1+|bias|));
        # named outlines are unit-ish so allow a generous 1.6x factor
        amp = 1.0 + abs(s.opts.get("bias") or 0.0)
        if s.opts.get("section"):
            rw = rh = 1.6 * max(s.rw, s.rh) * amp
        else:
            rw, rh = s.rw, s.rh * amp
        he = [abs(s.U[k]) * rw + abs(s.W[k]) * rh + abs(s.T[k]) * half_slab
              for k in range(3)]
        lo = grid.key_of((s.pos[0] - he[0], s.pos[1] - he[1], s.pos[2] - he[2]))
        hi = grid.key_of((s.pos[0] + he[0], s.pos[1] + he[1], s.pos[2] + he[2]))
        for ix in range(lo[0], hi[0] + 1):
            for iy in range(lo[1], hi[1] + 1):
                for iz in range(lo[2], hi[2] + 1):
                    c = grid.center_of((ix, iy, iz))
                    d = _sub(c, s.pos)
                    if abs(_dot(d, s.T)) > half_slab:
                        continue
                    u = _dot(d, s.U)
                    v = _dot(d, s.W)
                    if not inside_section(u, v, s.rw, s.rh, s.opts, sections):
                        continue
                    a_deg = math.degrees(math.atan2(v, u))
                    from_top = (450.0 - a_deg) % 360.0
                    sym = 360.0 - from_top if from_top > 180.0 else from_top
                    grid.stamp((ix, iy, iz), s.joint,
                               arc_color(base_rgb, arcs, sym))


def stamp_sheet(grid: Grid, points):
    """Rasterize a thin sheet as EXACTLY one voxel thick along its dominant
    normal axis: bucket fine samples by the two lateral axes, keep one cell
    per bucket. `points` = [(pos, bone, rgb, normal)]."""
    if not points:
        return
    # dominant normal axis over the sheet
    acc = [0.0, 0.0, 0.0]
    for _, _, _, n in points:
        for k in range(3):
            acc[k] += abs(n[k])
    dom = max(range(3), key=lambda k: acc[k])
    lat = [k for k in range(3) if k != dom]
    buckets = {}
    for pos, bone, rgb, _ in points:
        key = grid.key_of(pos)
        bkey = (key[lat[0]], key[lat[1]])
        # deterministic winner: nearest to its voxel-column center along dom
        center = grid.center_of(key)
        d = abs(pos[dom] - center[dom])
        prev = buckets.get(bkey)
        if prev is None or d < prev[0]:
            buckets[bkey] = (d, key, bone, rgb)
    for bkey in sorted(buckets):
        _, key, bone, rgb = buckets[bkey]
        grid.stamp(key, bone, rgb)


# ---------------------------------------------------------------------------
# greedy merge
# ---------------------------------------------------------------------------

def greedy_merge(grid: Grid):
    """3-axis greedy boxing per (bone, quantized color). Returns
    [(bone_name, size_world, center_world, rgb)] sorted deterministically."""
    groups = {}
    for key, cell in grid.cells.items():
        gk = (cell[0], cell[1], cell[2], cell[3])
        groups.setdefault(gk, set()).add(key)

    boxes = []
    vs = grid.vs
    for gk in sorted(groups, key=lambda g: (g[0], g[1], g[2], g[3])):
        cells = groups[gk]
        claimed = set()
        for start in sorted(cells):
            if start in claimed:
                continue
            x0, y0, z0 = start

            def free(x, y, z):
                return (x, y, z) in cells and (x, y, z) not in claimed

            # grow +x
            x1 = x0
            while free(x1 + 1, y0, z0):
                x1 += 1
            # widen +z
            z1 = z0
            while all(free(x, y0, z1 + 1) for x in range(x0, x1 + 1)):
                z1 += 1
            # thicken +y
            y1 = y0
            while all(free(x, y1 + 1, z)
                      for x in range(x0, x1 + 1) for z in range(z0, z1 + 1)):
                y1 += 1
            for x in range(x0, x1 + 1):
                for y in range(y0, y1 + 1):
                    for z in range(z0, z1 + 1):
                        claimed.add((x, y, z))
            size = ((x1 - x0 + 1) * vs, (y1 - y0 + 1) * vs, (z1 - z0 + 1) * vs)
            center = ((x0 + x1 + 1) * 0.5 * vs,
                      (y0 + y1 + 1) * 0.5 * vs,
                      (z0 + z1 + 1) * 0.5 * vs)
            boxes.append((gk[0], size, center, (gk[1], gk[2], gk[3])))
    return boxes
