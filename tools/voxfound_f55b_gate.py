#!/usr/bin/env python3
"""docs/VoxelFoundationPlan.md F5.5b live gates (prediction: docs/evidence/voxfound_f55_prediction.txt, F5.5b L1-L4).

Drives POST /api/debug/voxfound_bodies on a running Release editor (project VoxFoundBodies; rigs are FractureLab's
hand-built lab shapes -- not generator output) with the GPU solver live ({"solver":"gpu"}).
  l1   -- runs live: the stone-wall blast and the large wall (E/T 2800) fly, pile and sleep; per sample: awake, bodies
          below the floor, max penetration, solver cost (L2); then the same with the F5.4 rest policy on (the ledger
          at every sample); then determinism: the same rig twice with {"ticks":600}, centres compared.
  cull -- the per-body cull (L3/L4): the large wall settled, 3 camera poses, cull off / off (control) / on;
          pixels differing by > 1 level, drawn faces, draws, the body pass's device time (gpu_timing).
Pixel gates hold the effect clocks (/api/debug/effect_time) and compare the VIEWPORT box only (the editor's status
bar prints the FPS). Writes JSON lines to docs/evidence/voxfound_f55b_live.jsonl and PNGs to docs/evidence/voxfound_f55b_*.png.
"""
import argparse
import json
import os
import sys
import time
import urllib.request

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import voxfound_f53_gate as g53  # noqa: E402
from voxfound_f53_gate import post, shot, save  # noqa: E402

EVID = os.path.join(os.path.dirname(__file__), "..", "docs", "evidence")
AT = [5.0, 20.0, 5.0]   # world metres of the rig's voxel (0,0,0); the floor's top is at y = 20
BODIES = "/api/debug/voxfound_bodies"


TAG = ""


def log(rec):
    rec = dict(rec, world=TAG)
    with open(os.path.join(EVID, "voxfound_f55b_live.jsonl"), "a") as f:
        f.write(json.dumps(rec) + "\n")
    print(json.dumps(rec), flush=True)


def get(path):
    with urllib.request.urlopen(g53.API + path, timeout=120) as r:
        return json.loads(r.read())


def rig(name, energy, impulse, solver="gpu", run=False):
    return post(BODIES, {"rig": name, "energy": energy, "impulse": impulse, "at": AT, "solver": solver, "run": run})


def sample(st):
    ys = [c[1] for c in st["centres_m"]]
    return {"tick": st["tick"], "bodies": st["bodies"], "awake": st["awake"],
            "below_floor": sum(1 for y in ys if y < AT[1] - 1.0), "max_pen_mm": st["max_penetration_mm"],
            "solver": st["solver"], "render": st["render"]}


def live(name, energy, impulse, seconds, rest=None, cam=None):
    r = rig(name, energy, impulse)
    if cam:
        post("/api/camera", cam)
    if rest:
        post(BODIES, {"rest": dict(rest, on=True)})
    st0 = post(BODIES, {"status": True})
    ledger0 = st0["ledger"]
    post(BODIES, {"run": True})
    t0 = time.time()
    samples, prev = [], None
    ledger_broken = 0
    while time.time() - t0 < seconds:
        time.sleep(0.5)
        st = post(BODIES, {"status": True, "pen": False})   # the CPU penetration measure stalls the frame
        s = sample(st)
        now = time.time()
        if prev:
            s["ticks_per_s_interval"] = (s["tick"] - prev[0]) / (now - prev[1])
        prev = (s["tick"], now)
        s["ledger_ok"] = st["ledger"] == ledger0
        ledger_broken += not s["ledger_ok"]
        if rest:
            s["rest"] = st["rest"]
        samples.append(s)
    post(BODIES, {"run": False})
    st = post(BODIES, {"status": True})   # with the penetration measure, once at the end
    return r, samples, ledger_broken, st


def l1(a):
    blast_cam = {"position": {"x": 6.5, "y": 21.6, "z": 1.0}, "yaw": 90, "pitch": -14}
    wall_cam = {"position": {"x": 21.0, "y": 28.0, "z": -25.0}, "yaw": 90, "pitch": -20}
    for name, e, imp, cam in (("stone_wall", 80.0, 8000.0, blast_cam), ("large_wall", 2800.0, 200.0, wall_cam)):
        r, samples, broken, st = live(name, e, imp, a.seconds, cam=cam)
        log({"gate": "L1/L2 run", "rig": r, "samples": samples, "final": sample(st)})
        r, samples, broken, st = live(name, e, imp, a.seconds,
                                      rest={"budget": 64 if name == "large_wall" else 8, "keep_m": 0.0,
                                            "rest_ticks": 60}, cam=cam)
        log({"gate": "L1 rest", "rig": r, "ledger_broken_samples": broken, "samples": samples, "final": sample(st),
             "rest": st["rest"]})
        cs = []
        for k in range(2):
            rig(name, e, imp)
            st = post(BODIES, {"ticks": 600, "status": True})   # F5.5d: pen only on a status
            cs.append(st["centres_m"])
        differ = sum(1 for p, q in zip(cs[0], cs[1]) if p != q) + abs(len(cs[0]) - len(cs[1]))
        log({"gate": "L1 determinism", "rig_name": name, "bodies": len(cs[0]), "centres_differ": differ,
             "awake_after_600": st["awake"], "max_pen_mm": st["max_penetration_mm"],
             "below_floor": sum(1 for c in cs[1] if c[1] < AT[1] - 1.0)})


