"""F7.3b live (docs/evidence/voxfound_f7_prediction.txt O2-O5): the light occupancy from the new store when switched.
VoxFoundGate project (port 8120). Modes:
  hole  : rebuild the region, switch on, F7.4's pose, break the cube under the crosshair; capture debug view 7 (ambient)
          at tonemap curve 0: the mean grey inside the hole's screen box vs open ground beside it (O3); then place the
          cube back and capture again; then the occupancy stats (O2).
Every artifact: WorldGenerator Perlin s7 + flora (the 1/8 m region), edited through VoxelInteractionSystem's own calls."""
import json, os, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
POSE = {"mode": "free", "position": {"x": 8132.5, "y": 33.0, "z": 60.5}, "yaw": 90, "pitch": -40}
HOLE = (685, 360, 750, 405)     # screen box inside the hole (F7.4's captures: x 678-758, y 353-415)
GROUND = (840, 360, 905, 405)   # open ground beside it, same rows


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=3600) as resp:
        return json.loads(resp.read().decode())


def grab(name):
    d = os.path.join(EVID, name)
    shutil.copy(os.path.join(REPO, call("GET", "/api/screenshot")["path"]), d)
    return np.asarray(Image.open(d).convert("L")).astype(np.float64)


def mean(img, box):
    x0, y0, x1, y1 = box
    return round(float(img[y0:y1, x0:x1].mean()), 2)


def main():
    tag = sys.argv[2] if len(sys.argv) > 2 else "run"
    out = {"tag": tag}
    out["region"] = {k: v for k, v in call("POST", P, REGION).items() if k in ("faces", "chunks")}
    out["switch"] = call("POST", P, {"world_switch": True})
    call("POST", "/api/camera", POSE)
    call("POST", "/api/debug/foliage", {"windStrength": 0.0})
    time.sleep(3.0)
    out["break"] = {k: v for k, v in call("POST", P, {"edit_action": {"action": "break", "mode": "cube"}}).items()
                    if k in ("voxels_changed", "hover_voxel")}
    time.sleep(3.0)   # residency + a few probe refreshes (1/8 of the field per frame)
    call("POST", "/api/debug/tonemap", {"curve": 0})
    call("POST", "/api/debug/shadow", {"mode": 7})
    time.sleep(3.0)
    a = grab(f"voxfound_f73b_{tag}_hole_ambient.png")
    out["hole_ambient"] = {"hole": mean(a, HOLE), "ground": mean(a, GROUND)}
    call("POST", "/api/debug/shadow", {"mode": 0})
    call("POST", "/api/debug/tonemap", {"curve": 1})
    time.sleep(1.0)
    grab(f"voxfound_f73b_{tag}_hole_shaded.png")
    out["place"] = {k: v for k, v in call("POST", P, {"edit_action": {"action": "place", "mode": "cube"}}).items()
                    if k in ("voxels_changed",)}
    time.sleep(3.0)
    call("POST", "/api/debug/tonemap", {"curve": 0})
    call("POST", "/api/debug/shadow", {"mode": 7})
    time.sleep(3.0)
    b = grab(f"voxfound_f73b_{tag}_placed_ambient.png")
    out["placed_ambient"] = {"hole_box": mean(b, HOLE), "ground": mean(b, GROUND)}
    call("POST", "/api/debug/shadow", {"mode": 0})
    call("POST", "/api/debug/tonemap", {"curve": 1})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    try:
        out["occupancy"] = call("POST", P, {"occupancy_stats": True}).get("occupancy")
    except Exception as e:   # the control build has no such probe
        out["occupancy"] = str(e)
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f73b_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
