#!/usr/bin/env python3
"""gen_seat_variants.py on the 1/8 m grid (docs/VoxelFoundationPlan.md 7a.3 step 4).

Contracts:
  * legacy output equals the committed library and lands in furniture/ (the taxonomy folder), not the root;
  * grid 8: each seat is a solid block re-picked in metres to the nearest 1/8 m cell (stool_low 0.625 x
    0.375 x 0.625 m, bench_great 2.0 x 1.375 x 1.375 m); the seat point sits on the block's top centre;
  * the seat-fit band of engine/include/scene/SeatFit.h (refuse when the seat is more than kFootDropMax =
    0.20 m above the leg length; knees rise when seat < 0.6 * leg) still gives the intended verdicts for
    the leg lengths named in the generator: ogre 1.58 m and goliath 1.27 m fit the great bench.

Run: python -m pytest tools/test_gen_seat_variants_grid.py -q
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_seat_variants as gs  # noqa: E402
import voxgrid as vg  # noqa: E402


def _content(p):
    with open(p, "rb") as f:
        return f.read().replace(b"\r\n", b"\n")


def _seat(lines):
    ln = next(l for l in lines if l.startswith("# interaction_point: seat_0"))
    return tuple(map(float, ln.split()[4:7]))


def test_legacy_matches_committed_library_in_furniture(tmp_path):
    gs.write_all(out_dir=tmp_path)
    for n in ("stool_low", "bench_great"):
        assert _content(os.path.join(tmp_path, n + ".voxel")) == \
            _content(os.path.join(REPO, "resources", "templates", "furniture", n + ".voxel")), n
    assert gs.default_out_dir(9).as_posix().endswith("resources/templates/furniture")


def test_grid8_seats_repicked_in_metres(tmp_path):
    gs.write_all(out_dir=tmp_path, grid=8)
    want = {"stool_low": (5, 3, 5), "bench_great": (16, 11, 11)}
    for n, dims in want.items():
        lines = open(os.path.join(tmp_path, n + ".voxel"), encoding="utf-8").read().splitlines()
        assert lines[0] == "# grid: 8"
        _, cells = vg.parse_grid(lines)
        assert len(cells) == dims[0] * dims[1] * dims[2], n           # solid block
        assert tuple(max(c[i] for c in cells) + 1 for i in range(3)) == dims, n
        x, y, z = _seat(lines)
        assert (x, y, z) == (dims[0] / 16, dims[1] / 8, dims[2] / 16), (n, x, y, z)
    bench_top = want["bench_great"][1] / 8
    for leg in (1.58, 1.27):                                    # ogre, goliath: fit
        assert 0.6 * leg <= bench_top <= leg + 0.20, leg
