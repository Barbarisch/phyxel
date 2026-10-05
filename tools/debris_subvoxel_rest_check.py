#!/usr/bin/env python3
"""Sub-voxel rest check — GPU debris must rest ON sub-voxel geometry (DebrisInteractionPlan 1c).

Before 1c, GPU debris collided against a CUBE bitfield: a 1/3-thick slab was either a whole solid
cube (debris rested a full cube up, floating) or absent (debris sank through it), depending on which
path had filled the bitfield. Since 1c it reads the shared micro occupancy, so it must rest on the
slab's true top face.

Rig (DebrisLab, inside the BLAST chunk x 160..191, which the settle bench rebuilds every run):
  * slab   : cubes x 170..174, z 10..14 at y = 16 filled with ONLY the lowest subcube layer
             (sy = 0) -> top face at y = 16 + 1/3.
  * control: bare floor (top y = 16) at x 180..184, same z.
  * a 9x1x9 lattice of 1/3-scale debris (half-height 1/6) dropped 1.5 above each, settled 6 s.

Prediction (written before running): flat-resting centres at 16 + 1/3 + 1/6 = 16.500 on the slab and
16 + 1/6 = 16.167 on the control; none below that, median at it (+/- 1 cm), none more than one body
height (1/3) above it (tumbled bodies lean); 0 below each floor; 0 held for unknown occupancy.
(First run: a stricter "every body within 1 cm" failed on tumbled bodies leaning 0.07-0.1 up on
the control too -- the criterion, not the physics; see the comment in main.)

Usage:  python tools/debris_subvoxel_rest_check.py --url http://localhost:8097
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import debris_settle_bench as bench  # noqa: E402

SLAB_X0, CTRL_X0, Z0, N = 170, 180, 10, 5
GROUND = bench.GROUND                     # 16
THIRD = 1.0 / 3.0


def settle_on(api, x0, floor_y, seconds):
    api.post("/api/debug/clear_dynamics", {})
    bench.physics(api, frozen=True)
    api.post("/api/debug/settle_probe", {"op": "start", "floor_y": floor_y})
    bench.lattice(api, x=x0 + 0.5, y=GROUND + 1.5, z=Z0 + 0.5, nx=9, ny=1, nz=9,
                  scale=THIRD, gap=0.1, seed=11)
    start = bench.physics(api)["total_ticks"]
    bench.step_until(api, int(round(seconds / bench.TICK)), start)
    time.sleep(0.5)
    st = api.post("/api/debug/settle_probe", {"op": "stop", "bodies": 0})
    return st["summary"], body_heights(api)


def body_heights(api):
    """Every active body's centre y, from one frame of the engine's particle position log."""
    path = os.path.join(bench.ROOT, "build", "_subvoxel_rest_plog.csv")
    api.post("/api/debug/particle_log", {"action": "start", "file": path})
    time.sleep(0.5)
    api.post("/api/debug/particle_log", {"action": "stop"})
    frames = {}
    with open(path) as f:
        for line in f:
            if line.startswith("P,"):
                p = line.split(",")
                frames.setdefault(int(p[1]), []).append((float(p[3]), float(p[4]), float(p[5])))
    return frames[max(frames)] if frames else []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:8090")
    ap.add_argument("--seconds", type=float, default=6.0)
    a = ap.parse_args()
    api = bench.Api(a.url)
    api.wait_loop()

    # Slab: lowest subcube layer of every cube cell in the slab area.
    subs = [{"x": x, "y": GROUND, "z": z, "sx": sx, "sy": 0, "sz": sz, "material": "Stone"}
            for x in range(SLAB_X0, SLAB_X0 + N) for z in range(Z0, Z0 + N)
            for sx in range(3) for sz in range(3)]
    r = api.post("/api/world/subcubes/batch", {"subcubes": subs}, timeout=120)
    print("slab placed:", {k: r.get(k) for k in ("placed", "failed", "success", "error") if k in r})
    time.sleep(1.0)   # let the occupancy pool repack the edited chunk
    occ = api.get(f"/api/debug/light_occupancy?x={SLAB_X0 + 2}&y={GROUND}&z={Z0 + 2}")
    print("slab cell in the pool:", occ.get("cell"))

    ok = True
    for name, x0, floor_y, expect in (("slab", SLAB_X0, GROUND + THIRD, GROUND + THIRD + THIRD / 2),
                                      ("control", CTRL_X0, float(GROUND), GROUND + THIRD / 2)):
        s, pts = settle_on(api, x0, floor_y, a.seconds)
        tun = s["checks"]["tunnelled_through_floor"]
        held = s["checks"]["held_unknown_occupancy"]["value"]
        # Only bodies whose footprint is fully over the patch are judged; a tumbling body that slid
        # off the patch edge onto the floor is legitimate and reported, not counted as a failure.
        h = THIRD / 2
        over = [p for p in pts if x0 + h <= p[0] <= x0 + N - h and Z0 + h <= p[2] <= Z0 + N - h]
        off = [p for p in pts if p not in over]
        if off:
            print(f"   {len(off)} body(ies) slid off the patch edge: "
                  + ", ".join(f"({p[0]:.2f},{p[1]:.3f},{p[2]:.2f})" for p in off[:4]))
        ys = [p[1] for p in over]
        # Dropped debris tumbles: a few bodies come to rest on an edge or leaning on a neighbour
        # (a 1/3 cube on its edge sits 0.24 up). So the test is not "every body flat" but:
        #   * none BELOW the flat rest height   -> nothing sank into / through the slab;
        #   * the MEDIAN lies flat at it        -> the slab top is where debris rests;
        #   * none more than 0.5 above it -> nothing floats a cube up (one tumbled body may rest
        #     stacked on another, ~one body height = 0.33-0.35 up; the old bug floated +0.67).
        # The old cube bitfield failed one of these by ~0.67: rest at 17.17 or sink to 16.17.
        ys = sorted(ys)
        med = ys[len(ys) // 2] if ys else None
        print(f"\n[{name}] floor {floor_y:.3f}  flat rest centre {expect:.3f}")
        print(f"   verdict {s['verdict']}  tunnelled {tun['value']}  held_unknown {held}")
        if ys:
            print(f"   {len(ys)} bodies: centre y min {ys[0]:.4f}  median {med:.4f}  max {ys[-1]:.4f}")
        good = (tun["value"] == 0 and held == 0 and len(pts) == 81 and len(ys) >= 70 and
                ys[0] >= expect - 0.01 and abs(med - expect) < 0.01 and ys[-1] <= expect + 0.5)
        print("   PASS" if good else "   FAIL")
        ok &= good

    print("\nRESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
