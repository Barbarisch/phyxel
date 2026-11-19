#!/usr/bin/env python3
"""docs/VoxelFoundationPlan.md F5.4 live gates (prediction: docs/evidence/voxfound_f54_prediction.txt, L1-L5).

Drives POST /api/debug/voxfound_bodies on a running editor (project VoxFoundBodies): the stone wall blast settles
(900 ticks), then one rest pass bakes every asleep body (budget 0, keep 0). Shots before (two: the control) and after
the bake at a near camera (the F5.3 side view) and, in a second identical run, a camera 4x farther along the same ray.
  pop -- L1 + L2 + L3 + L4 (+ the before / after pictures for the owner, L5).
Writes JSON lines to docs/evidence/voxfound_f54_live.jsonl and PNGs to docs/evidence/voxfound_f54_*.png.
"""
import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import voxfound_f53_gate as g53  # noqa: E402
from voxfound_f53_gate import post, shot, save  # noqa: E402

EVID = os.path.join(os.path.dirname(__file__), "..", "docs", "evidence")
ROUTE = "/api/debug/voxfound_bodies"


def diff(a, b, box, floor):
    import numpy as np
    x0, y0, x1, y1 = box
    d = np.abs(a[y0:y1, x0:x1] - b[y0:y1, x0:x1]).max(axis=2)
    return {"px_any": int((d > 0).sum()), "px_above_floor": int((d > floor).sum()), "px_box": int(d.size),
            "pct_above_floor": 100.0 * float((d > floor).mean()), "max": int(d.max())}


def log(rec):
    with open(os.path.join(EVID, "voxfound_f54_live.jsonl"), "a") as f:
        f.write(json.dumps(rec) + "\n")
    print(json.dumps(rec))


def run(cam, tag, box, ticks, impulse):
    at = [5.0, 20.0, 5.0]
    r = post(ROUTE, {"rig": "stone_wall", "energy": 80.0, "impulse": impulse, "at": at})
    post("/api/camera", cam)
    st0 = post(ROUTE, {"ticks": ticks})
    b1 = shot(1.5)
    b2 = shot(0.6)
    st1 = post(ROUTE, {"bake_now": {"budget": 0, "keep_m": 0.0, "rest_ticks": 0}})
    after = shot(1.5)
    full = [0, 0, b1.shape[1], b1.shape[0]]
    save(b1, f"voxfound_f54_{tag}_bodies.png")
    save(after, f"voxfound_f54_{tag}_baked.png")
    rec = {"gate": "pop", "tag": tag, "cam": cam, "rig": r, "tick": st0["tick"], "awake_before": st0["awake"],
           "bodies_before": st0["bodies"], "bodies_after": st1["bodies"],
           "ledger_before": st0["ledger"], "ledger_after": st1["ledger"],
           "ledger_equal": st0["ledger"] == st1["ledger"], "rest": st1["rest"],
           "box": box, "control_box": diff(b1, b2, box, 1), "pop_box": diff(b1, after, box, 1),
           "control_full": diff(b1, b2, full, 1), "pop_full": diff(b1, after, full, 1),
           "preview_faces_after": st1["preview_faces"], "body_faces_after": st1["body_faces"]}
    log(rec)
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["pop"])
    ap.add_argument("--box", type=int, nargs=4, default=[470, 90, 970, 460])
    ap.add_argument("--ticks", type=int, default=900)
    ap.add_argument("--impulse", type=float, default=8000.0)
    ap.add_argument("--d0", type=float, default=6.0, help="metres from the near camera to the pile along its ray")
    ap.add_argument("--api", default="http://localhost:8090")
    a = ap.parse_args()
    g53.API = a.api
    post("/api/debug/tonemap", {"curve": 0})
    near = {"position": {"x": 6.5, "y": 21.6, "z": 1.0}, "yaw": 90, "pitch": -14}
    # the engine's front vector: (cos yaw cos pitch, sin pitch, sin yaw cos pitch)
    yaw, pitch = math.radians(90), math.radians(-14)
    f = (math.cos(yaw) * math.cos(pitch), math.sin(pitch), math.sin(yaw) * math.cos(pitch))
    k = 3.0 * a.d0   # the pile at d0 from the near camera -> 4 d0 from the far one
    p = near["position"]
    far = {"position": {"x": p["x"] - k * f[0], "y": p["y"] - k * f[1], "z": p["z"] - k * f[2]}, "yaw": 90,
           "pitch": -14}
    rn = run(near, "near", a.box, a.ticks, a.impulse)
    rf = run(far, "far", a.box, a.ticks, a.impulse)
    log({"gate": "L2", "near_pop_full_above_floor": rn["pop_full"]["px_above_floor"],
         "far_pop_full_above_floor": rf["pop_full"]["px_above_floor"],
         "ratio": rf["pop_full"]["px_above_floor"] / max(1, rn["pop_full"]["px_above_floor"])})
    return 0


if __name__ == "__main__":
    sys.exit(main())
