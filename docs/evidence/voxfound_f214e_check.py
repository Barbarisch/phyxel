"""F2.14e: the same region built with CPU levels (lod_gpu false) then GPU levels (true): faces per level, the level
time, and the main view at P=8 / P=32 at two poses (must be 0 px apart; control = the same build shot twice)."""
import json, os, shutil, sys, time, urllib.request
import numpy as np
from PIL import Image
API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
BOX = (45, 670, 242, 1196)
def call(m, p, b=None, t=3600):
    req = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=t) as r:
        return json.loads(r.read().decode())
def shot(name, settle=3.0):
    time.sleep(settle)
    dst = os.path.join(HERE, name)
    shutil.copy(call("GET", "/api/screenshot")["path"], dst)
    return np.asarray(Image.open(dst).convert("RGB")).astype(np.int16)[BOX[0]:BOX[1], BOX[2]:BOX[3]]
poses = json.load(open(sys.argv[1]))
out = {}
imgs = {}
call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
try:
    for gpu in (False, True):
        body = {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "cluster_blocks": 4, "lod_levels": 7,
                "lod_gpu": gpu, "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
        t = time.time()
        r = call("POST", "/api/debug/voxfound_preview", body)
        wall = time.time() - t
        out["gpu" if gpu else "cpu"] = {k: r.get(k) for k in ("lod_faces", "lod_solid_volume_vs_l0", "lod_level_ms",
                                                              "lod_gpu_device_ms_sum", "build_wall_ms", "generate_ms",
                                                              "region_lod_ms", "error")}
        out["gpu" if gpu else "cpu"]["request_wall_s"] = round(wall, 1)
        print("gpu" if gpu else "cpu", json.dumps(out["gpu" if gpu else "cpu"]), flush=True)
        call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True})
        for name in ("overview_south", "top_down"):
            x, y, z, yaw, pitch = poses[name]
            call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
            for P in (8.0, 32.0):
                call("POST", "/api/debug/voxfound_preview", {"lod_auto": True, "lod_px": P, "lod_max": 7})
                tag = f"{'gpu' if gpu else 'cpu'}_{name}_P{P:g}"
                imgs[tag] = shot(f"f214e_{tag}.png", 5.0)
                imgs[tag + "_2"] = shot(f"f214e_{tag}_2.png")
    for name in ("overview_south", "top_down"):
        for P in (8, 32):
            a, b = imgs[f"cpu_{name}_P{P}"], imgs[f"gpu_{name}_P{P}"]
            ctl = imgs[f"cpu_{name}_P{P}_2"]
            res = {"cpu_vs_gpu_px": int(np.any(np.abs(a - b) > 0, axis=2).sum()),
                   "control_px": int(np.any(np.abs(a - ctl) > 0, axis=2).sum())}
            out[f"{name} P{P}"] = res
            print(name, P, res, flush=True)
finally:
    call("POST", "/api/debug/voxfound_preview", {"lod_auto": False})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "f214e_check.json"), "w"), indent=1)
