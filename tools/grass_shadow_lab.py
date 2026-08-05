#!/usr/bin/env python3
"""SINGLE-BLADE grass shadow rig — does one blade of grass cast a shadow?

WHY THIS EXISTS. Grass-blade shadows were "verified" repeatedly by eyeballing the full forest
world, and the conclusion flipped five times. The readings were worthless because the scene
contained ~10^5 blades plus trees, terrain relief, wind and a moving sun, and because
`bladesPerVoxel = 1` means one blade PER GRASS VOXEL, not one blade. This rig fixes that:

  * a Stone plane emits NO grass (blades spawn only on grass-topped voxels),
  * exactly ONE Grass voxel is placed on it, with bladesPerVoxel = 1 -> ONE blade in the world,
  * flora/trees disabled, wind off, day/night paused, camera pinned,
  * shadow-only debug view, so no albedo can mask a thin feature.

With a single caster, ANY pixel that changes when shadow-casting is toggled IS that blade's
shadow. The control is the rest of the frame: a working blade shadow shows up as ONE compact
cluster of changed pixels, not scattered noise.

Sweeps ONE variable at a time (blade height, then sun elevation, then shadow distance) and
prints a table. Requires the GrassShadowLab project to be running.

  python tools/grass_shadow_lab.py
"""

import json
import math
import shutil
import sys
import time
import urllib.request

import numpy as np
from PIL import Image

URL = "http://localhost:8090"

# Rig geometry (world units).
#
# The plane FLOATS well above the generated terrain (Flat world surfaces near y=16). That is a
# deliberate cost decision: clearing the terrain instead meant a ~1.15 M voxel region op that
# stalled the engine for minutes. Since the sun is above, terrain grass below can only cast
# DOWNWARD — it can never shadow a plane above it — so it cannot contaminate this measurement
# and does not need removing. The rig is now ~6.5 k voxels and builds in seconds.
PLANE_TOP_Y = 60
# The single Grass voxel is FLUSH — it REPLACES a Stone voxel in the plane. One voxel proud
# would make the BLOCK a caster: its vertical faces would shadow the plane and contaminate the
# very measurement this rig exists for. Flush => zero height variation, blade is the ONLY
# thing above the plane.
BLADE_VOXEL = (0, PLANE_TOP_Y, 0)
PLANE_HALF  = 40          # 81x81 voxels — fills the view, cheap to build


