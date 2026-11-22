"""F7.3b O4 + O5 (docs/evidence/voxfound_f7_prediction.txt): the switch off restores today's light occupancy pool (O4);
the pixels at F2.19's 5 poses (curve 0, wind 0), GI on and GI off: S = the switch (the new store's occupancy) vs
P = F2.19's B (the region drawn, today's occupancy) (O5). VoxFoundGate, port 8120, region already built.
Usage: python tools/voxfound_f73b_pix.py docs/evidence/voxfound_f29_poses.json OUTDIR"""
import json, os, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
CLEAN = (425, 105, 1195, 620)
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=600) as resp:
        return json.loads(resp.read().decode())


def shot(out, name):
    d = os.path.join(out, name)
    shutil.copy(os.path.join(REPO, call("GET", "/api/screenshot")["path"]), d)
    x0, y0, x1, y1 = CLEAN
    return np.asarray(Image.open(d).convert("RGB")).astype(np.int16)[y0:y1, x0:x1]


def ndiff(a, b, t=8):
    return int((np.max(np.abs(a - b), axis=2) > t).sum())


poses = json.load(open(sys.argv[1]))
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
res = {}
call("POST", P, {"world_switch": True})
time.sleep(3.0)
res["O4_switch_on"] = call("POST", P, {"occupancy_stats": True})["occupancy"]
call("POST", P, {"world_switch": False})
time.sleep(3.0)
res["O4_switch_off"] = call("POST", P, {"occupancy_stats": True})["occupancy"]
print("O4", json.dumps(res), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/tonemap", {"curve": 0})
try:
    for gi in (True, False):
        call("POST", "/api/debug/gi", {"enabled": gi})
        for name, (x, y, z, yaw, pitch) in poses.items():
            call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
            im = {}
            for lab, cfgs in (("P", [{"world_switch": False}, {"hide_world": True, "show_preview": True}]),
                              ("S", [{"world_switch": True}]), ("S2", [{"world_switch": True}])):
                for c in cfgs:
                    call("POST", P, c)
                time.sleep(6.0 if gi else 3.0)   # GI: the probe field refreshes 1/8 per frame after an occupancy change
                im[lab] = shot(out, f"f73b_{name}_gi{int(gi)}_{lab}.png")
            n = im["P"].shape[0] * im["P"].shape[1]
            r = {"S_vs_P_pct": round(100.0 * ndiff(im["S"], im["P"]) / n, 2), "S_vs_S2_px": ndiff(im["S"], im["S2"])}
            res[f"{name} gi={int(gi)}"] = r
            print(name, "gi", gi, json.dumps(r), flush=True)
finally:
    call("POST", "/api/debug/gi", {"enabled": True})
    call("POST", "/api/debug/tonemap", {"curve": 1})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    call("POST", P, {"world_switch": False})
json.dump(res, open(os.path.join(out, "f73b_pix.json"), "w"), indent=1)
