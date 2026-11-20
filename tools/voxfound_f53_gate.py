#!/usr/bin/env python3
"""docs/VoxelFoundationPlan.md F5.3 live gates (prediction: docs/evidence/voxfound_f53_prediction.txt).

Drives POST /api/debug/voxfound_bodies on a running editor (project VoxFoundBodies) and compares screenshots.
  g1  -- the draw is invisible at rest: bodies (new path) vs the same voxels through the preview ("as_world"),
         a control (two shots of the same mode) and a mutation ("mutate", 0.5 degrees).
  g3  -- shadows: a piece paused in flight, cast on vs off; and with no bodies shown, on vs off.
  seq -- the screenshot sequence for the owner (t = 0, 0.25, 0.5, 1, 3 s).
Writes JSON lines to docs/evidence/voxfound_f53_live.jsonl and PNGs to docs/evidence/voxfound_f53_*.png.
"""
import argparse
import io
import json
import os
import sys
import time
import urllib.request

import numpy as np
from PIL import Image

API = "http://localhost:8090"
EVID = os.path.join(os.path.dirname(__file__), "..", "docs", "evidence")


def post(path, body):
    req = urllib.request.Request(API + path, data=json.dumps(body).encode(), method="POST")
    with urllib.request.urlopen(req, timeout=180) as r:
        return json.loads(r.read())


def shot(settle=0.6):
    time.sleep(settle)
    with urllib.request.urlopen(API + "/api/screenshot", timeout=60) as r:
        data = r.read()
    if data[:4] != b"\x89PNG":   # some builds answer JSON with a path
        j = json.loads(data)
        return np.asarray(Image.open(j["path"]).convert("RGB")).astype(np.int16)
    return np.asarray(Image.open(io.BytesIO(data)).convert("RGB")).astype(np.int16)


def diff(a, b, box):
    x0, y0, x1, y1 = box
    d = np.abs(a[y0:y1, x0:x1] - b[y0:y1, x0:x1]).max(axis=2)
    return {"px_differ": int((d > 0).sum()), "px_box": int(d.size), "pct": 100.0 * float((d > 0).mean()),
            "max": int(d.max())}


def log(rec):
    with open(os.path.join(EVID, "voxfound_f53_live.jsonl"), "a") as f:
        f.write(json.dumps(rec) + "\n")
    print(json.dumps(rec))


def save(img, name):
    Image.fromarray(img.astype(np.uint8)).save(os.path.join(EVID, name))


def setup(rig, energy, impulse, at, cam):
    r = post("/api/debug/voxfound_bodies", {"rig": rig, "energy": energy, "impulse": impulse, "at": at})
    post("/api/camera", cam)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["g1", "g3", "seq"])
    ap.add_argument("--box", type=int, nargs=4, default=[470, 90, 970, 460], help="x0 y0 x1 y1 (screen px)")
    ap.add_argument("--rig", default="stone_wall")
    ap.add_argument("--energy", type=float, default=80.0)
    ap.add_argument("--impulse", type=float, default=20.0)
    ap.add_argument("--cam", default='{"position": {"x": 6.5, "y": 21.6, "z": 1.0}, "yaw": 90, "pitch": -14}')
    ap.add_argument("--tag", default="")
    a = ap.parse_args()
    at = [5.0, 20.0, 5.0]
    cam = json.loads(a.cam)
    tone = post("/api/debug/tonemap", {"curve": 0})
    if a.what == "g1":
        r = setup(a.rig, a.energy, a.impulse, at, cam)
        bodies1 = shot(1.0)
        bodies2 = shot()
        post("/api/debug/voxfound_bodies", {"as_world": True})
        world = shot(1.0)
        post("/api/debug/voxfound_bodies", {"as_world": False, "mutate": True})
        mut = shot(1.0)
        st = post("/api/debug/voxfound_bodies", {"mutate": False})
        save(bodies1, "voxfound_f53_g1_bodies.png")
        save(world, "voxfound_f53_g1_as_world.png")
        save(mut, "voxfound_f53_g1_mutate.png")
        log({"gate": "G1", "rig": r, "tonemap": tone, "box": a.box, "control": diff(bodies1, bodies2, a.box),
             "bodies_vs_world": diff(bodies1, world, a.box), "mutation": diff(bodies1, mut, a.box),
             "status": {k: st[k] for k in ("bodies", "body_faces", "preview_faces", "render")}})
    elif a.what == "g3":
        r = setup(a.rig, a.energy, 8000.0, at, cam)
        post("/api/debug/voxfound_bodies", {"ticks": 12})   # pieces in flight (0.2 s)
        on1 = shot(1.0)
        on2 = shot()
        post("/api/debug/voxfound_bodies", {"cast": False})
        off = shot(1.0)
        full = [0, 0, on1.shape[1], on1.shape[0]]
        save(on1, "voxfound_f53_g3_cast_on.png")
        save(off, "voxfound_f53_g3_cast_off.png")
        darker = int(((on1 - off).sum(axis=2) < 0).sum())
        lighter = int(((on1 - off).sum(axis=2) > 0).sum())
        post("/api/debug/voxfound_bodies", {"cast": True, "shown": False})
        h_on = shot(1.0)
        post("/api/debug/voxfound_bodies", {"cast": False})
        h_off = shot(1.0)
        st = post("/api/debug/voxfound_bodies", {"cast": True, "shown": True})
        log({"gate": "G3", "rig": r, "tick": st["tick"], "control": diff(on1, on2, full),
             "cast_on_vs_off": diff(on1, off, full), "darker_px": darker, "lighter_px": lighter,
             "hidden_cast_on_vs_off": diff(h_on, h_off, full)})
    else:
        r = setup(a.rig, a.energy, a.impulse, at, cam)
        last = 0
        for t, name in ((0, "t0"), (15, "t025"), (30, "t05"), (60, "t1"), (180, "t3")):
            st = post("/api/debug/voxfound_bodies", {"ticks": t - last, "status": True})   # F5.5d: pen on a status
            last = t
            save(shot(0.8), f"voxfound_f53_seq_{a.rig}{a.tag}_{name}.png")
            log({"gate": "seq", "rig_name": a.rig, "impulse": a.impulse, "tick": st["tick"], "awake": st["awake"],
                 "max_penetration_mm": st["max_penetration_mm"], "render": st["render"]})
    return 0


if __name__ == "__main__":
    sys.exit(main())
