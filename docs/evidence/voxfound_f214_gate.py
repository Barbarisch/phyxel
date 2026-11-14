"""F2.14 pixel gates: "no holes where levels meet" and "no fattening", plus the looks check vs level 0.
Same pose, wind 0, GI probes off, today's world hidden (the background is sky). Per pose and pixels-per-voxel P:
  E   = preview hidden (the background)        E2 = again (control: what "the same" looks like)
  L0..L3 = one level for the whole region (lod_draw k)
  M   = level by size on screen (lod_auto, lod_px P)          M2 = again (control for M)
A pixel is EMPTY in a frame when it matches E within T on every channel (viewport box only).
  hole      = empty in M, covered in EVERY single-level frame       (must be 0)
  fattening = covered in M, empty in EVERY single-level frame       (must be 0)
  detector  = empty in M, covered in L0 (> 0 where coarse levels lose thin parts: shows the detector finds empties)
Run from the repo root. Usage: f214_gate.py poses.json [P ...]"""
import json, os, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (45, 670, 242, 1196)   # y0, y1, x0, x1 of the viewport
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


def empty(a, e):
    return np.all(np.abs(a - e) <= T, axis=2)


def changed(a, b, t=24):
    return float(np.mean(np.any(np.abs(a - b) > t, axis=2)) * 100.0)


poses = json.load(open(sys.argv[1]))
Ps = [float(p) for p in sys.argv[2:]] or [1.0, 4.0]
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True})
out = {}
try:
    for name in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        call("POST", "/api/debug/voxfound_preview", {"show_preview": False})
        E = shot(f"f214_{name}_E.png", 6.0)
        E2 = shot(f"f214_{name}_E2.png")
        call("POST", "/api/debug/voxfound_preview", {"show_preview": True})
        L = []
        for k in range(4):
            call("POST", "/api/debug/voxfound_preview", {"lod_draw": k})
            L.append(shot(f"f214_{name}_L{k}.png"))
        coveredAll = np.all([~empty(l, E) for l in L], axis=0)
        emptyAll = np.all([empty(l, E) for l in L], axis=0)
        ctrlE = int(np.sum(~empty(E2, E)))
        for P in Ps:
            r = call("POST", "/api/debug/voxfound_preview", {"lod_auto": True, "lod_px": P})
            M = shot(f"f214_{name}_P{P:g}.png")
            M2 = shot(f"f214_{name}_P{P:g}b.png")
            st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            eM = empty(M, E)
            res = {"hole": int(np.sum(eM & coveredAll)), "fattening": int(np.sum(~eM & emptyAll)),
                   "detector_empty_in_M_covered_in_L0": int(np.sum(eM & ~empty(L[0], E))),
                   "control_E_E2_px": ctrlE, "control_M_M2_changed_pct": round(changed(M, M2), 3),
                   "M_vs_L0_changed_pct": round(changed(M, L[0]), 3),
                   "L0_vs_L1_changed_pct": round(changed(L[0], L[1]), 3),
                   "faces_by_level": st.get("faces_by_level"), "cluster_faces_drawn": st.get("cluster_faces_drawn"),
                   "lod_focal_px": st.get("lod_focal_px"), "lod_code": st.get("lod_code"), "ok": r.get("ok")}
            out[f"{name} P{P:g}"] = res
            print(name, f"P={P:g}", json.dumps(res), flush=True)
            call("POST", "/api/debug/voxfound_preview", {"lod_auto": False})
            call("POST", "/api/debug/voxfound_preview", {"lod_draw": 0})
finally:
    call("POST", "/api/debug/voxfound_preview", {"lod_draw": 0})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f214_gate.json"), "w"), indent=1)
