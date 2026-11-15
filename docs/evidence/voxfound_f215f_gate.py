"""F2.15f gate (D = direct per-chunk draws, the reference): compacted + merged commands. Region built per direction order (octant / axis). Today's world hidden,
wind 0, GI probes off, LOD off, defaults otherwise (dir_clusters + dir_cull on). Per pose: U = per-cluster commands
(today), K = mdi_compact, K2 = K again (control). Commands that draw + faces from the stats route; pixels K vs U.
usage: f215f_gate.py poses.json order [order ...]"""
import json, os, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=3600) as resp:
        return json.loads(resp.read().decode())


def shot(name, settle=3.0):
    time.sleep(settle)
    d = os.path.join(HERE, name)
    shutil.copy(call("GET", "/api/screenshot")["path"], d)
    return np.asarray(Image.open(d).convert("RGB")).astype(np.int16)


def stats():
    st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
    return {k: st.get(k) for k in ("clusters", "clusters_drawn", "cluster_faces_drawn", "mdi_compact")}


poses = json.load(open(sys.argv[1]))
out = {}
for order in sys.argv[2:]:
    r = call("POST", "/api/debug/voxfound_preview",
             {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "dir_order": order,
              "region": {"from": [254, 0, 2], "to": [257, 1, 5]}})
    build = {k: r.get(k) for k in ("faces", "build_wall_ms")}
    print("build", order, json.dumps(build), flush=True)
    call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
    call("POST", "/api/debug/voxfound_preview", {"hide_world": True})
    call("POST", "/api/debug/voxfound_preview", {"show_preview": True})
    res_o = {"build": build}
    try:
        for name in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
            x, y, z, yaw, pitch = poses[name]
            call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
            res = {}
            call("POST", "/api/debug/voxfound_preview", {"mdi": False})
            D = shot(f"f215f_{order}_{name}_D.png", 6.0)
            call("POST", "/api/debug/voxfound_preview", {"mdi": True})
            call("POST", "/api/debug/voxfound_preview", {"mdi_compact": False})
            U = shot(f"f215f_{order}_{name}_U.png", 4.0)
            res["U"] = stats()
            res["compact_reply"] = call("POST", "/api/debug/voxfound_preview", {"mdi_compact": True})
            K = shot(f"f215f_{order}_{name}_K.png", 4.0)
            res["K"] = stats()
            K2 = shot(f"f215f_{order}_{name}_K2.png")
            res["K_vs_U_px"] = int(np.any(K != U, axis=2).sum())
            res["K_vs_U_gt8_px"] = int((np.max(np.abs(K - U), axis=2) > 8).sum())
            res["control_K_px"] = int(np.any(K != K2, axis=2).sum())
            res["U_vs_D_gt8_px"] = int((np.max(np.abs(U - D), axis=2) > 8).sum())
            res["K_vs_D_gt8_px"] = int((np.max(np.abs(K - D), axis=2) > 8).sum())
            res_o[name] = res
            print(order, name, json.dumps(res), flush=True)
    finally:
        call("POST", "/api/debug/voxfound_preview", {"mdi_compact": False})
        call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
    out[order] = res_o
json.dump(out, open(os.path.join(HERE, "f215f_gate.json"), "w"), indent=1)
