"""F2.15b gate: shadow casters by the light's direction. Today's world hidden, wind 0, GI probes off, LOD off.
Per pose: A = default casters (per-chunk draws), M = shadow_mdi (per-cell clusters), D = shadow_mdi + per-direction
clusters + light test (D2 control). px differing (any channel, and > 8 levels); faces cast (mid / near)."""
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


def px(a, b, t=0):
    return int(np.any(np.abs(a - b) > t, axis=2).sum())


poses = json.load(open(sys.argv[1]))
call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True})
call("POST", "/api/debug/voxfound_preview", {"show_preview": True})
out = {}
try:
    for name in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        imgs, res = {}, {}
        for tag, mdi, dc in (("A", False, False), ("M", True, False), ("D", True, True)):
            call("POST", "/api/debug/voxfound_preview", {"shadow_mdi": mdi})
            call("POST", "/api/debug/voxfound_preview", {"shadow_dir_cull": dc})
            imgs[tag] = shot(f"f215b_{name}_{tag}.png", 5.0)
            if tag == "D":
                imgs["D2"] = shot(f"f215b_{name}_D2.png")
            if mdi:
                st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
                res[f"faces_cast_{tag}"] = [st.get("shadow_cluster_faces_mid"), st.get("shadow_cluster_faces_near")]
        res.update({"A_vs_M_px": px(imgs["A"], imgs["M"]), "M_vs_D_px": px(imgs["M"], imgs["D"]),
                    "M_vs_D_px_gt8": px(imgs["M"], imgs["D"], 8), "control_D_px": px(imgs["D"], imgs["D2"]),
                    "preview_px": int(imgs["A"].shape[0] * imgs["A"].shape[1])})
        out[name] = res
        print(name, json.dumps(res), flush=True)
finally:
    call("POST", "/api/debug/voxfound_preview", {"shadow_mdi": False})
    call("POST", "/api/debug/voxfound_preview", {"shadow_dir_cull": False})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f215b_gate.json"), "w"), indent=1)
