#!/usr/bin/env python3
"""SWEEP the grass shadow-pass WIDTH MULTIPLIER on the single-blade rig.

Same rig as grass_shadow_verify.py — a 5x5 Stone slab with exactly ONE flush Grass voxel,
bladesPerVoxel = 1, wind off, sun pinned, shadow-only view — so every changed pixel belongs to
one blade. ONE variable moves here: `shadowWidthScale`, how many times wider than the real blade the
shadow proxy is drawn (POST /api/debug/grass).

WHY: shadow width is now tied to the BLADE, not to the shadow map — the old shadow-TEXEL clamp
made the shadow ~5x wider than its caster at a 420u shadow distance ("too wide, not
concentrated"). This sweep shows how footprint and darkness trade off as the proxy widens, so
the shipped default (2.0) rests on a measurement rather than on taste.

PREDICTION (state it before running, so the run can falsify it):
  - Area grows roughly linearly with the multiplier; PEAK darkness saturates quickly.
    Extra width buys blur, not contrast — which is why the old texel clamp read as a smudge.
  - At 1.0 the shadow is as wide as the blade and may go sub-texel at long shadow distances,
    where it stops casting rather than smearing (that trade is the whole point of the design).
  If instead area is FLAT across the sweep, width is not what spreads the shadow and the blur
  is coming from the PCSS penumbra instead — a different fix, in lighting.glsl.

CAVEAT the rig cannot remove: at bladesPerVoxel=1 the density compensation
(min(inversesqrt(densityFrac), 2.6) in grass.vert) sits at its 2.6x cap, so the rig blade is
~0.104u — about 2.6x WIDER than a blade in a dense field (~0.040u). Thresholds measured here
are optimistic for real grass.

METRICS
  area     changed pixels        -> how WIDE the shadow is        (the "too wide" complaint)
  peak     max |delta|           -> how DARK the darkest pixel is (the "not concentrated" one)
  solid%   pixels with delta>60  -> fraction that is real shadow rather than dither haze
"""
import json, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image

URL = "http://localhost:8090"
BLADE = (-3, 60, -3)   # centre of a 5x5 slab held INSIDE chunk (-1,1,-1) — see build_rig
SWEEP = (1.0, 1.5, 2.0, 3.0, 4.0)   # shadow width as a MULTIPLE of the real blade width
DISTS = (80, 420)          # near + the default; the clamp is adaptive so both must behave


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
    """A/B the ONE blade's casting against itself; everything else in frame is the control."""
    must(post("/api/debug/grass", {"castShadows": True}), "cast on");  time.sleep(1.8)
    on = viewport(shot(f"docs/evidence/w_{tag}_on.png"))
    must(post("/api/debug/grass", {"castShadows": False}), "cast off"); time.sleep(1.8)
    off = viewport(shot(f"docs/evidence/w_{tag}_off.png"))
    must(post("/api/debug/grass", {"castShadows": True}), "restore")
    d = np.abs(on - off)
    hit = d > 4
    n = int(hit.sum())
    if not n:
        return 0, 0, 0.0
    return n, int(d.max()), 100.0 * float((d > 60).sum()) / n


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

    for dist in DISTS:
        must(post("/api/debug/shadow", {"distance": dist}), f"dist {dist}")
        time.sleep(2.5)
        texel = 2.0 * (dist * 1.28 + 48) / 8192.0
        print(f"\nshadowDistance = {dist}   (one shadow texel ~= {texel:.3f} u; "
              f"rig blade ~= 0.104 u — density comp at its 2.6x cap, NOT the 0.040 u of dense grass)")
        print(f"{'widthX':>10} {'width~u':>9} {'area':>7} {'peak':>6} {'solid%':>7}  verdict",
              flush=True)
        for mt in SWEEP:
            r = must(post("/api/debug/grass", {"shadowWidthScale": mt}), f"widthScale {mt}")
            if "shadow_width_scale" not in r:
                raise SystemExit("engine ignored shadowWidthScale — stale binary?")
            time.sleep(2.0)
            n, pk, solid = measure(f"d{dist}_m{mt}")
            print(f"{mt:>10.1f} {mt*0.104:>9.3f} {n:>7} {pk:>6} {solid:>7.1f}  "
                  f"{'casts' if n > 60 else 'NO SHADOW'}", flush=True)

    # Control: with grass gone, toggling casting must change nothing at any width.
    must(post("/api/debug/grass", {"enabled": False, "shadowWidthScale": 2.0}), "grass off")
    time.sleep(2.5)
    n, pk, _ = measure("control")
    print(f"\nCONTROL (grass removed): area={n} peak={pk}  "
          f"{'PASS' if n < 20 else 'FAIL — something else is moving'}", flush=True)
    must(post("/api/debug/grass", {"enabled": True}), "grass on")
    must(post("/api/debug/shadow", {"mode": 0, "distance": 420}), "restore view")
    return 0


if __name__ == "__main__":
    sys.exit(main())
