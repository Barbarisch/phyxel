"""F2.18 shaded pixel check on the flora gate scene: level 0, 0 again (control), 2 at two poses, mode 0.
Then the shadow-only view (mode 1) the same way: whether the shadow map has T-junction leaks (recorded, not fixed)."""
import json, os, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
VIEW = (245, 47, 1195, 670)
CLEAN = (425, 105, 1195, 620)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tj")
REPO = "G:/Github/phyxel-matsimb-lab"


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=600) as resp:
        return json.loads(resp.read().decode())


def shot(name):
    time.sleep(3.0)
    d = os.path.join(OUT, name + ".png")
    shutil.copy(os.path.join(REPO, call("GET", "/api/screenshot")["path"]), d)
    return np.asarray(Image.open(d).convert("RGB")).astype(np.int16)


def diff(a, b, box=VIEW):
    x0, y0, x1, y1 = box
    return int((np.max(np.abs(a[y0:y1, x0:x1] - b[y0:y1, x0:x1]), axis=2) > 8).sum())


poses = json.load(open(sys.argv[1]))
r = call("POST", P, {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "editable": True,
                     "region": {"from": [254, 0, 2], "to": [257, 1, 5]}})
print("build", r.get("faces"), r.get("sprigs"), r.get("error"), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
call("POST", P, {"hide_world": True, "show_preview": True})
res = {}
for mode in (0, 1):
    call("POST", "/api/debug/shadow", {"mode": mode})
    for name in ("forest_floor_open", "overview_south"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        call("POST", P, {"tjunction": 0})
        a = shot(f"sh{mode}_{name}_L0")
        a2 = shot(f"sh{mode}_{name}_L0b")
        call("POST", P, {"tjunction": 2})
        b = shot(f"sh{mode}_{name}_L2")
        call("POST", P, {"tjunction": 0})
        n = (VIEW[2] - VIEW[0]) * (VIEW[3] - VIEW[1])
        res[f"mode{mode}_{name}"] = {"control_px": diff(a, a2), "L2_vs_L0_px": diff(a, b), "view_px": n,
                                    "L2_vs_L0_pct": round(100.0 * diff(a, b) / n, 4)}
        print(mode, name, res[f"mode{mode}_{name}"], flush=True)
call("POST", "/api/debug/shadow", {"mode": 0})
json.dump(res, open(os.path.join(OUT, "shaded.json"), "w"), indent=1)
