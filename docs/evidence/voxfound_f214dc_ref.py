"""F2.14d card-cell fix, look gate vs the level-0 reference R (lod_auto off) at the same pose: over the pixels where
A (cards hide, F2.13 meshing) and B (the fix) differ, mean |A - R| vs |B - R| (lower = closer to level 0)."""
import json, os, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image
API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (45, 670, 242, 1196)
def call(m, p, b=None):
    req = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=300) as r:
        return json.loads(r.read().decode())
def shot(name, settle=3.0):
    time.sleep(settle)
    dst = os.path.join(HERE, name)
    shutil.copy(call("GET", "/api/screenshot")["path"], dst)
    return np.asarray(Image.open(dst).convert("RGB")).astype(np.int16)[BOX[0]:BOX[1], BOX[2]:BOX[3]]
poses = json.load(open(sys.argv[1]))
call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True})
out = {}
try:
    for name, (x, y, z, yaw, pitch) in poses.items():
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        call("POST", "/api/debug/voxfound_preview", {"lod_auto": False})
        R = shot(f"f214dc_{name}_R.png", 6.0)
        for P in (8.0, 32.0):
            call("POST", "/api/debug/voxfound_preview", {"lod_auto": True, "lod_px": P, "lod_cards_hide": True})
            A = shot(f"f214dc_{name}_P{P:g}_A.png", 4.0)
            call("POST", "/api/debug/voxfound_preview", {"lod_cards_hide": False})
            B = shot(f"f214dc_{name}_P{P:g}_B.png", 4.0)
            m = np.any(np.abs(A - B) > 24, axis=2)
            eA = np.max(np.abs(A - R), axis=2)[m]; eB = np.max(np.abs(B - R), axis=2)[m]
            res = {"px_A_ne_B": int(m.sum()), "mean_err_A_vs_L0": round(float(eA.mean()), 2) if m.any() else 0,
                   "mean_err_B_vs_L0": round(float(eB.mean()), 2) if m.any() else 0,
                   "B_closer_px": int((eB < eA).sum()), "A_closer_px": int((eA < eB).sum())}
            out[f"{name} P{P:g}"] = res
            print(name, f"P={P:g}", json.dumps(res), flush=True)
finally:
    call("POST", "/api/debug/voxfound_preview", {"lod_auto": False, "lod_cards_hide": False})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f214dc_ref.json"), "w"), indent=1)
