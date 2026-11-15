"""Shadow-map leak census: bare terrain (flora off), shadow-only view (mode 1), main view fixed (tjunction 2) and
not (0). A speck = a pixel > 25 off both opposite neighbours (left+right or up+down) while those two agree (<= 6).
Specks present at level 2 come from something other than the main view's cracks (the shadow map, or noise)."""
import json, os, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
CLEAN = (425, 105, 1195, 620)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tj")
REPO = "G:/Github/phyxel-matsimb-lab"


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=600) as resp:
        return json.loads(resp.read().decode())


def specks(a):
    x0, y0, x1, y1 = CLEAN
    v = a[y0:y1, x0:x1].astype(np.int16)
    d = lambda p, q: np.max(np.abs(p - q), axis=2)
    c = v[1:-1, 1:-1]
    lr = (d(v[1:-1, :-2], v[1:-1, 2:]) <= 6) & (d(c, v[1:-1, :-2]) > 25) & (d(c, v[1:-1, 2:]) > 25)
    ud = (d(v[:-2, 1:-1], v[2:, 1:-1]) <= 6) & (d(c, v[:-2, 1:-1]) > 25) & (d(c, v[2:, 1:-1]) > 25)
    m = lr | ud
    bright = m & (c.sum(axis=2) > v[1:-1, :-2].sum(axis=2))
    return int(m.sum()), int(bright.sum())


poses = json.load(open(sys.argv[1]))
r = call("POST", P, {"type": "Perlin", "seed": 7, "flora": False, "hide_world": False, "editable": True,
                     "region": {"from": [254, 0, 2], "to": [257, 1, 5]}})
print("build", r.get("faces"), r.get("error"), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
call("POST", P, {"hide_world": True, "show_preview": True, "sprigs": False})
call("POST", "/api/debug/shadow", {"mode": 1})
res = {}
for name, (x, y, z, yaw, pitch) in poses.items():
    call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
    for L in (0, 2):
        call("POST", P, {"tjunction": L})
        time.sleep(3.0)
        d = os.path.join(OUT, f"leak_{name}_L{L}.png")
        shutil.copy(os.path.join(REPO, call("GET", "/api/screenshot")["path"]), d)
        res[f"{name}_L{L}"] = specks(np.asarray(Image.open(d).convert("RGB")))
        print(name, L, res[f"{name}_L{L}"], flush=True)
call("POST", "/api/debug/shadow", {"mode": 0})
call("POST", P, {"tjunction": 0})
json.dump(res, open(os.path.join(OUT, "shadowleak.json"), "w"), indent=1)
