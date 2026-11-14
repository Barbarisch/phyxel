"""F2.14h gate: the coarse canopy's shape vs the cards, per leaf fill t. For each t the gate region is rebuilt (h=2,
lod_levels 3, the main variant = the F2.14c/f mass, the alt = the leaf pyramid at fill t); lod_auto P, today's world
hidden, wind 0, GI probes off:
  E = preview hidden   A = lod_sprig false (sprigs everywhere: the cards)   B0 = handoff, main   B = handoff, alt (t)
Per pose: % px changed vs A, luma, mean |X - A| over the pixels where A and B0 differ, coverage lost / gained vs E
(covered in A and background in X / background in A and covered in X). Usage: f214h_gate.py poses.json P t [t ...]"""
import json, os, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (45, 670, 242, 1196)
T = 10


def call(m, p, b=None, t=3600):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=t) as resp:
        return json.loads(resp.read().decode())


def shot(name, settle=3.0):
    time.sleep(settle)
    d = os.path.join(HERE, name)
    shutil.copy(call("GET", "/api/screenshot")["path"], d)
    return np.asarray(Image.open(d).convert("RGB")).astype(np.int16)[BOX[0]:BOX[1], BOX[2]:BOX[3]]


def changed(a, b, t=24):
    return float(np.mean(np.any(np.abs(a - b) > t, axis=2)) * 100.0)


poses = json.load(open(sys.argv[1]))
P = float(sys.argv[2])
fills = [float(t) for t in sys.argv[3:]]
out = {}
call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
try:
    for t in fills:
        r = call("POST", "/api/debug/voxfound_preview", {
            "type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "cluster_blocks": 4, "lod_levels": 3,
            "lod_sprig_level": 2, "lod_alt": {"split_cards": False, "leaf_fill": t},
            "region": {"from": [254, 0, 2], "to": [257, 1, 5]}})
        faces = {"lod_leaf_mass_faces": r.get("lod_leaf_mass_faces"), "lod_leaf_mass_faces_alt": r.get("lod_leaf_mass_faces_alt"),
                 "lod_faces": r.get("lod_faces"), "lod_faces_alt": r.get("lod_faces_alt"), "error": r.get("error")}
        print("fill", t, json.dumps(faces), flush=True)
        out[f"fill {t:g} build"] = faces
        call("POST", "/api/debug/voxfound_preview", {"hide_world": True})
        for name in ("overview_south", "top_down", "east_low"):
            x, y, z, yaw, pitch = poses[name]
            call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
            call("POST", "/api/debug/voxfound_preview", {"show_preview": False})
            E = shot(f"f214h_{name}_E.png", 6.0)
            call("POST", "/api/debug/voxfound_preview", {"show_preview": True})   # lod_* keys are a separate branch
            call("POST", "/api/debug/voxfound_preview", {"lod_auto": True, "lod_px": P, "lod_sprig": False, "lod_alt_on": False})
            A = shot(f"f214h_{name}_P{P:g}_A.png", 5.0)
            call("POST", "/api/debug/voxfound_preview", {"lod_sprig": True})
            B0 = shot(f"f214h_{name}_P{P:g}_B0.png", 5.0)
            call("POST", "/api/debug/voxfound_preview", {"lod_alt_on": True})
            B = shot(f"f214h_{name}_P{P:g}_t{t:g}_B.png", 5.0)
            B2 = shot(f"f214h_{name}_P{P:g}_t{t:g}_B2.png")
            m = np.any(np.abs(A - B0) > 24, axis=2)
            err = lambda X: round(float(np.max(np.abs(X - A), axis=2)[m].mean()), 2) if m.any() else 0.0
            dA = np.max(np.abs(A - E), axis=2)
            cov = lambda X: (int(np.sum((dA > 3 * T) & (np.max(np.abs(X - E), axis=2) <= T))),
                             int(np.sum((dA <= T) & (np.max(np.abs(X - E), axis=2) > 3 * T))))
            res = {"B0_vs_A_pct": round(changed(B0, A), 3), "B_vs_A_pct": round(changed(B, A), 3),
                   "control_B_pct": round(changed(B, B2), 3), "luma_A": round(float(A.mean()), 2),
                   "luma_B0": round(float(B0.mean()), 2), "luma_B": round(float(B.mean()), 2),
                   "mass_err_B0": err(B0), "mass_err_B": err(B),
                   "lost_gained_B0": cov(B0), "lost_gained_B": cov(B)}
            out[f"fill {t:g} {name}"] = res
            print("fill", t, name, json.dumps(res), flush=True)
finally:
    call("POST", "/api/debug/voxfound_preview", {"lod_auto": False, "lod_alt_on": False})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f214h_gate.json"), "w"), indent=1)
