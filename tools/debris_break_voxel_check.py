#!/usr/bin/env python3
"""Live check for DebrisInteractionPlan 1d: every B-key break becomes GPU debris.

Drives POST /api/debug/break_voxel (the B-key break, no cursor hover) at all three levels on the
DebrisLab blast chunk (x 160..191, restored afterwards) and asserts, per break:
  * the voxel is removed (store) and the store / physics grid / packed pool agree (occupancy_diff);
  * exactly ONE GPU piece is queued and none refused;
and finally that every piece settles (no body left awake after the settle window). (Until 1d
part 3 it also asserted cpu_dynamic stayed 0; the CPU single-box path is now deleted.)

Prediction written before the run: removed=True, gpu_pieces=1, refused=0 for each level;
occ diff 0; all pieces asleep within 6 s.

  python tools/debris_break_voxel_check.py --url http://localhost:8097
"""
import argparse
import sys
import time

import debris_settle_bench as bench

X, Z = 170, 16
G = bench.GROUND          # first air layer above the slab


def occ_ok(api):
    x0 = 32 * bench.BLAST_CHUNK
    d = api.post("/api/debug/occupancy_diff", {"x1": x0, "y1": 0, "z1": 0, "x2": x0 + 31, "y2": 31, "z2": 31})
    return d.get("agrees") is True, d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:8090")
    a = ap.parse_args()
    api = bench.Api(a.url)
    api.wait_loop()
    api.post("/api/debug/clear_dynamics", {})
    bench.restore_blast_site(api)

    # One target per level, stacked on the slab so the pieces fall onto it:
    #   a full cube at (X, G+1); a subcube cell at (X+3, G+1); a microcube cell at (X+6, G+1).
    api.post("/api/world/voxel", {"x": X, "y": G + 1, "z": Z, "material": "Stone"})
    api.post("/api/world/subcubes/batch", {"subcubes": [
        {"x": X + 3, "y": G + 1, "z": Z, "sx": 1, "sy": 1, "sz": 1, "material": "Stone"}]})
    api.post("/api/world/microcubes/batch", {"microcubes": [
        {"x": X + 6, "y": G + 1, "z": Z, "sx": 1, "sy": 1, "sz": 1, "mx": 1, "my": 1, "mz": 1, "material": "Stone"}]})
    time.sleep(1.5)   # let the pool repack the edited chunk
    ok, d = occ_ok(api)
    print("setup occupancy agrees:", ok, {k: d.get(k) for k in ("cell_mismatches", "grid_mismatches", "cells_pool_unknown")})

    api.post("/api/debug/settle_probe", {"op": "start", "floor_y": G})
    breaks = [
        {"x": X, "y": G + 1, "z": Z, "level": "cube"},
        {"x": X + 3, "y": G + 1, "z": Z, "level": "subcube", "sub": [1, 1, 1]},
        {"x": X + 6, "y": G + 1, "z": Z, "level": "microcube", "sub": [1, 1, 1], "micro": [1, 1, 1]},
    ]
    # Break all three first, then settle, then the (slow, ~seconds each in Debug) occupancy
    # comparisons: pieces live 25 s, and interleaving the diffs let the first piece EXPIRE before
    # the settle check (a test-timing artefact, measured 2026-10-05).
    fails = 0
    results = []
    for b in breaks:
        r = api.post("/api/debug/break_voxel", b)
        results.append((b, r))
        good = r.get("removed") is True and r.get("gpu_pieces") == 1 and r.get("refused") == 0
        fails += not good
        print(f"{b['level']:9s} removed={r.get('removed')} gpu_pieces={r.get('gpu_pieces')} "
              f"refused={r.get('refused')} -> {'OK' if good else 'FAIL'}")

    time.sleep(6.0)
    st = api.post("/api/debug/settle_probe", {"op": "status", "series_last": 1})
    row = (st.get("series") or [{}])[-1]
    api.post("/api/debug/settle_probe", {"op": "stop", "bodies": 0})
    active = bench.physics(api).get("active")
    print(f"after 6 s: active={active} awake={row.get('awake')} below_floor={row.get('below_floor')}")
    if active != 3 or row.get("awake") != 0 or row.get("below_floor"):
        fails += 1
        print("FAIL: expected 3 pieces, all asleep, none below the slab")

    ok, d = occ_ok(api)
    print(f"occupancy agrees={ok}")
    if not ok:
        fails += 1
        print("   ", {k: d.get(k) for k in ("cell_mismatches", "grid_mismatches", "first_mismatches")})

    api.post("/api/debug/clear_dynamics", {})
    bench.restore_blast_site(api)
    print("RESULT:", "PASS" if fails == 0 else f"FAIL ({fails})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