def body_ms(frames=120):
    t = get(f"/api/debug/gpu_timing?frames={frames}")
    return t


def px1(a, b, box):
    x0, y0, x1, y1 = box
    d = np.abs(a[y0:y1, x0:x1] - b[y0:y1, x0:x1]).max(axis=2)
    return {"px_gt1": int((d > 1).sum()), "px_any": int((d > 0).sum()), "px_box": int(d.size), "max": int(d.max())}


def cull(a):
    post("/api/debug/tonemap", {"curve": 0})
    # run 2: wind / grass / sky move every frame -- the pixel gate needs the effect clocks held (F5.3 G1 did this)
    post("/api/debug/effect_time", {"frozen": True})
    r = rig("large_wall", 2800.0, 200.0)
    st = post(BODIES, {"ticks": a.settle})
    log({"gate": "cull setup", "rig": r, "tick": st["tick"], "awake": st["awake"], "bodies": st["bodies"]})
    poses = {
        "whole": {"position": {"x": 21.0, "y": 28.0, "z": -25.0}, "yaw": 90, "pitch": -20},
        "half": {"position": {"x": 37.0, "y": 24.0, "z": -10.0}, "yaw": 90, "pitch": -15},
        "close": {"position": {"x": 15.0, "y": 22.5, "z": 0.0}, "yaw": 90, "pitch": -20},
    }
    for name, cam in poses.items():
        post("/api/camera", cam)
        time.sleep(a.cam_settle)   # the scene converges for seconds after a camera move (run 1: 1 s was not enough)
        rec = {"gate": "L3/L4", "pose": name, "cam": cam, "cam_settle_s": a.cam_settle}
        post(BODIES, {"cull": False})
        off1 = shot(1.0)
        off2 = shot(0.6)
        post(BODIES, {"cull": True})
        on = shot(1.0)
        rec["on_status"] = post(BODIES, {"status": True, "pen": False})["render"]
        post(BODIES, {"cull": False})
        off3 = shot(1.0)
        rec["off_status"] = post(BODIES, {"status": True, "pen": False})["render"]
        # run 3: the whole window includes the editor's status-bar FPS text, which changes between any two shots --
        # the gate compares the viewport only (--box)
        full = a.box
        rec["box"] = full
        rec["control_off_vs_off"] = px1(off1, off2, full)
        rec["on_vs_off_before"] = px1(on, off2, full)
        rec["on_vs_off_after"] = px1(on, off3, full)
        # device time: 3 alternating pairs (off, on), 120 frames each; the scopes' medians
        keys = ["Scene Pass/VoxFound Bodies", "Shadow Pass/Shadow Mid/VoxFound Bodies Shadow",
                "Shadow Pass/Shadow Near/VoxFound Bodies Shadow"]
        ab = {"off": [], "on": []}
        for k in range(3):
            for mode in ("off", "on"):
                post(BODIES, {"cull": mode == "on"})
                time.sleep(1.0)
                t = body_ms(120)
                med = {s["key"]: s["median_ms"] for s in t["scopes"]}
                ab[mode].append([med.get(x, 0.0) for x in keys] + [t["gpu_frame_ms"]["median_ms"]])
        rec["ab_keys"] = keys + ["frame"]
        rec["ab"] = ab
        post(BODIES, {"cull": True})
        save(on, f"voxfound_f55b_cull_{name}_on.png")
        save(off2, f"voxfound_f55b_cull_{name}_off.png")
        log(rec)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["l1", "cull"])
    ap.add_argument("--api", default="http://localhost:8090")
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--settle", type=int, default=1200)
    ap.add_argument("--box", type=int, nargs=4, default=[241, 44, 1198, 672], help="viewport x0 y0 x1 y1 (px)")
    ap.add_argument("--cam-settle", type=float, default=5.0)
    ap.add_argument("--world", default="", help="which project's world the lab runs in (recorded)")
    a = ap.parse_args()
    g53.API = a.api
    global TAG
    TAG = a.world
    return {"l1": l1, "cull": cull}[a.what](a) or 0


if __name__ == "__main__":
    sys.exit(main())
