"""F2.14e control: the SAME path (lod_gpu false) built twice -> px apart at overview_south P=8 (build-to-build noise)."""
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
def shot(name, settle=5.0):
    time.sleep(settle)
    dst = os.path.join(HERE, name)
    shutil.copy(call("GET", "/api/screenshot")["path"], dst)
    return np.asarray(Image.open(dst).convert("RGB")).astype(np.int16)[BOX[0]:BOX[1], BOX[2]:BOX[3]]
poses = json.load(open(sys.argv[1]))
gpu = sys.argv[2] != "0"
call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
imgs = []
for i in range(2):
    call("POST", "/api/debug/voxfound_preview", {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False,
         "cluster_blocks": 4, "lod_levels": 7, "lod_gpu": gpu, "region": {"from": [254, 0, 2], "to": [257, 1, 5]}})
    call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True})
    x, y, z, yaw, pitch = poses["overview_south"]
    call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
    call("POST", "/api/debug/voxfound_preview", {"lod_auto": True, "lod_px": 8.0, "lod_max": 7})
    imgs.append(shot(f"f214e_rebuild_{'gpu' if gpu else 'cpu'}_{i}.png"))
d = np.any(np.abs(imgs[0] - imgs[1]) > 0, axis=2)
print("lod_gpu", gpu, "build twice, overview_south P=8: px apart", int(d.sum()))
call("POST", "/api/debug/voxfound_preview", {"lod_auto": False})
call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
