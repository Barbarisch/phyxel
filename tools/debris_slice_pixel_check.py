#!/usr/bin/env python3
"""Visual check for DebrisInteractionPlan 1d texture parity: a broken piece keeps its texture slice.

Rig (DebrisLab blast chunk, restored afterwards): one cube cell built from 27 Bricks subcubes,
camera head-on to its +Z face (yaw -90, pitch 0), GPU solver FROZEN so a broken piece stays
exactly where its static subcube was. Three captures at the same pose:
  A  static cube (27 subcubes)
  B  after breaking the top-left front subcube (0,2,2) and the centre front subcube (1,1,2) —
     their pieces now sit, frozen, in the static subcubes' place
  C  the same 27 subcubes in Stone: same geometry, so the SAME ground shadow (an emissive
     material lit the ground and leaked into the mask). A vs C
     differs only on the cube's faces, which gives the face's pixel box, split 3x3. (Removing
     the cube instead moved its shadow into the mask and mis-placed the grid.)
Per cell, the normalised correlation of A vs B (texture PATTERN, insensitive to the different
light model debris uses).

Prediction (written before the run):
  * unbroken cells: corr ~1 (nothing changed) — the noise floor;
  * the CENTRE piece: high corr in both old and new code (the old shader always drew the centre
    slice, so the centre is the CONTROL that the metric accepts a correct piece);
  * the CORNER piece: high corr with texture parity; LOW with the old hard-coded centre slice.
PASS = corner corr >= 0.8 and centre corr >= 0.8.

  python tools/debris_slice_pixel_check.py --url http://localhost:8097 [--tag NAME]
"""
import argparse
import os
import sys
import time

import numpy as np
from PIL import Image

import debris_settle_bench as bench

CX, CY, CZ = 172, bench.GROUND + 1, 16     # the cube cell, one cube above the slab
OUT = os.path.join(bench.ROOT, "docs", "evidence", "debris_slice")


def grab(api, name, tag):
    os.makedirs(os.path.join(OUT, tag), exist_ok=True)
    dst = bench.screenshot(api, os.path.join(OUT, tag, name + ".png"))
    return np.asarray(Image.open(dst).convert("L"), dtype=np.float64)


def corr(a, b):
    a = a - a.mean(); b = b - b.mean()
    d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 0 else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:8090")
    ap.add_argument("--tag", default=time.strftime("%Y%m%d-%H%M%S"))
    a = ap.parse_args()
    api = bench.Api(a.url)
    api.wait_loop()
    api.post("/api/debug/clear_dynamics", {})
    bench.restore_blast_site(api)

    subs = [{"x": CX, "y": CY, "z": CZ, "sx": x, "sy": y, "sz": z, "material": "Bricks"}
            for x in range(3) for y in range(3) for z in range(3)]
    api.post("/api/world/subcubes/batch", {"subcubes": subs})
    api.post("/api/camera", {"position": {"x": CX + 0.5, "y": CY + 0.5, "z": CZ + 3.2},
                             "yaw": -90.0, "pitch": 0.0})
    bench.physics(api, frozen=True)
    time.sleep(2.0)
    A = grab(api, "A_static", a.tag)

    for sub in ([0, 2, 2], [1, 1, 2]):
        r = api.post("/api/debug/break_voxel", {"x": CX, "y": CY, "z": CZ, "level": "subcube", "sub": sub})
        print("break", sub, {k: r.get(k) for k in ("removed", "gpu_pieces", "refused")})
    time.sleep(2.0)
    B = grab(api, "B_broken_frozen", a.tag)

    api.post("/api/debug/clear_dynamics", {})
    bench.world_job(api, "/api/world/clear", {"x1": CX, "y1": CY, "z1": CZ, "x2": CX, "y2": CY, "z2": CZ})
    api.post("/api/world/subcubes/batch", {"subcubes": [dict(s, material="Stone") for s in subs]})
    time.sleep(2.0)
    C = grab(api, "C_stone_mask", a.tag)
    bench.physics(api, frozen=False)
    bench.restore_blast_site(api)

    mask = np.abs(A - C) > 20.0
    if mask.sum() < 100:
        print("FAIL: could not find the cube face in the capture"); return 1
    # The face is the dense block of the mask: rows/columns where at least half the peak count
    # of mask pixels sit (stray pixels elsewhere cannot widen the box).
    rows, cols = mask.sum(axis=1), mask.sum(axis=0)
    ry, rx = np.nonzero(rows >= 0.5 * rows.max())[0], np.nonzero(cols >= 0.5 * cols.max())[0]
    x0, x1, y0, y1 = float(rx.min()), float(rx.max()), float(ry.min()), float(ry.max())
    print(f"cube face box: x {x0:.0f}..{x1:.0f}  y {y0:.0f}..{y1:.0f}")
    cw, ch = (x1 - x0) / 3.0, (y1 - y0) / 3.0
    res = {}
    for row in range(3):          # row 0 = top = subcube y 2
        line = []
        for col in range(3):      # col 0 = left = subcube x 0 (camera looks -Z, right = +X)
            cx0, cy0 = x0 + col * cw, y0 + row * ch
            sl = (slice(int(cy0 + 0.2 * ch), int(cy0 + 0.8 * ch)), slice(int(cx0 + 0.2 * cw), int(cx0 + 0.8 * cw)))
            c = corr(A[sl], B[sl])
            res[(col, 2 - row)] = c
            line.append(f"{c:6.3f}")
        print("   ".join(line))
    corner, centre = res[(0, 2)], res[(1, 1)]
    others = [v for k, v in res.items() if k not in ((0, 2), (1, 1))]
    print(f"corner piece corr {corner:.3f} | centre piece (control) {centre:.3f} | unbroken min {min(others):.3f}")
    ok = corner >= 0.8 and centre >= 0.8
    print("RESULT:", "PASS" if ok else "FAIL", "| evidence:", os.path.join(OUT, a.tag))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
