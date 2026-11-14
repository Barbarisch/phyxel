"""F2.14f: the multidraw and the direct draws must give the same image (leaf look on/off x handoff on/off, two poses)."""
import json, shutil, time, urllib.request, os
import numpy as np
from PIL import Image
S = os.path.dirname(os.path.abspath(__file__))
API = "http://127.0.0.1:8120"; BOX = (45, 670, 242, 1196)
def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(r, timeout=300).read().decode())
def shot(n, s=5.0):
    time.sleep(s); d = os.path.join(S, n); shutil.copy(call("GET", "/api/screenshot")["path"], d)
    return np.asarray(Image.open(d).convert("RGB")).astype(np.int16)[BOX[0]:BOX[1], BOX[2]:BOX[3]]
poses = json.load(open(os.path.join(S, "f29_poses.json")))
call("POST", "/api/debug/foliage", {"windStrength": 0.0}); call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True})
out = {}
for name in ("top_down", "overview_south"):
    x, y, z, yaw, pitch = poses[name]
    call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
    for look in (False, True):
        for sprig in (False, True):
            call("POST", "/api/debug/voxfound_preview", {"lod_auto": True, "lod_px": 8.0, "lod_sprig": sprig,
                                                         "lod_leaf_look": look, "mdi": True})
            m = shot("f214f_p_m.png")
            call("POST", "/api/debug/voxfound_preview", {"mdi": False})
            d = shot("f214f_p_d.png")
            k = f"{name} look {look} handoff {sprig}"
            out[k] = int(np.any(m != d, axis=2).sum())
            print(k, "mdi vs direct px", out[k], flush=True)
call("POST", "/api/debug/voxfound_preview", {"mdi": True, "lod_auto": False, "lod_leaf_look": True})
call("POST", "/api/debug/foliage", {"windStrength": 0.05}); call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(S, "f214f_paths.json"), "w"), indent=1)
