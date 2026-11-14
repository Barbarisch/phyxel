"""F2.14d gate: levels above 1 m (2..16 m) from the region pyramid. Same pose, wind 0, GI probes off, today's world hidden; the
main view by size on screen (lod_auto, lod_px P) in every frame:
  A  = lod_max 3 (levels to 1 m, the F2.14 state)   A2 = again (control)
  B  = lod_max 7 (levels to 16 m)                      B2 = again (control)
  E  = preview hidden (background), to measure coverage: canopy pixels lost (covered in A, background in B) = holes;
       gained (background in A, covered in B) = fattening.
Usage: f214d_gate.py poses.json [P ...]"""
import json, os, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (45, 670, 242, 1196)
T = 10


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


def d(a, e):
    return np.max(np.abs(a - e), axis=2)


poses = json.load(open(sys.argv[1]))
Ps = [float(p) for p in sys.argv[2:]] or [8.0, 16.0, 32.0]
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True})
out = {}
try:
    for name in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        call("POST", "/api/debug/voxfound_preview", {"show_preview": False})
        E = shot(f"f214d_{name}_E.png", 6.0)
        call("POST", "/api/debug/voxfound_preview", {"show_preview": True})
        for P in Ps:
            call("POST", "/api/debug/voxfound_preview", {"lod_auto": True, "lod_px": P, "lod_max": 3})
            A = shot(f"f214d_{name}_P{P:g}_A.png", 4.0)
            A2 = shot(f"f214d_{name}_P{P:g}_A2.png")
            stA = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            call("POST", "/api/debug/voxfound_preview", {"lod_max": 7})
            B = shot(f"f214d_{name}_P{P:g}_B.png", 4.0)
            B2 = shot(f"f214d_{name}_P{P:g}_B2.png")
            stB = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            dA, dB = d(A, E), d(B, E)
            res = {"B_vs_A_changed_pct": round(changed(B, A), 3),
                   "control_A_A2_pct": round(changed(A, A2), 3), "control_B_B2_pct": round(changed(B, B2), 3),
                   "lost_px": int(np.sum((dA > 3 * T) & (dB <= T))), "gained_px": int(np.sum((dA <= T) & (dB > 3 * T))),
                   "covered_A_px": int(np.sum(dA > 3 * T)), "covered_B_px": int(np.sum(dB > 3 * T)),
                   "luma_A": round(float(A.mean()), 2), "luma_B": round(float(B.mean()), 2),
                   "faces_A": stA.get("cluster_faces_drawn"), "faces_B": stB.get("cluster_faces_drawn"),
                   "by_level_A": stA.get("faces_by_level"), "by_level_B": stB.get("faces_by_level")}
            out[f"{name} P{P:g}"] = res
            print(name, f"P={P:g}", json.dumps(res), flush=True)
finally:
    call("POST", "/api/debug/voxfound_preview", {"lod_auto": False, "lod_max": 7})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f214d_gate.json"), "w"), indent=1)
