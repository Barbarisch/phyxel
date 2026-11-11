#!/usr/bin/env python3
"""gen_items.py on the 1/64 m item grid (docs/VoxelFoundationPlan.md 7a.3 step 4).

The items are authored in 1/81 m cells (flat projected boards in 1/27 m). On grid 64 the authored canvas is
resampled at emit time: which output cells exist comes from mapping every authored cell EDGE to the nearest
output edge (thin parts keep >= 1 cell, touching parts keep touching); what an output cell holds is the
authored cell whose centre is nearest its centre (so the result does not depend on painting order).
Contracts:
  * legacy output (files + manifest + catalog) equals the unmodified generator's (digest pinned);
  * grid 64: every item is written as '# grid: 64'; extents within one output cell of legacy; the same
    (material, tint) set except where a single authored cell had no output cell of its own; grip points
    inside the item and within one cell of the legacy grip.

Run: python -m pytest tools/test_gen_items_grid.py -q
"""

import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_items as gi  # noqa: E402

# sha256 over (relpath, LF content) of everything the UNMODIFIED generator (commit 302f8817) wrote when
# redirected into an empty root -- see _run.
LEGACY_DIGEST = "c2c328cde1aab1b432aee64220579107639f51a51d86a907de62d8627307c563"   # 54 files


def _run(mod, root, grid=None):
    saved = {k: getattr(mod, k) for k in ("ROOT", "OUT_WEAPONS", "OUT_ITEMS", "OUT_TEST", "MANIFEST", "CATALOG")}
    argv = sys.argv
    try:
        t = os.path.join(root, "resources", "templates")
        mod.ROOT = str(root)
        mod.OUT_WEAPONS, mod.OUT_ITEMS, mod.OUT_TEST = (os.path.join(t, d) for d in ("weapons", "items", "test"))
        mod.MANIFEST = os.path.join(t, "items_manifest.json")
        mod.CATALOG = os.path.join(t, "template_catalog.json")
        sys.argv = ["gen_items.py"] + (["--grid", str(grid)] if grid else [])
        mod.main()
    finally:
        sys.argv = argv
        for k, v in saved.items():
            setattr(mod, k, v)


def digest(root):
    h = hashlib.sha256()
    files = []
    for d, _, fs in os.walk(root):
        for f in fs:
            files.append(os.path.relpath(os.path.join(d, f), root).replace("\\", "/"))
    for rel in sorted(files):
        with open(os.path.join(root, rel), "rb") as f:
            h.update(rel.encode() + b"\0" + f.read().replace(b"\r\n", b"\n") + b"\0")
    return len(files), h.hexdigest()


def _cells(path):
    grid, cells = None, {}
    for ln in open(path, encoding="utf-8", errors="replace"):   # legacy headers are cp1252 (em dash 0x97)
        t = ln.split()
        if ln.startswith("# grid:"):
            grid = int(ln.split(":")[1])
        elif t and t[0] in ("V", "B"):
            x, y, z = map(int, t[1:4])
            sx, sy, sz, rest = (1, 1, 1, t[4:]) if t[0] == "V" else (*map(int, t[4:7]), t[7:])
            mat = rest[0]
            tint = next((r[5:] for r in rest[1:] if r.startswith("tint=")), None)
            for i in range(sx):
                for j in range(sy):
                    for k in range(sz):
                        cells[(x + i, y + j, z + k)] = (mat, tint)
    return grid, cells


def test_legacy_output_is_the_unmodified_generators(tmp_path):
    _run(gi, tmp_path)
    n, d = digest(tmp_path)
    assert (n, d) == (54, LEGACY_DIGEST)


def test_grid64_items_conform(tmp_path):
    _run(gi, tmp_path / "legacy")
    _run(gi, tmp_path / "g64", grid=64)
    t9 = tmp_path / "legacy" / "resources" / "templates"
    t64 = tmp_path / "g64" / "resources" / "templates_g8"
    man9 = json.load(open(t9 / "items_manifest.json"))
    man64 = json.load(open(t64 / "items_manifest.json"))
    assert set(man9) == set(man64) and len(man9) >= 50
    for name, e9 in man9.items():
        e64 = man64[name]
        g9, c9 = _cells(t9 / e9["file"])
        g64, c64 = _cells(t64 / e64["file"])
        assert g64 == 64 and e64["grid"] == 64, name
        for a, b in zip(e64["dims_units"], e9["dims_units"]):
            assert abs(a - b) <= 1 / 64 + 1e-6, (name, e64["dims_units"], e9["dims_units"])
        lost = set(c9.values()) - set(c64.values())
        assert set(c64.values()) <= set(c9.values()), name
        assert not lost, (name, lost)          # no detail vanishes (a one-cell fletching cap included)
        if "grip_point_units" in e9:
            gp = e64["grip_point_units"]
            cell = tuple(int(gp[i] * 64) for i in range(3))
            cell9 = tuple(int(v * g9) for v in e9["grip_point_units"])
            # held at a solid cell on both grids (bowl and lantern are held over a hollow, on both)
            assert (cell in c64) == (cell9 in c9), (name, gp)
            for a, b in zip(gp, e9["grip_point_units"]):
                assert abs(a - b) <= 1 / 64 + 1e-6, (name, gp, e9["grip_point_units"])
