#!/usr/bin/env python3
"""Tests for the 1/8 m generator path (docs/VoxelFoundationPlan.md §7a): voxgrid's emitter and tree_forge --grid 8.

Run: python -m pytest tools/test_voxgrid.py -q
"""

import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tree_forge as tf  # noqa: E402
import voxgrid as vg  # noqa: E402

REPO = os.path.dirname(HERE)


def _random_canvas(seed, n=4000, span=20, mats=("Log", "Leaf", "Stone")):
    rng = random.Random(seed)
    gv = vg.GridVoxels(8)
    for _ in range(n):
        gv.set(rng.randrange(-span, span), rng.randrange(0, span), rng.randrange(-span, span), rng.choice(mats))
    # plus a solid box so merging has something to do
    for x in range(5):
        for y in range(3):
            for z in range(7):
                gv.set(x + 30, y, z, "Stone")
    return gv


def test_emit_parse_roundtrip_is_exact():
    for seed in range(5):
        gv = _random_canvas(seed)
        lines, (nboxes, ncells), _ = vg.emit_grid(gv)
        n, cells = vg.parse_grid(lines)
        assert n == 8
        ox = min(k[0] for k in gv.v); oy = min(k[1] for k in gv.v); oz = min(k[2] for k in gv.v)
        want = {(x - ox, y - oy, z - oz): m for (x, y, z), m in gv.v.items()}
        assert cells == want
        assert ncells == len(want) and nboxes <= ncells


def test_boxes_never_overlap():
    gv = _random_canvas(9)
    lines, _, _ = vg.emit_grid(gv)
    seen = set()
    for ln in lines[1:]:
        t = ln.split()
        x, y, z = map(int, t[1:4])
        sx, sy, sz = (1, 1, 1) if t[0] == "V" else tuple(map(int, t[4:7]))
        for i in range(sx):
            for j in range(sy):
                for k in range(sz):
                    c = (x + i, y + j, z + k)
                    assert c not in seen, f"cell {c} covered twice"
                    seen.add(c)


def test_solid_box_is_one_line():
    gv = vg.GridVoxels(8)
    for x in range(8):
        for y in range(8):
            for z in range(8):
                gv.set(x, y, z, "Stone")
    lines, (nboxes, ncells), bounds = vg.emit_grid(gv)
    assert lines == ["# grid: 8", "B 0 0 0 8 8 8 Stone"]
    assert (nboxes, ncells, bounds) == (1, 512, (8, 8, 8))


def test_emit_is_deterministic():
    a = vg.emit_grid(_random_canvas(3))[0]
    b = vg.emit_grid(_random_canvas(3))[0]
    assert a == b


def _strip_cr(path):
    with open(path, "rb") as f:
        return f.read().replace(b"\r\n", b"\n")


def test_legacy_bake_is_byte_identical_to_the_committed_library(tmp_path):
    """Adding --grid must not change today's output (forge_core's determinism contract). Content compare,
    line endings ignored (Windows checkouts have CRLF)."""
    import json
    with open(os.path.join(HERE, "forge_library.json"), encoding="utf-8") as f:
        lib = {e["name"]: e for e in json.load(f)}
    for name in ("forge_oak_m", "forge_palm_m", "forge_spruce_s"):
        tf.bake(outdir=str(tmp_path), **lib[name])
        got = _strip_cr(os.path.join(tmp_path, f"{name}.voxel"))
        want = _strip_cr(os.path.join(REPO, "resources", "templates", "nature", f"{name}.voxel"))
        assert got == want, f"{name}: legacy bake changed"


def _volumes_m3(cells, cell_m):
    vol = {}
    for m in cells.values():
        vol[m] = vol.get(m, 0.0) + cell_m ** 3
    return vol


def test_grid8_tree_keeps_the_legacy_wood_volume():
    """Same skeleton, other grid. PREDICTION (written before the first run, 2026-11-11): wood volume at 1/8 m is
    within 20% of the legacy bake's (legacy thick wood has a 1/3 m surface shell, so it over-fills by up to a
    sixth of a metre of radius; 1/8 m follows the capsule closer)."""
    mv9, _ = tf.build_tree("oak", 13, 11, "forest")
    mv8, _ = tf.build_tree("oak", 13, 11, "forest", grid=8)
    w9 = sum(1 for m in mv9.v.values() if m == "Log") / 729.0
    w8 = sum(1 for m in mv8.v.values() if m == "Log") / 512.0
    print(f"[voxgrid] oak h13 s11 wood: legacy {w9:.3f} m3, 1/8 m {w8:.3f} m3 ({w8 / w9:.3f}x)")
    assert abs(w8 / w9 - 1.0) <= 0.20


def test_grid8_tree_stands_on_its_floor_and_is_one_piece():
    """The trunk base is on the template floor (y=0) and every cell is 26-connected to it (no floaters)."""
    mv8, _ = tf.build_tree("oak", 13, 11, "forest", grid=8)
    cells = set(mv8.v)
    assert min(k[1] for k in cells) == 0
    assert any(mv8.v[k] == "Log" for k in cells if k[1] == 0)
    start = next(k for k in cells if k[1] == 0 and mv8.v[k] == "Log")
    seen, stack = {start}, [start]
    while stack:
        x, y, z = stack.pop()
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    n = (x + dx, y + dy, z + dz)
                    if n in cells and n not in seen:
                        seen.add(n)
                        stack.append(n)
    print(f"[voxgrid] oak h13 s11 grid 8: {len(cells)} cells, {len(cells) - len(seen)} not connected to the trunk")
    assert len(seen) == len(cells)


def test_grid8_bake_writes_the_grid_format(tmp_path):
    msg = tf.bake("bush", 3, 1, "forest", outdir=str(tmp_path), grid=8, name="g8_bush")
    with open(os.path.join(tmp_path, "g8_bush.voxel"), encoding="utf-8") as f:
        text = f.read().splitlines()
    assert text[0] == "# grid: 8", "grid declaration must precede everything"
    n, cells = vg.parse_grid(text)
    assert n == 8 and cells
    assert all(not ln.startswith(("C ", "S ", "M ")) for ln in text), "no legacy C/S/M lines on the 1/8 m grid"
    assert "grid 8" in msg