def post(path, payload, timeout=90):
    req = urllib.request.Request(URL + path, data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read().decode()
    return json.loads(body) if body.strip() else {}


def shot(dest):
    with urllib.request.urlopen(URL + "/api/screenshot", timeout=90) as r:
        p = json.loads(r.read().decode()).get("path")
    shutil.copyfile(p, dest)
    return dest


def viewport(path):
    """Crop to the 3D viewport so UI panels can never contribute a 'difference'."""
    return np.asarray(Image.open(path).convert("L"), dtype=np.int16)[40:670, 245:1195]


def build_rig():
    """One Stone plane + exactly one Grass voxel. No clearing (see PLANE_TOP_Y note)."""
    n = (2 * PLANE_HALF + 1) ** 2
    print(f"[rig] laying {n} Stone voxels at y={PLANE_TOP_Y} ...", flush=True)
    post("/api/world/fill", {"x1": -PLANE_HALF, "y1": PLANE_TOP_Y, "z1": -PLANE_HALF,
                             "x2":  PLANE_HALF, "y2": PLANE_TOP_Y, "z2":  PLANE_HALF,
                             "material": "Stone"})
    time.sleep(8)
    print("[rig] placing THE single Grass voxel (flush with the plane)", flush=True)
    post("/api/world/voxel", {"x": BLADE_VOXEL[0], "y": BLADE_VOXEL[1], "z": BLADE_VOXEL[2],
                              "material": "Grass"})
    time.sleep(4)
    print("[rig] done", flush=True)


def freeze_scene():
    """Everything that could vary between two frames is pinned here."""
    post("/api/debug/grass", {"bladesPerVoxel": 1, "windStrength": 0.0, "castShadows": True})
    post("/api/debug/shadow", {"mode": 1})          # shadow-only view
    post("/api/daynight/set", {"enabled": True, "paused": True})


def aim_camera():
    """Fixed pose. Camera sits on the -X/-Z side; the sun is placed so the shadow falls toward
    +X/+Z, i.e. AWAY from the camera — so the blade can never occlude its own shadow (the flaw
    that made an earlier 'widened blade' run measure LESS shadow, not more)."""
    post("/api/camera", {"mode": "free",
                         "position": {"x": -7.0, "y": 74.0, "z": -7.0},
                         "yaw": 45.0, "pitch": -52.0})


def sun_elevation_deg():
    d = post("/api/daynight/set", {}).get("daynight", {}).get("sunDirection", {})
    y = -float(d.get("y", 0.0))
    return math.degrees(math.asin(max(-1.0, min(1.0, y))))


def measure(tag):
    """Toggle ONLY shadow casting and diff. One caster in the world => any change is its shadow."""
    post("/api/debug/grass", {"castShadows": True});  time.sleep(2.5)
    on = viewport(shot(f"docs/evidence/lab_{tag}_on.png"))
    post("/api/debug/grass", {"castShadows": False}); time.sleep(2.5)
    off = viewport(shot(f"docs/evidence/lab_{tag}_off.png"))
    post("/api/debug/grass", {"castShadows": True})

    d = np.abs(on - off)
    changed = d > 4
    n = int(changed.sum())
    if n:
        ys, xs = np.nonzero(changed)
        # Compactness: a real shadow is ONE blob. Scattered pixels would mean noise.
        spread = float(np.hypot(xs.std(), ys.std()))
    else:
        spread = 0.0
    return n, int(d.max()), spread


def main():
    for _ in range(60):
        try:
            post("/api/state", {}) if False else urllib.request.urlopen(URL + "/api/state", timeout=3)
            break
        except Exception:
            time.sleep(3)

    build_rig()
    freeze_scene()
    aim_camera()
    time.sleep(6)

    print("\n=== PHASE 1: does ONE blade cast at all?  (shadow distance 40 = fine texels) ===")
    post("/api/debug/shadow", {"distance": 40})
    post("/api/daynight/set", {"timeOfDay": 15.3})     # ~45 deg sun
    time.sleep(4)
    print(f"sun elevation: {sun_elevation_deg():.1f} deg")
    print(f"{'bladeHeight':>12} {'changedPx':>10} {'maxDelta':>9} {'spread':>8}  verdict", flush=True)
    for h in (0.5, 1.0, 2.0, 4.0):
        post("/api/debug/grass", {"bladeHeight": h})
        time.sleep(3)
        n, mx, sp = measure(f"h{h}")
        print(f"{h:>12} {n:>10} {mx:>9} {sp:>8.1f}  "
              f"{'CASTS' if n > 150 else 'no shadow'}", flush=True)

    print("\n=== PHASE 1b: blade WIDTH sweep at the DEFAULT 420u shadow distance ===")
    print("   texel there is ~0.125 u, so this is where width actually decides casting")
    post("/api/debug/shadow", {"distance": 420})
    post("/api/debug/grass", {"bladeHeight": 2.0})
    print(f"{'widthMult':>10} {'~width_u':>9} {'changedPx':>10} {'maxDelta':>9}  verdict", flush=True)
    for w in (1.0, 2.0, 4.0, 8.0):
        post("/api/debug/grass", {"bladeWidth": w})
        time.sleep(3)
        n, mx, _ = measure(f"w{w}")
        print(f"{w:>10} {0.040 * w:>9.3f} {n:>10} {mx:>9}  "
              f"{'CASTS' if n > 150 else 'no shadow'}", flush=True)
    post("/api/debug/grass", {"bladeWidth": 1.0})
    post("/api/debug/shadow", {"distance": 40})   # restore fine texels for phase 2

    print("\n=== PHASE 2: sun elevation sweep (blade height 2.0, width 1.0) ===")
    post("/api/debug/grass", {"bladeHeight": 2.0})
    print(f"{'timeOfDay':>10} {'sunElev':>8} {'changedPx':>10} {'maxDelta':>9}  verdict", flush=True)
    for tod in (12.0, 14.0, 15.3, 16.4, 17.2):
        post("/api/daynight/set", {"timeOfDay": tod})
        time.sleep(3)
        elev = sun_elevation_deg()
        n, mx, _ = measure(f"tod{tod}")
        print(f"{tod:>10} {elev:>7.1f} {n:>10} {mx:>9}  "
              f"{'CASTS' if n > 150 else 'no shadow'}", flush=True)

    print("\n=== PHASE 3: shadow distance (blade 2.0, sun 45 deg) — the DEFAULT-settings question ===")
    post("/api/daynight/set", {"timeOfDay": 15.3})
    print(f"{'shadowDist':>11} {'texel~u':>8} {'changedPx':>10} {'maxDelta':>9}  verdict", flush=True)
    for dist in (40, 80, 160, 420):
        post("/api/debug/shadow", {"distance": dist})
        time.sleep(3)
        texel = 2.0 * (dist * 1.28 + 48) / 8192.0
        n, mx, _ = measure(f"d{dist}")
        print(f"{dist:>11} {texel:>8.3f} {n:>10} {mx:>9}  "
              f"{'CASTS' if n > 150 else 'no shadow'}", flush=True)

    post("/api/debug/shadow", {"mode": 0, "distance": 420})
    print("\ndebug view off; images in docs/evidence/lab_*.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
