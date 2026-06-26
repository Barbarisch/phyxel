#!/usr/bin/env python3
"""Regenerate furniture .voxel templates whose dims drift from the grounded canon
(resources/object_dimensions.json), at the correct proportions, AND write the matching
.metrics.json from the SAME emitted voxel bounds (so the sidecar can't drift from the geometry).

Deterministic, no LLM. Microcube resolution: 1 cube = 9 micro = 1 m. A global micro index p maps to
(cube=p//9, sub=(p%9)//3, mic=p%3). Run from the repo root.

Targets (canon, metres):
  chest  -> coffer  1.2 w x 0.55 d x 0.7 h  (object_dimensions 'chest')
  hearth -> fireplace 1.5 w x 1.2 h x 0.6 d (object_dimensions 'hearth')
"""
import json
import os

TEMPLATES = "resources/templates"


def decomp(p):
    """global micro index -> (cube, sub, mic)"""
    return p // 9, (p % 9) // 3, p % 3


class Model:
    def __init__(self):
        self.cells = {}  # (px,py,pz) -> material  (micro grid)

    def m(self, px, py, pz, mat):
        self.cells[(px, py, pz)] = mat

    def box_shell(self, x0, x1, y0, y1, z0, z1, mat, faces=("x", "z", "ymin", "ymax")):
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    on = False
                    if "x" in faces and (x == x0 or x == x1):
                        on = True
                    if "z" in faces and (z == z0 or z == z1):
                        on = True
                    if "ymin" in faces and y == y0:
                        on = True
                    if "ymax" in faces and y == y1:
                        on = True
                    if on:
                        self.cells[(x, y, z)] = mat

    def fill(self, x0, x1, y0, y1, z0, z1, mat):
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    self.cells[(x, y, z)] = mat

    def clear(self, x0, x1, y0, y1, z0, z1):
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                for z in range(z0, z1 + 1):
                    self.cells.pop((x, y, z), None)

    def bounds(self):
        xs = [c[0] for c in self.cells]
        ys = [c[1] for c in self.cells]
        zs = [c[2] for c in self.cells]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))

    def emit_lines(self, order):
        """order: list of (predicate, comment) -> grouped output for parts."""
        out = []
        used = set()
        for pred, comment in order:
            if comment:
                out.append(comment)
            for (px, py, pz), mat in sorted(self.cells.items()):
                if (px, py, pz) in used or not pred(px, py, pz):
                    continue
                cx, sx, mx = decomp(px)
                cy, sy, my = decomp(py)
                cz, sz, mz = decomp(pz)
                out.append(f"M {cx} {cy} {cz} {sx} {sy} {sz} {mx} {my} {mz} {mat}")
                used.add((px, py, pz))
        # any ungrouped cells
        for (px, py, pz), mat in sorted(self.cells.items()):
            if (px, py, pz) in used:
                continue
            cx, sx, mx = decomp(px)
            cy, sy, my = decomp(py)
            cz, sz, mz = decomp(pz)
            out.append(f"M {cx} {cy} {cz} {sx} {sy} {sz} {mx} {my} {mz} {mat}")
        return out


