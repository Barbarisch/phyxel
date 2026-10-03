#!/usr/bin/env python3
"""Drop N GPU debris cubes a few metres in front of the current camera, in real time.

  python tools/debris_drop_here.py              # 40 cubes, 9 m ahead, 3 m above ground
  python tools/debris_drop_here.py 100 --height 6 --scale 0.333 --keep

Clears existing debris first unless --keep. Uses /api/debug/spawn_gpu_lattice (loose grid
with small gaps and random spin) — the same GPU solver path blast debris uses.
"""
import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import debris_settle_bench as b   # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("count", nargs="?", type=int, default=40)
    ap.add_argument("--distance", type=float, default=9.0, help="metres in front of the camera")
    ap.add_argument("--height", type=float, default=3.0, help="bottom layer height above ground")
    ap.add_argument("--scale", type=float, default=1.0, help="1 = cube, 0.333 = subcube")
    ap.add_argument("--gap", type=float, default=0.35)
    ap.add_argument("--spin", type=float, default=2.0)
    ap.add_argument("--material", default="Stone")
    ap.add_argument("--keep", action="store_true", help="keep existing debris")
    ap.add_argument("--url", default=os.environ.get("PHYXEL_API_URL", "http://localhost:8090"))
    a = ap.parse_args()

    api = b.Api(a.url)
    api.wait_loop()
    if not a.keep:
        api.post("/api/debug/clear_dynamics", {})
    b.physics(api, frozen=False)

    cam = api.get("/api/camera")
    p, yaw = cam["position"], cam["yaw"]
    cx = p["x"] + math.cos(math.radians(yaw)) * a.distance     # yaw -90 = -Z, 0 = +X
    cz = p["z"] + math.sin(math.radians(yaw)) * a.distance
    g = b.ground_top(api, int(math.floor(cx)), int(math.floor(cz)))

    # Near-cubic footprint: 2 layers when possible, fill the rest across.
    # Pick the lattice (layers x rows x cols, roughly square footprint) whose size is closest to
    # the request without going under it.
    best = None
    for ny in (1, 2, 3):
        for nx in range(1, a.count + 1):
            nz = math.ceil(a.count / (ny * nx))
            if nz < nx / 2 or nz > nx * 2:
                continue
            total = nx * ny * nz
            key = (total - a.count, abs(nx - nz), -ny)
            if best is None or key < best[0]:
                best = (key, nx, ny, nz)
    _, nx, ny, nz = best
    step = a.scale + a.gap
    r = api.post("/api/debug/spawn_gpu_lattice", {
        "x": cx - nx * step / 2, "y": g + a.height, "z": cz - nz * step / 2,
        "nx": nx, "ny": ny, "nz": nz, "scale": a.scale, "gap": a.gap, "spin": a.spin,
        "material": a.material, "seed": 7, "lifetime": 600.0})
    print(json.dumps({"spawned": r.get("spawned"), "at": [round(cx, 2), g + a.height, round(cz, 2)],
                      "ground_top": g}))


if __name__ == "__main__":
    main()
