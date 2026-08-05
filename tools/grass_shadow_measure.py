#!/usr/bin/env python3
"""Measure whether ONE grass blade casts a shadow. Assumes the rig is already built and
visually confirmed: a 5x5 Stone slab at y=60 with a single Grass voxel FLUSH at its centre,
one blade, no wind, day/night paused, camera fixed.

DOES NOT rebuild the rig — the confirmed world is not to be disturbed.

Method: toggle ONLY grass shadow-casting and diff the frame in shadow-only view. There is
exactly one caster in the world, so any changed pixel IS that blade's shadow. Reports the
changed-pixel count, the peak delta, and the centroid of the change (a real shadow is one
compact blob adjacent to the blade; scatter would mean noise).

EVERY setup call is checked. The previous harness discarded the response from place_voxel,
which was returning success:false the whole time — so an entire four-phase sweep ran against
a world containing no grass at all, and produced a table of zeros that looked like a result.
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
BLADE = (0, 60, 0)


def post(path, payload, timeout=60):
    req = urllib.request.Request(URL + path, data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read().decode()
    return json.loads(body) if body.strip() else {}


def get(path, timeout=60):
    with urllib.request.urlopen(URL + path, timeout=timeout) as r:
        body = r.read().decode()
    return json.loads(body) if body.strip() else {}


def must(resp, what):
    """Fail LOUDLY on a bad setup response instead of measuring a broken rig."""
    if isinstance(resp, dict) and resp.get("success") is False:
        raise SystemExit(f"SETUP FAILED: {what} -> {resp}")
    return resp


def verify_rig():
    v = get(f"/api/world/voxel?x={BLADE[0]}&y={BLADE[1]}&z={BLADE[2]}")
    if not v.get("exists"):
        raise SystemExit(f"SETUP FAILED: no voxel at {BLADE} — the rig is empty, refusing to measure")
    print(f"[rig] grass voxel present at {BLADE}", flush=True)


def viewport(path):
    return np.asarray(Image.open(path).convert("L"), dtype=np.int16)[40:670, 245:1195]


def shot(dest):
    p = get("/api/screenshot").get("path")
    shutil.copyfile(p, dest)
    return dest


def measure(tag):
    must(post("/api/debug/grass", {"castShadows": True}), "castShadows on")
    time.sleep(1.8)
    on = viewport(shot(f"docs/evidence/m_{tag}_on.png"))
    must(post("/api/debug/grass", {"castShadows": False}), "castShadows off")
    time.sleep(1.8)
    off = viewport(shot(f"docs/evidence/m_{tag}_off.png"))
    must(post("/api/debug/grass", {"castShadows": True}), "castShadows restore")

    d = np.abs(on - off)
    hit = d > 4
    n = int(hit.sum())
    if n:
        ys, xs = np.nonzero(hit)
        cx, cy = float(xs.mean()), float(ys.mean())
        spread = float(np.hypot(xs.std(), ys.std()))
    else:
        cx = cy = spread = 0.0
    return n, int(d.max()), cx, cy, spread


def sun_elev():
    d = post("/api/daynight/set", {}).get("daynight", {}).get("sunDirection", {})
    return math.degrees(math.asin(max(-1.0, min(1.0, -float(d.get("y", 0.0))))))


def row(label, n, mx, cx, cy, sp):
    print(f"{label:>14} {n:>10} {mx:>9} {sp:>8.1f}  ({cx:6.0f},{cy:6.0f})  "
          f"{'CASTS' if n > 60 else 'NO SHADOW'}", flush=True)


def main():
    verify_rig()
    must(post("/api/debug/shadow", {"mode": 1}), "shadow-only view")
    must(post("/api/debug/grass", {"bladesPerVoxel": 1, "windStrength": 0.0}), "freeze grass")
    must(post("/api/daynight/set", {"enabled": True, "paused": True, "timeOfDay": 15.3}), "sun")
    time.sleep(3)
    print(f"[rig] sun elevation {sun_elev():.1f} deg\n", flush=True)

    hdr = f"{'':>14} {'changedPx':>10} {'maxDelta':>9} {'spread':>8}  {'centroid':>15}"

    print("=== PHASE 1: blade HEIGHT   (width x1, shadow dist 40) ===", flush=True)
    must(post("/api/debug/shadow", {"distance": 40}), "dist 40")
    must(post("/api/debug/grass", {"bladeWidth": 1.0}), "width 1")
    print(hdr, flush=True)
    for h in (0.5, 1.0, 2.0, 4.0):
        must(post("/api/debug/grass", {"bladeHeight": h}), f"height {h}")
        time.sleep(2)
        row(f"h={h}", *measure(f"h{h}"))

    print("\n=== PHASE 2: blade WIDTH   (height 2.0, shadow dist 40) ===", flush=True)
    must(post("/api/debug/grass", {"bladeHeight": 2.0}), "height 2")
    print(hdr, flush=True)
    for w in (1.0, 2.0, 4.0, 8.0):
        must(post("/api/debug/grass", {"bladeWidth": w}), f"width {w}")
        time.sleep(2)
        row(f"w=x{w} ({0.040*w:.2f}u)", *measure(f"w{w}"))
    must(post("/api/debug/grass", {"bladeWidth": 1.0}), "width reset")

    print("\n=== PHASE 3: SUN elevation   (height 2.0, width x1, dist 40) ===", flush=True)
    print(hdr, flush=True)
    for tod in (12.0, 14.0, 15.3, 16.4, 17.2):
        must(post("/api/daynight/set", {"timeOfDay": tod}), f"tod {tod}")
        time.sleep(2)
        row(f"sun {sun_elev():.0f}deg", *measure(f"tod{tod}"))

    print("\n=== PHASE 4: SHADOW DISTANCE   (height 2.0, width x1, sun 45deg) ===", flush=True)
    must(post("/api/daynight/set", {"timeOfDay": 15.3}), "sun 45")
    print(hdr, flush=True)
    for dist in (40, 80, 160, 420):
        must(post("/api/debug/shadow", {"distance": dist}), f"dist {dist}")
        time.sleep(2)
        texel = 2.0 * (dist * 1.28 + 48) / 8192.0
        row(f"d={dist} ({texel:.3f}u)", *measure(f"d{dist}"))

    must(post("/api/debug/shadow", {"mode": 0, "distance": 40}), "restore view")
    print("\ndone — images: docs/evidence/m_*.png", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
