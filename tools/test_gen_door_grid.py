#!/usr/bin/env python3
"""gen_door.py on the 1/8 m grid (docs/VoxelFoundationPlan.md 7a.3 step 4).

Contracts:
  * legacy output (subcube leaf) equals the committed library, and lands in architecture/ (the taxonomy
    folder since 2026-08-07), never at the template root;
  * grid 8: the door is designed in metres -- a 1 m x 2 m (or 2 m x 2 m) leaf one cell deep, a one-cell
    (0.125 m) Log frame on the perimeter + mid rail (+ centre mullion on wide doors), Wood panels, a Gold
    knob one cell proud on the latch side just under the mid rail, and the handle interaction point at the
    knob's centre.

Run: python -m pytest tools/test_gen_door_grid.py -q
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_door as gd  # noqa: E402
import voxgrid as vg  # noqa: E402

DOORS = (("door_wood", 1, 2), ("door_wood_wide", 2, 2))


def _content(p):
    with open(p, "rb") as f:
        return f.read().replace(b"\r\n", b"\n")


def test_legacy_matches_committed_library_in_architecture(tmp_path):
    for name, w, h in DOORS:
        gd.write_door(name, w, h, out_dir=tmp_path)
        for ext in (".voxel", ".metrics.json"):
            got = _content(os.path.join(tmp_path, name + ext))
            ref = _content(os.path.join(REPO, "resources", "templates", "architecture", name + ext))
            assert got == ref, name + ext
    assert gd.default_out_dir(9) == gd.TEMPLATES / "architecture"


def test_grid8_door_is_designed_in_metres(tmp_path):
    for name, w, h in DOORS:
        gd.write_door(name, w, h, out_dir=tmp_path, grid=8)
        lines = open(os.path.join(tmp_path, name + ".voxel"), encoding="utf-8").read().splitlines()
        assert lines[0] == "# grid: 8"
        n, cells = vg.parse_grid(lines)
        W, H = 8 * w, 8 * h
        leaf = {k: m for k, m in cells.items() if k[2] == 0}
        assert len(leaf) == W * H, (name, len(leaf))                      # a full 1-cell-deep leaf
        mid = H // 2
        for (x, y, z), m in leaf.items():
            frame = x in (0, W - 1) or y in (0, H - 1, mid) or (w > 1 and x == W // 2)
            assert m == ("Log" if frame else "Wood"), (name, x, y, m)
        knob = {k: m for k, m in cells.items() if k[2] != 0}
        assert set(knob.values()) == {"Gold"} and all(k[2] == 1 for k in knob), (name, knob)
        assert all(x > W // 2 and y < mid for x, y, _ in knob), (name, knob)   # latch side, under the rail
        ip = [l for l in lines if l.startswith("# interaction_point: handle_0 door_handle")]
        assert len(ip) == 1, name
        hx, hy, hz = map(float, ip[0].split()[4:7])
        kx = sum(k[0] + 0.5 for k in knob) / len(knob) / 8
        ky = sum(k[1] + 0.5 for k in knob) / len(knob) / 8
        assert abs(hx - kx) < 1e-3 and abs(hy - ky) < 1e-3, (name, hx, hy, kx, ky)
