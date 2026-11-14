"""F2.14f gate: the leaf-mass appearance. Same rig as F2.14c (build lod_sprig_level h, lod_auto P, today's world hidden,
wind 0, GI probes off):
  A  = lod_sprig false (no leaf mass, sprigs everywhere: the card look)        A2 = again (control)
  B0 = handoff on, lod_leaf_look false (the F2.14c look: the red)
  B  = handoff on, lod_leaf_look true (the leaf mass shaded as cards)          B2 = again (control)
Per pose: % px changed vs A (> 24 levels in any channel), mean luma, and mean |X - A| over the pixels where A and B0
differ (the leaf-mass pixels). Usage: f214f_gate.py poses.json [P ...]"""
import json, os, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (45, 670, 242, 1196)


def call(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=300) as r:
        return json.loads(r.read().decode())


def shot(name, settle=2.5):
    time.sleep(settle)
    dst = os.path.join(HERE, name)
    shutil.copy(call("GET", "/api/screenshot")["path"], dst)
    a = np.asarray(Image.open(dst).convert("RGB")).astype(np.int16)
    return a[BOX[0]:BOX[1], BOX[2]:BOX[3]]


def changed(a, b, t=24):
    return float(np.mean(np.any(np.abs(a - b) > t, axis=2)) * 100.0)


poses = json.load(open(sys.argv[1]))
Ps = [float(p) for p in sys.argv[2:]] or [8.0]
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True})
out = {}
try:
    for name in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        for P in Ps:
            call("POST", "/api/debug/voxfound_preview", {"lod_auto": True, "lod_px": P, "lod_sprig": False})
            A = shot(f"f214f_{name}_P{P:g}_A.png", 5.0)
            A2 = shot(f"f214f_{name}_P{P:g}_A2.png")
            call("POST", "/api/debug/voxfound_preview", {"lod_sprig": True, "lod_leaf_look": False})
            B0 = shot(f"f214f_{name}_P{P:g}_B0.png", 4.0)
            call("POST", "/api/debug/voxfound_preview", {"lod_leaf_look": True})
            B = shot(f"f214f_{name}_P{P:g}_B.png", 4.0)
            B2 = shot(f"f214f_{name}_P{P:g}_B2.png")
            m = np.any(np.abs(A - B0) > 24, axis=2)
            e = lambda X: round(float(np.max(np.abs(X - A), axis=2)[m].mean()), 2) if m.any() else 0.0
            res = {"B0_vs_A_changed_pct": round(changed(B0, A), 3), "B_vs_A_changed_pct": round(changed(B, A), 3),
                   "control_A_pct": round(changed(A, A2), 3), "control_B_pct": round(changed(B, B2), 3),
                   "luma_A": round(float(A.mean()), 2), "luma_B0": round(float(B0.mean()), 2),
                   "luma_B": round(float(B.mean()), 2), "mass_px": int(m.sum()),
                   "mass_err_B0": e(B0), "mass_err_B": e(B)}
            out[f"{name} P{P:g}"] = res
            print(name, f"P={P:g}", json.dumps(res), flush=True)
finally:
    call("POST", "/api/debug/voxfound_preview", {"lod_auto": False, "lod_sprig": True, "lod_leaf_look": True})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f214f_gate.json"), "w"), indent=1)
