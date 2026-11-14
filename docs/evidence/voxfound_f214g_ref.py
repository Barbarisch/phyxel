"""F2.14g look gate against the LEVEL-0 REFERENCE. Region built with lod_card_ab (both meshings on the device);
today's world hidden, wind 0, GI probes off. Per pose and P:
  R = lod_auto off (level 0 everywhere)   E = preview hidden (background)
  A = lod_auto P, lod_cards_split false (one pyramid, cards hide: F2.13-F2.14f)   A2 = again (control)
  B = lod_auto P, lod_cards_split true (two pyramids)                              B2 = again (control)
Over the pixels where A and B differ: mean |A - R| vs |B - R|, and which is closer per pixel. Holes: covered in R
(> 30 from E) but background in X (<= 10 from E), more than P/2 px from R's coverage outline (an outline shift of up to
half a coarse voxel is the half-solid rule, F2.14d). Usage: f214g_ref.py poses.json [P ...]"""
import json, os, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image
from scipy import ndimage

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (45, 670, 242, 1196)


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=300) as resp:
        return json.loads(resp.read().decode())


def shot(name, settle=3.0):
    time.sleep(settle)
    d = os.path.join(HERE, name)
    shutil.copy(call("GET", "/api/screenshot")["path"], d)
    return np.asarray(Image.open(d).convert("RGB")).astype(np.int16)[BOX[0]:BOX[1], BOX[2]:BOX[3]]


def holes(X, R, E, P):
    dR = np.max(np.abs(R - E), axis=2); dX = np.max(np.abs(X - E), axis=2)
    cov = dR > 30
    edge = (cov ^ ndimage.binary_erosion(cov)) | (~cov ^ ndimage.binary_erosion(~cov))
    far = ndimage.distance_transform_edt(~edge) > P / 2
    return int(np.sum(cov & (dX <= 10) & far))


poses = json.load(open(sys.argv[1]))
Ps = [float(p) for p in sys.argv[2:]] or [8.0, 32.0]
call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True})
out = {}
try:
    for name in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        call("POST", "/api/debug/voxfound_preview", {"show_preview": False})
        E = shot(f"f214g_{name}_E.png", 6.0)
        call("POST", "/api/debug/voxfound_preview", {"show_preview": True})   # (lod_* keys are a separate branch)
        call("POST", "/api/debug/voxfound_preview", {"lod_auto": False})
        R = shot(f"f214g_{name}_R.png", 5.0)
        for P in Ps:
            call("POST", "/api/debug/voxfound_preview", {"lod_auto": True, "lod_px": P, "lod_cards_split": False})
            A = shot(f"f214g_{name}_P{P:g}_A.png", 5.0)
            A2 = shot(f"f214g_{name}_P{P:g}_A2.png")
            call("POST", "/api/debug/voxfound_preview", {"lod_cards_split": True})
            B = shot(f"f214g_{name}_P{P:g}_B.png", 5.0)
            B2 = shot(f"f214g_{name}_P{P:g}_B2.png")
            m = np.any(np.abs(A - B) > 24, axis=2)
            eA = np.max(np.abs(A - R), axis=2)[m]; eB = np.max(np.abs(B - R), axis=2)[m]
            res = {"px_A_ne_B": int(m.sum()),
                   "mean_err_A_vs_L0": round(float(eA.mean()), 2) if m.any() else 0.0,
                   "mean_err_B_vs_L0": round(float(eB.mean()), 2) if m.any() else 0.0,
                   "B_closer_px": int((eB < eA).sum()), "A_closer_px": int((eA < eB).sum()),
                   "holes_A": holes(A, R, E, P), "holes_B": holes(B, R, E, P),
                   "control_A_px": int(np.any(A != A2, axis=2).sum()), "control_B_px": int(np.any(B != B2, axis=2).sum())}
            out[f"{name} P{P:g}"] = res
            print(name, f"P={P:g}", json.dumps(res), flush=True)
finally:
    call("POST", "/api/debug/voxfound_preview", {"lod_auto": False, "lod_cards_split": True})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f214g_ref.json"), "w"), indent=1)
