#!/usr/bin/env python3
"""regen_furniture.py on the 1/8 m grid (docs/VoxelFoundationPlan.md §7a.3 step 4).

The furniture designs are authored in ninths (today's micro). On another grid the generator maps every
authored box EDGE to the nearest cell edge (so parts that touch keep touching) and the anchors' surface
heights through the same map. Contracts:
  * legacy (grid 9) output is byte-identical to the committed library (.voxel and .metrics.json);
  * grid 8: every asset is emitted as '# grid: 8', extents within one cell of the legacy metres, the same
    material set (no part vanishes), parts kept, and every seat/surface/lie anchor rests on a solid top.

Run: python -m pytest tools/test_regen_furniture_grid.py -q
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import regen_furniture as rf  # noqa: E402
import voxgrid as vg  # noqa: E402

GENS = [getattr(rf, n) for n in dir(rf) if n.startswith("gen_") and callable(getattr(rf, n))]


def _content(path):
    with open(path, "rb") as f:
        return f.read().replace(b"\r\n", b"\n")


def _run_all(tmp_path, grid):
    os.chdir(REPO)          # the metrics characterizer and the default dirs are repo-relative
    rf.set_grid(grid)
    rf.OUT_ROOT = str(tmp_path)
    try:
        for g in GENS:
            g()
    finally:
        rf.set_grid(9)
        rf.OUT_ROOT = None
    out = {}
    for root, _, files in os.walk(tmp_path):
        for fn in files:
            out[os.path.relpath(os.path.join(root, fn), tmp_path).replace("\\", "/")] = os.path.join(root, fn)
    return out


def _legacy_cells(path):
    """C/S/M micro lines -> {(px,py,pz): mat} in ninths (regen_furniture only writes M lines)."""
    cells = {}
    for ln in open(path, encoding="utf-8"):
        t = ln.split()
        if t and t[0] == "M":
            cx, cy, cz, sx, sy, sz, mx, my, mz = map(int, t[1:10])
            cells[(cx * 9 + sx * 3 + mx, cy * 9 + sy * 3 + my, cz * 9 + sz * 3 + mz)] = t[10]
    return cells


def _anchors(path):
    pts = []
    for ln in open(path, encoding="utf-8"):
        if ln.startswith("# interaction_point:"):
            t = ln.split(":", 1)[1].split()
            pts.append((t[0], t[1], float(t[2]), float(t[3]), float(t[4])))
    return pts


def _extent(cells, n):
    lo = [min(c[i] for c in cells) for i in range(3)]
    hi = [max(c[i] for c in cells) + 1 for i in range(3)]
    return [(h - l) / n for l, h in zip(lo, hi)], lo


# sha256 over (relpath, LF content) of everything the generator wrote at commit 9b66bc87, BEFORE the grid
# switch existed. Pinned to the generator's output, not the committed library: the committed templates had
# already drifted from their generator (metrics schema v1 vs the characterizer's v2; six templates missing
# the interaction_point header line the generator writes since A4) -- see VoxelFoundationPlan 7a.3 step 4.
LEGACY_DIGEST = "8475c6cecf76fcff7d47b15ee09e5b04ecf1a89654bb0e211e982690898b22ac"


def test_legacy_output_is_byte_identical(tmp_path):
    import hashlib
    got = _run_all(tmp_path, 9)
    assert len(got) == 2 * len(GENS) == 68, sorted(got)
    h = hashlib.sha256()
    for rel in sorted(got):
        h.update(rel.encode() + b"\0" + _content(got[rel]) + b"\0")
    assert h.hexdigest() == LEGACY_DIGEST


def test_grid8_every_asset_conforms(tmp_path):
    got = _run_all(tmp_path, 8)
    voxels = {rel: p for rel, p in got.items() if rel.endswith(".voxel")}
    assert len(voxels) == len(GENS), sorted(got)
    for rel, p in voxels.items():
        lines = open(p, encoding="utf-8").read().splitlines()
        assert lines[0] == "# grid: 8", rel
        n, cells = vg.parse_grid(lines)
        legacy = _legacy_cells(os.path.join(REPO, "resources", "templates", rel))
        assert cells and legacy, rel
        e8, _ = _extent(cells, 8)
        e9, _ = _extent(legacy, 9)
        for a, b in zip(e8, e9):
            assert abs(a - b) <= 1 / 8 + 1e-9, f"{rel}: extent {e8} vs legacy {e9}"
        assert set(cells.values()) == set(legacy.values()), f"{rel}: materials {set(cells.values())}"
        if "chest_closed" in rel:
            assert "# part: base" in lines and any(l.startswith("# part: lid") for l in lines), rel
        # every anchor stands on a solid top: the cell just below it is filled, the one at it is free
        for pid, kind, x, y, z in _anchors(p):
            cx, cy, cz = int(x * 8), round(y * 8), int(z * 8)
            assert (cx, cy - 1, cz) in cells, f"{rel}: {pid} at y={y} has nothing under it"
            assert (cx, cy, cz) not in cells, f"{rel}: {pid} at y={y} is inside the solid"


def test_legacy_anchors_also_stand_on_a_solid_top():
    """The same anchor check on the committed legacy library, so the grid-8 check is not stricter than today."""
    d = os.path.join(REPO, "resources", "templates")
    for rel in ("furniture/chair.voxel", "furniture/stool.voxel", "furniture/bar_stool.voxel",
                "furniture/bench_wood.voxel", "furniture/table_wood.voxel", "furniture/tavern_table.voxel"):
        cells = _legacy_cells(os.path.join(d, rel))
        for pid, kind, x, y, z in _anchors(os.path.join(d, rel)):
            cx, cy, cz = int(x * 9), round(y * 9), int(z * 9)
            assert (cx, cy - 1, cz) in cells and (cx, cy, cz) not in cells, (rel, pid)
