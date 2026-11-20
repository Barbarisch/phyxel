#!/usr/bin/env python3
"""docs/VoxelFoundationPlan.md F5.5c live gates (prediction: docs/evidence/voxfound_f55_prediction.txt, F5.5c L1-L3).

Drives POST /api/debug/voxfound_bodies on a running Release editor; rigs are FractureLab's hand-built lab shapes (not
generator output), motion by BodySimGpu.
  pixels -- L1: region preview vs F5.5b's area preview on the SAME settled bodies ({"preview":...} switches the path):
            the large wall at "at" y = 20.25 (the area origin on an integer metre: the area path's texture phase follows
            its origin), effect clocks held, the viewport box only, F5.5b's three poses; control = area vs area.
  hitch  -- L2/L3: the large wall at E/T 2800 with the rest policy on (budget 64, keep 0, rest 60 ticks, cap 4) for
            --seconds; samples every 0.5 s (ledger, bodies below the floor, the bake levers); at the end the whole rest
            call's max (the hitch), the frame intervals, and {"verify":true} (every region chunk rebuilt on the GPU and
            compared with the incremental faces). --control runs F5.5b's path instead ({"preview":"area","gpu_edit":false},
            no cap) for the before number on the same machine.
Writes JSON lines to docs/evidence/voxfound_f55c_live.jsonl and PNGs to docs/evidence/voxfound_f55c_*.png.
"""
import argparse
import json
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import voxfound_f53_gate as g53  # noqa: E402
from voxfound_f53_gate import post, shot, save  # noqa: E402

EVID = os.path.join(os.path.dirname(__file__), "..", "docs", "evidence")
BODIES = "/api/debug/voxfound_bodies"
TAG = ""


def log(rec):
    rec = dict(rec, world=TAG)
    with open(os.path.join(EVID, "voxfound_f55c_live.jsonl"), "a") as f:
        f.write(json.dumps(rec) + "\n")
    print(json.dumps(rec), flush=True)


def px1(a, b, box):
    x0, y0, x1, y1 = box
    d = np.abs(a[y0:y1, x0:x1].astype(int) - b[y0:y1, x0:x1].astype(int)).max(axis=2)
    return {"px_gt1": int((d > 1).sum()), "px_any": int((d > 0).sum()), "px_box": int(d.size), "max": int(d.max())}


def pixels(a):
    post("/api/debug/tonemap", {"curve": 0})
    post("/api/debug/effect_time", {"frozen": True})
    at = [5.0, 20.25, 5.0]
    r = post(BODIES, {"rig": "large_wall", "energy": 2800.0, "impulse": 200.0, "at": at, "solver": "gpu",
                      "preview": "area"})
    st = post(BODIES, {"ticks": a.settle})
    log({"gate": "L1 setup", "rig": r, "tick": st["tick"], "awake": st["awake"], "bodies": st["bodies"]})
    poses = {
        "whole": {"position": {"x": 21.0, "y": 28.25, "z": -25.0}, "yaw": 90, "pitch": -20},
        "half": {"position": {"x": 37.0, "y": 24.25, "z": -10.0}, "yaw": 90, "pitch": -15},
        "close": {"position": {"x": 15.0, "y": 22.75, "z": 0.0}, "yaw": 90, "pitch": -20},
    }
    for name, cam in poses.items():
        post("/api/camera", cam)
        post(BODIES, {"preview": "area"})
        time.sleep(a.cam_settle)
        a1 = shot(1.0)
        a2 = shot(0.6)
        rr = post(BODIES, {"preview": "region", "status": True, "pen": False})
        reg = shot(1.5)
        post(BODIES, {"preview": "area"})
        a3 = shot(1.5)
        rec = {"gate": "L1", "pose": name, "cam": cam, "box": a.box,
               "control_area_vs_area": px1(a1, a2, a.box),
               "region_vs_area_before": px1(reg, a2, a.box), "region_vs_area_after": px1(reg, a3, a.box),
               "region_status": {"preview_faces": rr["preview_faces"], "bake_levers": rr["bake_levers"]}}
        save(reg, f"voxfound_f55c_pixels_{name}_region.png")
        save(a2, f"voxfound_f55c_pixels_{name}_area.png")
        log(rec)
    post(BODIES, {"preview": "region"})


def hitch(a):
    post("/api/debug/effect_time", {"frozen": False})
    at = [5.0, 20.0, 5.0]
    ctl = a.control
    r = post(BODIES, {"rig": "large_wall", "energy": 2800.0, "impulse": 200.0, "at": at, "solver": "gpu",
                      "preview": "area" if ctl else "region", "gpu_edit": not ctl})
    post("/api/camera", {"position": {"x": 21.0, "y": 28.0, "z": -25.0}, "yaw": 90, "pitch": -20})
    rest = {"on": True, "budget": 64, "keep_m": 0.0, "rest_ticks": 60, "cap": 0 if ctl else a.cap}
    post(BODIES, {"rest": rest})
    st0 = post(BODIES, {"status": True, "pen": False})
    ledger0 = st0["ledger"]
    floor_y = at[1] - 1.0
    post(BODIES, {"run": True})
    t0 = time.time()
    samples = []
    broken = 0
    while time.time() - t0 < a.seconds:
        time.sleep(0.5)
        st = post(BODIES, {"status": True, "pen": False})
        ok = st["ledger"] == ledger0
        broken += not ok
        samples.append({"t": round(time.time() - t0, 2), "tick": st["tick"], "bodies": st["bodies"],
                        "awake": st["awake"], "below_floor": sum(1 for c in st["centres_m"] if c[1] < floor_y),
                        "ledger_ok": ok, "baked": st["rest"]["baked"], "levers": st["bake_levers"],
                        "frames": st["frames"]})
    post(BODIES, {"run": False})
    st = post(BODIES, {"status": True, "pen": False, "verify": not ctl})
    log({"gate": "L2/L3 hitch" + (" CONTROL (F5.5b path)" if ctl else ""), "rig": r, "rest": rest,
         "seconds": a.seconds, "ledger_broken_samples": broken, "samples": samples,
         "final": {"bodies": st["bodies"], "awake": st["awake"], "rest": st["rest"], "levers": st["bake_levers"],
                   "frames": st["frames"], "solver": st["solver"], "verify": st.get("verify"),
                   "ledger_ok": st["ledger"] == ledger0}})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["pixels", "hitch"])
    # 127.0.0.1: "localhost" costs ~2 s per call on this machine (the IPv6 attempt first) -- found in the first runs
    ap.add_argument("--api", default="http://127.0.0.1:8090")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--settle", type=int, default=1200)
    ap.add_argument("--cap", type=int, default=4)
    ap.add_argument("--control", action="store_true")
    ap.add_argument("--box", type=int, nargs=4, default=[241, 44, 1198, 672], help="viewport x0 y0 x1 y1 (px)")
    ap.add_argument("--cam-settle", type=float, default=5.0)
    ap.add_argument("--world", default="", help="which project's world the lab runs in (recorded)")
    a = ap.parse_args()
    g53.API = a.api
    global TAG
    TAG = a.world
    return {"pixels": pixels, "hitch": hitch}[a.what](a) or 0


if __name__ == "__main__":
    sys.exit(main())
