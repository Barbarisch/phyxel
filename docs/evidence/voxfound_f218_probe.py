"""T-junction crack probe: flat debug view (mode 11), count isolated non-flat pixels in the 3D viewport.
usage: tj_probe.py poses.json tag [setup]"""
import json, os, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
VIEW = (425, 105, 1195, 620)   # viewport minus the editor overlays (equipper, compass, HP, cube bar)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tj")
os.makedirs(OUT, exist_ok=True)


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=600) as resp:
        return json.loads(resp.read().decode())


def cracks(a):
    x0, y0, x1, y1 = VIEW
    v = a[y0:y1, x0:x1].astype(np.int16)
    flat_val = np.median(v.reshape(-1, 3), axis=0)
    flat = np.max(np.abs(v - flat_val), axis=2) <= 6
    nf = np.max(np.abs(v - flat_val), axis=2) > 25
    c = np.zeros_like(nf)
    lr = flat[1:-1, :-2] & flat[1:-1, 2:]
    ud = flat[:-2, 1:-1] & flat[2:, 1:-1]
    c[1:-1, 1:-1] = nf[1:-1, 1:-1] & (lr | ud)
    return int(c.sum()), int(flat.sum()), c


poses = json.load(open(sys.argv[1]))
tag = sys.argv[2]
if len(sys.argv) > 3 and sys.argv[3] == "setup":
    r = call("POST", P, {"type": "Perlin", "seed": 7, "flora": (len(sys.argv) < 5 or sys.argv[4] != "noflora"), "hide_world": False, "editable": True,
                         "region": {"from": [254, 0, 2], "to": [257, 1, 5]}})
    print("build", r.get("faces"), r.get("error"))
    call("POST", "/api/debug/foliage", {"windStrength": 0.0})
    call("POST", "/api/debug/gi", {"enabled": False})
    call("POST", P, {"hide_world": True, "show_preview": True, "sprigs": False})
call("POST", "/api/debug/shadow", {"mode": 11})
res = {}
for name, (x, y, z, yaw, pitch) in poses.items():
    call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
    time.sleep(3.0)
    d = os.path.join(OUT, f"{tag}_{name}.png")
    shutil.copy(os.path.join("G:/Github/phyxel-matsimb-lab", call("GET", "/api/screenshot")["path"]), d)
    a = np.asarray(Image.open(d).convert("RGB"))
    n, f, c = cracks(a)
    Image.fromarray((c * 255).astype(np.uint8)).save(os.path.join(OUT, f"{tag}_{name}_mask.png"))
    res[name] = {"crack_px": n, "flat_px": f}
    print(name, n, f, flush=True)
call("POST", "/api/debug/shadow", {"mode": 0})
json.dump(res, open(os.path.join(OUT, f"{tag}.json"), "w"), indent=1)
