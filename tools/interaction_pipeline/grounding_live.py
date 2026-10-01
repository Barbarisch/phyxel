"""A5 L4 — grounding on a real subcube ramp in the live engine (docs/AnimationSystemV3Plan.md §4 A5).

Lays a 1/3 u-riser ramp (RISERS cells along +x, three cells wide) on the flat CharacterTestbed floor,
spawns a standard NPC on the plateau, patrols it down the ramp and back, and judges the walk with
the engine's own oracle (`animation_record` + `animation_validate`: point probe, ankle reference,
sole model) — once with the terrain foot solve OFF (control / red baseline) and once ON. Prints
the grounding readback of a few frames while it walks. Nothing is asserted here; it is the live
number next to the headless one.

    python tools/interaction_pipeline/grounding_live.py            # both runs
    python tools/interaction_pipeline/grounding_live.py --keep     # leave the ramp + NPC
"""
from __future__ import annotations

import argparse
import json
import time
import urllib.request

BASE = "http://127.0.0.1:8090"
X0, Y_FLOOR, Z0 = 60, 17, 60         # first riser cell; the flat floor's top is y = 17 (surface 16)
RISERS, WIDTH = 6, 3                 # 6 risers of 1/3 u = 2 u of climb over 6 cells


def post(path, body, timeout=30):
    req = urllib.request.Request(BASE + path, json.dumps(body).encode(), {"Content-Type": "application/json"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=timeout))
    except Exception as e:  # noqa: BLE001
        return {"error": str(e)}


def get(path, timeout=30):
    try:
        return json.load(urllib.request.urlopen(BASE + path, timeout=timeout))
    except Exception as e:  # noqa: BLE001
        return {"error": str(e)}


def build_ramp():
    """Riser k (1..RISERS) at cell x = X0 + k - 1 is k/3 u tall: full cubes + a subcube layer."""
    cubes, subs = [], []
    for k in range(1, RISERS + 1):
        x = X0 + k - 1
        full, layers = k // 3, k % 3
        for dz in range(WIDTH):
            z = Z0 + dz
            for c in range(full):
                cubes.append({"x": x, "y": Y_FLOOR + c, "z": z, "material": "Stone"})
            for sy in range(layers):
                for sx in range(3):
                    for sz in range(3):
                        subs.append({"x": x, "y": Y_FLOOR + full, "z": z, "sx": sx, "sy": sy, "sz": sz, "material": "Stone"})
    # plateau: 4 cells beyond the ramp at the top height
    top = RISERS // 3
    for x in range(X0 + RISERS, X0 + RISERS + 4):
        for dz in range(WIDTH):
            for c in range(top):
                cubes.append({"x": x, "y": Y_FLOOR + c, "z": Z0 + dz, "material": "Stone"})
    placed = 0
    for c in cubes:
        r = post("/api/world/voxel", c)
        placed += 1 if r.get("success") else 0
    r = post("/api/world/subcubes/batch", {"subcubes": subs})
    print(f"ramp: {placed}/{len(cubes)} cubes, subcube batch -> {str(r)[:120]}")


def run(foot_ik: bool, name: str):
    zc = Z0 + 1.5
    top_y = Y_FLOOR + RISERS / 3.0
    r = post("/api/npc/spawn", {"name": name, "position": {"x": X0 + RISERS + 2.0, "y": top_y + 0.05, "z": zc},
                                "appearance": {"preset": "standard"}})
    if not r.get("success"):
        print("spawn failed:", r); return
    eid = f"npc_{name}"
    time.sleep(1.0)
    k = post("/api/debug/foot_ik", {"id": eid, "enabled": foot_ik})
    print(f"[{('ON ' if foot_ik else 'OFF')}] foot_ik -> enabled={k.get('enabled')} knobs={k.get('knobs')}")
    # patrol: plateau -> bottom of the ramp -> back, at walk speed
    post("/api/npc/behavior", {"name": name, "behavior": "patrol", "walkSpeed": 1.5, "waitTime": 0.5,
                               "waypoints": [{"x": X0 - 3.0, "y": Y_FLOOR, "z": zc},
                                             {"x": X0 + RISERS + 2.0, "y": top_y, "z": zc}]})
    time.sleep(1.5)                                     # let it turn and start down the ramp
    rec = post("/api/animation/record", {"id": eid, "seconds": 4.0})
    print(f"[{('ON ' if foot_ik else 'OFF')}] record -> {str(rec)[:100]}")
    samples = []
    t0 = time.time()
    while time.time() - t0 < 4.2:
        g = get(f"/api/animation/grounding?id={eid}")
        if "l_corr_u" in g:
            samples.append((round(g["l_corr_u"], 3), round(g["r_corr_u"], 3), round(g["pelvis_shift_u"], 3), g["probe_ok"]))
        time.sleep(0.25)
    v = post("/api/animation/validate", {"id": eid})
    m = v.get("metrics", {}) if isinstance(v, dict) else {}
    keys = ("max_stance_float", "max_penetration", "world_skate_ratio", "stance_samples", "mean_capsule_speed")
    print(f"[{('ON ' if foot_ik else 'OFF')}] validate ({v.get('frames_sampled')} frames, {v.get('state')}/{v.get('clip')}) -> "
          + json.dumps({k: round(m[k], 3) if isinstance(m.get(k), float) else m.get(k) for k in keys if k in m}))
    nz = [s for s in samples if abs(s[0]) > 0.02 or abs(s[1]) > 0.02 or abs(s[2]) > 0.02]
    print(f"[{('ON ' if foot_ik else 'OFF')}] grounding samples: {len(samples)}, with a correction: {len(nz)}; e.g. {nz[:4]}")
    return eid


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--skip-ramp", action="store_true")
    args = ap.parse_args()
    if not args.skip_ramp:
        build_ramp()
        time.sleep(1.0)
    a = run(False, "GL_off")
    if a and not args.keep: post("/api/npc/remove", {"name": "GL_off"})
    b = run(True, "GL_on")
    if b and not args.keep: post("/api/npc/remove", {"name": "GL_on"})


if __name__ == "__main__":
    main()
