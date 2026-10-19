"""Debris self-shadow probe. Pose + rig fixed; reads shadow-only view (mode 13, tonemap curve 0).
Usage: python tools/debris_self_shadow_probe.py <tag> [--spawn]   (project DebrisPushTest; PHYXEL_API_URL to override port)
Red/green record: docs/evidence/dynamic_shadow/ and docs/LightingPipeline.md section 9, 2026-10-19."""
import json, os, shutil, sys, time, urllib.request
from PIL import Image
B = os.environ.get("PHYXEL_API_URL", "http://localhost:8090")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs/evidence/dynamic_shadow")
def call(m, p, body=None):
    d = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(B + p, data=d, method=m, headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(r, timeout=120).read())
tag = sys.argv[1]
if "--spawn" in sys.argv:
    call("POST", "/api/world/fill", {"x1":18,"y1":17,"z1":24,"x2":19,"y2":18,"z2":25,"material":"Stone"})
    print("aligned", call("POST", "/api/debug/spawn_gpu_lattice", {"x":21.5,"y":17.6,"z":24.5,"nx":2,"ny":2,"nz":2,"scale":1.0,"gap":0.02,"material":"Stone","lifetime":3600}))
    print("tumbled", call("POST", "/api/debug/spawn_gpu_lattice", {"x":25.5,"y":19.5,"z":24.5,"nx":2,"ny":2,"nz":2,"scale":1.0,"gap":0.3,"spin":3.0,"material":"Stone","lifetime":3600,"seed":7}))
    print("subcube", call("POST", "/api/debug/spawn_gpu_lattice", {"x":29.5,"y":18.5,"z":24.5,"nx":4,"ny":2,"nz":4,"scale":0.3333,"gap":0.05,"spin":2.0,"material":"Stone","lifetime":3600,"seed":3}))
    time.sleep(6)
pose = {"mode":"free","position":{"x":24.5,"y":27,"z":19.5},"yaw":90,"pitch":-55}
call("POST", "/api/camera", pose)
res = {"tag": tag, "pose": pose, "dynamic_stats": call("GET", "/api/debug/dynamic_stats")}
for mode, curve in ((13, 0), (0, 1)):
    call("POST", "/api/debug/tonemap", {"curve": curve})
    call("POST", "/api/debug/shadow", {"mode": mode})
    time.sleep(2)
    p = call("GET", "/api/screenshot")["path"]
    dst = os.path.join(OUT, f"{tag}_mode{mode}.png")
    shutil.copy(os.path.join(ROOT, p), dst)
    if mode == 13:
        im = Image.open(dst).convert("L")
        # boxes in full-frame px, inside each TOP face at this pose (see *_mode13.png)
        boxes = {"static_top_CONTROL": (1015, 315, 1150, 370), "aligned_debris_top": (790, 290, 895, 360)}
        for k, b in boxes.items():
            px = list(im.crop(b).getdata())
            res[k] = {"box": b, "mean_luma": round(sum(px)/len(px), 1),
                      "frac_black(<20)": round(sum(v < 20 for v in px)/len(px), 3)}
call("POST", "/api/debug/shadow", {"mode": 0})
call("POST", "/api/debug/tonemap", {"curve": 1})
json.dump(res, open(os.path.join(OUT, f"{tag}.json"), "w"), indent=1)
print(json.dumps(res, indent=1))
