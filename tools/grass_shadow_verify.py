#!/usr/bin/env python3
"""VERIFY the adaptive grass shadow-width fix on the single-blade rig.

Same rig that proved the bug: a 5x5 Stone slab (Stone emits no grass) with exactly ONE Grass
voxel flush at its centre, bladesPerVoxel = 1 -> one blade in the world. Wind off, day/night
paused, camera fixed, shadow-only view. One caster means ANY changed pixel is that blade's
shadow; the control is the rest of the frame staying identical.

BEFORE the fix (measured): 0 changed pixels at EVERY blade height (0.5-4.0), EVERY sun
elevation (90-16 deg) and shadow distances 40 and 80 — the blade never rasterized into the
shadow map at its authored 0.04 u width.

PASS CRITERIA: the blade must now cast at its AUTHORED width (bladeWidth x1) across the whole
shadow-distance range, because the clamp is adaptive. A fixed widening would pass at one
distance and fail at the others — that is exactly what this sweep is designed to catch.
"""
import json, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image

URL = "http://localhost:8090"
BLADE = (-3, 60, -3)   # centre of a 5x5 slab held INSIDE chunk (-1,1,-1) — see build_rig


def post(p, d, timeout=60):
    r = urllib.request.Request(URL + p, data=json.dumps(d).encode(),
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=timeout) as resp:
        b = resp.read().decode()
    return json.loads(b) if b.strip() else {}


def get(p, timeout=60):
    with urllib.request.urlopen(URL + p, timeout=timeout) as r:
        b = r.read().decode()
    return json.loads(b) if b.strip() else {}


def must(resp, what):
    """Assert on setup. A discarded place_voxel response (success:false) once silently emptied
    the rig and produced a whole table of zeros that looked like a real result."""
    if isinstance(resp, dict) and resp.get("success") is False:
        raise SystemExit(f"SETUP FAILED: {what} -> {resp}")
    return resp


def build_rig():
    # x/z -5..-1 keeps all 25 voxels in ONE chunk. A slab spanning 0 crosses into three
    # more chunks that are not resident in a fresh Flat world, and those fills drop silently
    # (placed=4 failed=21, measured 2026-08-05) — leaving a rig that looks built but isn't.
    must(post("/api/world/fill", {"x1": -5, "y1": 60, "z1": -5, "replace": True,
                                  "x2": -1, "y2": 60, "z2": -1, "material": "Stone"}), "slab")
    time.sleep(6)
    post("/api/world/voxel/remove", {"x": BLADE[0], "y": BLADE[1], "z": BLADE[2]})
    time.sleep(2)
    must(post("/api/world/voxel", {"x": BLADE[0], "y": BLADE[1], "z": BLADE[2],
                                   "material": "Grass"}), "grass voxel")
    time.sleep(3)
    if not get(f"/api/world/voxel?x={BLADE[0]}&y={BLADE[1]}&z={BLADE[2]}").get("exists"):
        raise SystemExit("rig empty — refusing to measure")
    print("[rig] verified: 5x5 slab + ONE flush grass voxel", flush=True)


def viewport(p):
    return np.asarray(Image.open(p).convert("L"), dtype=np.int16)[40:670, 245:1195]


def shot(dst):
    shutil.copyfile(get("/api/screenshot")["path"], dst)
    return dst


def measure(tag):
    must(post("/api/debug/grass", {"castShadows": True}), "cast on");  time.sleep(1.8)
    on = viewport(shot(f"docs/evidence/v_{tag}_on.png"))
    must(post("/api/debug/grass", {"castShadows": False}), "cast off"); time.sleep(1.8)
    off = viewport(shot(f"docs/evidence/v_{tag}_off.png"))
    must(post("/api/debug/grass", {"castShadows": True}), "restore")
    d = np.abs(on - off)
    hit = d > 4
    n = int(hit.sum())
    if not n:
        return 0, 0, 0.0
    ys, xs = np.nonzero(hit)
    return n, int(d.max()), float(np.hypot(xs.std(), ys.std()))


def main():
    for _ in range(60):
        try:
            get("/api/state"); break
        except Exception:
            time.sleep(3)

    build_rig()
    must(post("/api/debug/shadow", {"mode": 1}), "shadow view")
    must(post("/api/debug/grass", {"bladesPerVoxel": 1, "bladeHeight": 2.0,
                                   "bladeWidth": 1.0, "windStrength": 0.0}), "grass cfg")
    must(post("/api/daynight/set", {"enabled": True, "paused": True, "timeOfDay": 15.3}), "sun")
    must(post("/api/camera", {"mode": "free", "position": {"x": -6, "y": 67, "z": 4},
                              "yaw": -66.8, "pitch": -33.3}), "camera")
    time.sleep(8)

    print("\nAUTHORED blade width (x1) — the case that measured ZERO before the fix.")
    print(f"{'shadowDist':>11} {'texel~u':>9} {'changedPx':>10} {'maxDelta':>9} {'spread':>8}  result",
          flush=True)
    ok = True
    for dist in (40, 80, 160, 420):
        must(post("/api/debug/shadow", {"distance": dist}), f"dist {dist}")
        time.sleep(2.5)
        n, mx, sp = measure(f"d{dist}")
        texel = 2.0 * (dist * 1.28 + 48) / 8192.0
        good = n > 60
        ok &= good
        print(f"{dist:>11} {texel:>9.3f} {n:>10} {mx:>9} {sp:>8.1f}  "
              f"{'CASTS' if good else 'NO SHADOW'}", flush=True)

    print("\nCONTROL — grass removed from the world entirely; toggling casting must change NOTHING.")
    must(post("/api/debug/grass", {"enabled": False}), "grass off")
    time.sleep(2.5)
    n, mx, _ = measure("control")
    print(f"  no-grass control: changedPx={n} maxDelta={mx}  "
          f"{'PASS (nothing changes)' if n < 20 else 'FAIL — something else is moving!'}",
          flush=True)
    must(post("/api/debug/grass", {"enabled": True}), "grass on")

    must(post("/api/debug/shadow", {"mode": 0, "distance": 420}), "restore view")
    print(f"\nOVERALL: {'PASS' if ok and n < 20 else 'FAIL'}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