def write(name, header, body_lines, model):
    (mnx, mny, mnz), (mxx, mxy, mxz) = model.bounds()
    # overall extent in metres: a micro at index p occupies [p/9, (p+1)/9]
    omin = [mnx / 9.0, mny / 9.0, mnz / 9.0]
    omax = [(mxx + 1) / 9.0, (mxy + 1) / 9.0, (mxz + 1) / 9.0]
    vox_path = os.path.join(TEMPLATES, name + ".voxel")
    with open(vox_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header.rstrip() + "\n\n")
        f.write("\n".join(body_lines) + "\n")
    met_path = os.path.join(TEMPLATES, name + ".metrics.json")
    with open(met_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump({
            "schema_version": "asset_metrics.v1",
            "template_name": name,
            "overall_min": omin,
            "overall_max": omax,
            "interaction_points": [],
        }, f, indent=2)
    w, h, d = omax[0] - omin[0], omax[1] - omin[1], omax[2] - omin[2]
    print(f"{name}: {len(model.cells)} micro, bounds {w:.3f}w x {h:.3f}h x {d:.3f}d m -> {vox_path}")


def gen_chest():
    # coffer: 11 micro wide (1.22), 6 tall (0.67), 5 deep (0.56). facing +Z (clasp front).
    W, H, D = 11, 6, 5
    body_top = 3   # body occupies y 0..3, lid y 4..5
    m = Model()
    # body shell (hollow): walls + bottom, open top (lid sits over it)
    m.box_shell(0, W - 1, 0, body_top, 0, D - 1, "Wood", faces=("x", "z", "ymin"))
    # iron straps on the front face (z=0)
    for sx in (2, W - 3):
        for y in range(0, body_top + 1):
            m.m(sx, y, 0, "Metal")
    # lid: full top cap y 4..5 + a front clasp
    lid = Model()
    for x in range(0, W):
        for z in range(0, D):
            for y in range(body_top + 1, H):
                lid.m(x, y, z, "Wood")
    lid.m(W // 2, body_top + 1, 0, "Metal")  # clasp, front centre
    for k, v in lid.cells.items():
        m.cells[k] = v
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         chest_closed\n"
        "# display_name: Chest (Closed)\n"
        "# description:  A wide wooden storage coffer with iron straps, lid closed.\n"
        "# category:     furniture\n"
        "# subcategory:  container\n"
        "# tags:         chest, loot, storage, house, treasure, common\n"
        "# materials:    Wood, Metal\n"
        "# facing:       +Z (clasp faces viewer)\n"
        "# bounds:       1.22W x 0.67H x 0.56D m (grounded to object_dimensions 'chest' 1.2x0.55x0.7)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# ==========================================================\n"
        "\n# Front = +Z."
    )
    is_lid = lambda px, py, pz: py > body_top
    order = [
        (lambda px, py, pz: py <= body_top, "# part: base"),
        (is_lid, "# part: lid hinge=back_top axis=x"),
    ]
    write("chest_closed", header, m.emit_lines(order), m)


def gen_fireplace():
    # hearth: 14 wide (1.56), 11 tall (1.22), 5 deep (0.56). facing +Z (opening front).
    W, H, D = 14, 11, 5
    m = Model()
    # hearth base slab
    m.fill(0, W - 1, 0, 1, 0, D - 1, "Stone")
    # back wall
    m.fill(0, W - 1, 0, H - 1, D - 1, D - 1, "Stone")
    # side pillars
    m.fill(0, 2, 2, H - 1, 0, D - 1, "Stone")
    m.fill(W - 3, W - 1, 2, H - 1, 0, D - 1, "Stone")
    # lintel / mantel across the top
    m.fill(0, W - 1, H - 3, H - 1, 0, D - 1, "Stone")
    # carve the firebox opening (front + interior void)
    m.clear(3, W - 4, 2, H - 4, 0, D - 2)
    # fire: a log bed + glowing embers on the hearth floor inside the firebox
    m.fill(4, W - 5, 2, 2, 1, D - 2, "Log")
    m.fill(5, W - 6, 2, 2, 2, 2, "glow")
    header = (
        "# ==========================================================\n"
        "# ASSET METADATA\n"
        "# name:         fireplace\n"
        "# display_name: Fireplace\n"
        "# description:  A stone hearth with a fire opening and chimney breast.\n"
        "# category:     furniture\n"
        "# materials:    Stone, Log, glow\n"
        "# facing:       +Z (opening faces +Z; chimney breast to the wall)\n"
        "# bounds:       1.56W x 1.22H x 0.56D m (grounded to object_dimensions 'hearth' 1.5x1.2x0.6)\n"
        "# method:       tools/regen_furniture.py (deterministic, canon-proportioned)\n"
        "# =========================================================="
    )
    write("fireplace", header, m.emit_lines([]), m)


if __name__ == "__main__":
    gen_chest()
    gen_fireplace()
