"""F2.14 part 2 gate: coarse shadow casters. Same pose, wind 0, GI probes off, today's world hidden, the main view
already by size on screen (lod_auto, lod_px P) in every frame, so ONLY the casters differ:
  A  = casters level 0 (lod_casters off)      A2 = again (control)
  B  = casters by size on screen (lod_casters on)   B2 = again (control)
  N  = no face casters at all (sensitivity: what the face shadows are worth on screen)
Reported per pose and P: % viewport pixels changed (> 24/255) B vs A, the controls, N vs A; and the sign of the change
(B darker than A = shadow gained, e.g. self-shadowing by a coarse caster; B lighter = shadow lost).
Run from the repo root. Usage: f214b_gate.py poses.json [P ...]"""
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


def darker(a, b, t=24):   # a darker than b by more than t (luma)
    la, lb = a.mean(axis=2), b.mean(axis=2)
    return float(np.mean(la < lb - t) * 100.0), float(np.mean(la > lb + t) * 100.0)


poses = json.load(open(sys.argv[1]))
Ps = [float(p) for p in sys.argv[2:]] or [4.0, 8.0]
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True})
out = {}
try:
    for name in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        for P in Ps:
            call("POST", "/api/debug/voxfound_preview", {"lod_auto": True, "lod_px": P, "lod_casters": False})
            A = shot(f"f214b_{name}_P{P:g}_A.png", 6.0)
            A2 = shot(f"f214b_{name}_P{P:g}_A2.png")
            stA = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            call("POST", "/api/debug/voxfound_preview", {"lod_casters": True})
            B = shot(f"f214b_{name}_P{P:g}_B.png")
            B2 = shot(f"f214b_{name}_P{P:g}_B2.png")
            stB = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            call("POST", "/api/debug/voxfound_preview", {"lod_casters": False})
            call("POST", "/api/debug/voxfound_preview", {"cast": {"faces": False}})
            N = shot(f"f214b_{name}_P{P:g}_N.png")
            call("POST", "/api/debug/voxfound_preview", {"cast": {"faces": True}})
            dk, lt = darker(B, A)
            res = {"B_vs_A_changed_pct": round(changed(B, A), 3), "B_darker_pct": round(dk, 3),
                   "B_lighter_pct": round(lt, 3),
                   "control_A_A2_pct": round(changed(A, A2), 3), "control_B_B2_pct": round(changed(B, B2), 3),
                   "sensitivity_no_face_casters_vs_A_pct": round(changed(N, A), 3),
                   "shadow_faces_mid_A": stA.get("shadow_faces_mid"), "shadow_faces_near_A": stA.get("shadow_faces_near"),
                   "shadow_faces_mid_B": stB.get("shadow_faces_mid"), "shadow_faces_near_B": stB.get("shadow_faces_near"),
                   "by_level_mid_B": stB.get("shadow_faces_by_level_mid"),
                   "by_level_near_B": stB.get("shadow_faces_by_level_near"),
                   "floor_B": stB.get("shadow_lod_floor"), "lod_casters_B": stB.get("lod_casters")}
            out[f"{name} P{P:g}"] = res
            print(name, f"P={P:g}", json.dumps(res), flush=True)
finally:
    call("POST", "/api/debug/voxfound_preview", {"lod_auto": False, "lod_casters": False})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f214b_gate.json"), "w"), indent=1)
