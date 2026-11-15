"""F2.15b closed-caster gate. Today's world hidden, wind 0, GI probes off, LOD off. Per pose:
  M = shadow_mdi (per-cell clusters, the main faces)   D = + light test, main faces   C = + light test, closed casters
Counts vs M: px > 8 levels brighter (shadow LEAKS), px > 8 darker (added shadows), any change; C2 = control."""
import json, os, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

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


def vs(M, X):
    d = X.sum(axis=2) - M.sum(axis=2)
    big = np.max(np.abs(X - M), axis=2) > 8
    return {"brighter_gt8": int(np.sum(big & (d > 0))), "darker_gt8": int(np.sum(big & (d < 0))),
            "any": int(np.any(X != M, axis=2).sum())}


poses = json.load(open(sys.argv[1]))
call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True})
call("POST", "/api/debug/voxfound_preview", {"show_preview": True})
call("POST", "/api/debug/voxfound_preview", {"shadow_mdi": True})
out = {}
try:
    for name in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        imgs, res = {}, {}
        for tag, dc, closed in (("M", False, False), ("D", True, False), ("C", True, True)):
            call("POST", "/api/debug/voxfound_preview", {"shadow_dir_cull": dc, "shadow_closed": closed})
            imgs[tag] = shot(f"f215c_{name}_{tag}.png", 5.0)
            st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            res[f"faces_cast_{tag}"] = [st.get("shadow_cluster_faces_mid"), st.get("shadow_cluster_faces_near")]
        imgs["C2"] = shot(f"f215c_{name}_C2.png")
        res["D_vs_M"] = vs(imgs["M"], imgs["D"])
        res["C_vs_M"] = vs(imgs["M"], imgs["C"])
        res["control_C_px"] = int(np.any(imgs["C"] != imgs["C2"], axis=2).sum())
        out[name] = res
        print(name, json.dumps(res), flush=True)
finally:
    call("POST", "/api/debug/voxfound_preview", {"shadow_dir_cull": False, "shadow_closed": True})
    call("POST", "/api/debug/voxfound_preview", {"shadow_mdi": False})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f215c_gate.json"), "w"), indent=1)
