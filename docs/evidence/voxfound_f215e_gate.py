"""F2.15d seam gate. Region built per seam depth (with closed casters too). Today's world hidden, wind 0, GI probes off,
LOD off. Per pose: M = shadow_mdi (main faces, per-cell clusters)  D = + light test, main faces only
S = + light test, main + seam  C = + light test, closed casters.  Counts vs M as in F2.15b; S2 = control.
usage: f215e_gate.py poses.json depth [depth ...]"""
import json, os, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (45, 670, 242, 1196)


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=3600) as resp:
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
out = {}
for depth in [int(a) for a in sys.argv[2:]]:
    r = call("POST", "/api/debug/voxfound_preview",
             {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "closed_casters": True,
              "caster_seam_depth": 0, "caster_exit_reach": depth, "region": {"from": [254, 0, 2], "to": [257, 1, 5]}})
    r.pop("palette", None)
    build = {k: r.get(k) for k in ("faces", "caster_faces", "seam_faces", "seam_depth", "exit_reach", "build_wall_ms",
                                   "gpu_face_build_ms_sum")}
    print("build", depth, json.dumps(build), flush=True)
    call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
    call("POST", "/api/debug/voxfound_preview", {"hide_world": True})
    call("POST", "/api/debug/voxfound_preview", {"show_preview": True})
    call("POST", "/api/debug/voxfound_preview", {"shadow_mdi": True})
    res_d = {"build": build}
    try:
        for name in ("overview_south", "top_down", "forest_floor_open", "east_low", "canopy_close"):
            x, y, z, yaw, pitch = poses[name]
            call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
            imgs, res = {}, {}
            for tag, dc, closed, seam in (("M", False, False, False), ("D", True, False, False),
                                          ("S", True, False, True), ("C", True, True, False)):
                call("POST", "/api/debug/voxfound_preview", {"shadow_dir_cull": dc, "shadow_closed": closed,
                                                             "shadow_seam": seam})
                imgs[tag] = shot(f"f215e_{depth}_{name}_{tag}.png", 5.0)
                st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
                res[f"faces_cast_{tag}"] = [st.get("shadow_cluster_faces_mid"), st.get("shadow_cluster_faces_near")]
            call("POST", "/api/debug/voxfound_preview", {"shadow_dir_cull": True, "shadow_closed": False,
                                                         "shadow_seam": True})
            imgs["S2"] = shot(f"f215e_{depth}_{name}_S2.png")
            for t in ("D", "S", "C"):
                res[f"{t}_vs_M"] = vs(imgs["M"], imgs[t])
            res["S_vs_C"] = vs(imgs["C"], imgs["S"])
            res["control_S_px"] = int(np.any(imgs["S"] != imgs["S2"], axis=2).sum())
            res_d[name] = res
            print(depth, name, json.dumps(res), flush=True)
    finally:
        call("POST", "/api/debug/voxfound_preview", {"shadow_dir_cull": False, "shadow_closed": True, "shadow_seam": True})
        call("POST", "/api/debug/voxfound_preview", {"shadow_mdi": False})
        call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
    out[str(depth)] = res_d
json.dump(out, open(os.path.join(HERE, "f215e_gate.json"), "w"), indent=1)

