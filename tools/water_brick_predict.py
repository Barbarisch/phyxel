"""WaterCore.md 26.17 - predict how many cells the GPU stores for the creek bench's water, per brick shape.

Applies WaterCoreGpu's residency rule (a brick holding water records its water's distance to each face; a neighbour brick is
stored when every axis's gap is within the reach - Chebyshev; liquid reach = 4 + travel) to the bench's carve (the same
bed_top as tools/water_creek_bench.py) and a measured depth profile (tools/water_creek_profile.py). Checked against the live
record before it is trusted for other shapes (26.17: 1,316 predicted vs 1,351 live for 8^3).

    python tools/water_brick_predict.py [--profile docs/evidence/water_core_h1/creekbench_pond_g9_q0.2_profile.json] [--travel 2]
"""
import argparse, json, math, pathlib, sys
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import water_creek_bench as B

H = 9   # cells per metre


def bench_grid(depth):
    """the pond volume's box (water_creek_bench.run_pond): voxels x 10-109, y 14-17, z 29-34; ground top at y 16 m, the
    channel z 31-32 carved down to bed_top; water from the bed to bed + depth(x)"""
    org = np.array([B.X0 * H, 14 * H, (B.Z0 - 2) * H])
    nx, ny, nz = (B.X1 - B.X0 + 1) * H, 4 * H, (B.Z1 - B.Z0 + 5) * H
    solid = np.zeros((nx, ny, nz), bool); wet = np.zeros((nx, ny, nz), bool)
    z0, z1 = 2 * H, (B.Z1 - B.Z0 + 3) * H
    for X in range(B.X0, B.X1 + 1):
        bed = int(round((B.bed_top(X) - 14) * H)); top = int(math.ceil((B.bed_top(X) + depth.get(X, 0.0) - 14) * H))
        xs = slice((X - B.X0) * H, (X - B.X0 + 1) * H)
        solid[xs, :2 * H, :] = True
        solid[xs, bed:2 * H, z0:z1] = False
        wet[xs, bed:top, z0:z1] = True
    return org, solid, wet


def predict(org, solid, wet, bs, reach, skip_solid=False):
    bs = np.array(bs); n = np.array(wet.shape); o = org % bs
    bn = (n + o + bs - 1) // bs
    need = np.zeros(bn, bool)
    q = np.argwhere(wet) + o
    recs = {}
    for bb, ll in zip(map(tuple, q // bs), q % bs):
        d = (ll[0], bs[0] - 1 - ll[0], ll[1], bs[1] - 1 - ll[1], ll[2], bs[2] - 1 - ll[2])
        r = recs.get(bb)
        recs[bb] = d if r is None else tuple(min(a, c) for a, c in zip(r, d))
    for bb, d in recs.items():
        lo, hi = [0, 0, 0], [0, 0, 0]
        for a in range(3):
            k = 1
            while d[2 * a] + 1 + (k - 1) * bs[a] <= reach and bb[a] - k >= 0: lo[a] = -k; k += 1
            k = 1
            while d[2 * a + 1] + 1 + (k - 1) * bs[a] <= reach and bb[a] + k < bn[a]: hi[a] = k; k += 1
        need[bb[0] + lo[0]:bb[0] + hi[0] + 1, bb[1] + lo[1]:bb[1] + hi[1] + 1, bb[2] + lo[2]:bb[2] + hi[2] + 1] = True
    if skip_solid:   # 26.18: bricks whose every in-box cell is Solid and that hold no water
        pad = np.ones(tuple(bn * bs), bool)
        pad[o[0]:o[0] + n[0], o[1]:o[1] + n[1], o[2]:o[2] + n[2]] = solid
        need &= ~pad.reshape(bn[0], bs[0], bn[1], bs[1], bn[2], bs[2]).all(axis=(1, 3, 5))
    nb = int(need.sum())
    return nb, nb * int(bs.prod())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", default=str(B.EV / "creekbench_pond_g9_q0.2_profile.json"))
    ap.add_argument("--travel", type=int, default=2, help="cells the call can move water (WaterCoreGpu: ceil(vmax dt / h) + 1)")
    args = ap.parse_args()
    depth = {p["x"]: (p["depth"] or 0.0) for p in json.load(open(args.profile))["profile"]}
    org, solid, wet = bench_grid(depth)
    print(f"water cells {int(wet.sum())}, reach {4 + args.travel}")
    for bs in ((8, 8, 8), (8, 4, 8), (8, 2, 8), (4, 4, 4)):
        for sk in (False, True):
            nb, cells = predict(org, solid, wet, bs, 4 + args.travel, sk)
            print(f"  brick {bs[0]}x{bs[1]}x{bs[2]}{' (no all-bed bricks)' if sk else '':21s} bricks {nb:6d}  cells {cells:8d}  ({cells / wet.sum():.1f} per water cell)")


if __name__ == "__main__":
    main()
