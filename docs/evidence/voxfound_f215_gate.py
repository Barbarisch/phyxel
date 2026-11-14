"""F2.15 gate (section 4.6 item 8): direction culling must not change a pixel. Today's world hidden, wind 0, GI probes
off, LOD off (the default). Per pose: A = per-cell clusters, B = per-direction clusters (test off), C = per-direction +
test (A2 / C2 = controls); px differing (any channel); faces passed to the vertex stage (cluster_faces_drawn)."""
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


def px(a, b):
    return int(np.any(a != b, axis=2).sum())


poses = json.load(open(sys.argv[1]))
call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True})
call("POST", "/api/debug/voxfound_preview", {"show_preview": True})
out = {}
try:
    for name in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        res = {}
        imgs = {}
        for tag, dc, cu in (("A", False, False), ("B", True, False), ("C", True, True)):
            call("POST", "/api/debug/voxfound_preview", {"dir_clusters": dc, "dir_cull": cu})
            imgs[tag] = shot(f"f215_{name}_{tag}.png", 5.0)
            imgs[tag + "2"] = shot(f"f215_{name}_{tag}2.png")
            st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            res[f"faces_{tag}"] = st.get("cluster_faces_drawn")
            res[f"commands_drawn_{tag}"] = st.get("clusters_drawn")
        res.update({"A_vs_B_px": px(imgs["A"], imgs["B"]), "B_vs_C_px": px(imgs["B"], imgs["C"]),
                    "A_vs_C_px": px(imgs["A"], imgs["C"]), "control_A_px": px(imgs["A"], imgs["A2"]),
                    "control_C_px": px(imgs["C"], imgs["C2"])})
        out[name] = res
        print(name, json.dumps(res), flush=True)
finally:
    call("POST", "/api/debug/voxfound_preview", {"dir_clusters": False, "dir_cull": False})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f215_gate.json"), "w"), indent=1)
