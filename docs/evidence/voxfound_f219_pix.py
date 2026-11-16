"""F2.19 pixel gate: today's store (A) vs the 1/8 m store (B) at the 5 poses, same session.
Debug tonemap curve 0, wind 0, GI off. Shaded (mode 0): A, A again (control), B, B again (control).
Debug view 11 (flat grey per voxel.frag fragment): A, B -> which px are voxel faces in each store.
Usage: python f219_pix.py poses.json outdir"""
import json, os, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
CLEAN = (425, 105, 1195, 620)
REPO = "G:/Github/phyxel-matsimb-lab"
CFG = {"A": {"hide_world": False, "show_preview": False}, "B": {"hide_world": True, "show_preview": True}}


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=600) as resp:
        return json.loads(resp.read().decode())


def shot(out, name):
    d = os.path.join(out, name)
    shutil.copy(os.path.join(REPO, call("GET", "/api/screenshot")["path"]), d)
    x0, y0, x1, y1 = CLEAN
    return np.asarray(Image.open(d).convert("RGB")).astype(np.int16)[y0:y1, x0:x1]


def ndiff(a, b, t=8):
    return np.max(np.abs(a - b), axis=2) > t


def grey_mask(v):
    g = (np.max(v, axis=2) - np.min(v, axis=2)) <= 3
    vals, cnt = np.unique(v[..., 0][g], return_counts=True)
    dom = int(vals[np.argmax(cnt)])
    return g & (np.abs(v[..., 0] - dom) <= 6), dom


poses = json.load(open(sys.argv[1]))
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/tonemap", {"curve": 0})
res = {}
try:
    for name, (x, y, z, yaw, pitch) in poses.items():
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        im = {}
        call("POST", "/api/debug/shadow", {"mode": 0})
        for lab in ("A", "A2", "B", "B2"):
            call("POST", P, CFG[lab[0]])
            time.sleep(4.0)
            im[lab] = shot(out, f"f219_{name}_{lab}.png")
        call("POST", "/api/debug/shadow", {"mode": 11})
        for lab in ("A", "B"):
            call("POST", P, CFG[lab])
            time.sleep(3.0)
            im["g" + lab] = shot(out, f"f219_{name}_m11_{lab}.png")
        call("POST", "/api/debug/shadow", {"mode": 0})
        n = im["A"].shape[0] * im["A"].shape[1]
        dAB = ndiff(im["A"], im["B"])
        mA, domA = grey_mask(im["gA"])
        mB, domB = grey_mask(im["gB"])
        both = mA & mB
        r = {"px": n,
             "control_A_px": int(ndiff(im["A"], im["A2"]).sum()), "control_B_px": int(ndiff(im["B"], im["B2"]).sum()),
             "AB_pct": round(100.0 * dAB.sum() / n, 2),
             "faces_A_pct": round(100.0 * mA.sum() / n, 2), "faces_B_pct": round(100.0 * mB.sum() / n, 2),
             "faces_one_store_only_pct": round(100.0 * (mA ^ mB).sum() / n, 2),
             "faces_both_pct": round(100.0 * both.sum() / n, 2),
             "AB_pct_of_faces_both": round(100.0 * (dAB & both).sum() / max(both.sum(), 1), 2),
             "AB_pct_outside_faces_both": round(100.0 * (dAB & ~both).sum() / max((~both).sum(), 1), 2),
             "grey_dom": [domA, domB]}
        # diff map: red = differs where both are voxel faces, yellow = differs elsewhere
        dm = np.zeros(im["A"].shape, np.uint8)
        dm[dAB & both] = (255, 0, 0)
        dm[dAB & ~both] = (255, 220, 0)
        Image.fromarray(dm).save(os.path.join(out, f"f219_{name}_diffmap.png"))
        res[name] = r
        print(name, json.dumps(r), flush=True)
finally:
    call("POST", "/api/debug/shadow", {"mode": 0})
    call("POST", "/api/debug/tonemap", {"curve": 1})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    call("POST", "/api/debug/gi", {"enabled": True})
    call("POST", P, CFG["A"])
json.dump(res, open(os.path.join(out, "f219_pix.json"), "w"), indent=1)
