"""F2.14d: build the gate region preview with pyramid levels 0..7 (levels 4-7 from the region pyramid).
Usage: f214d_build.py [lod_levels=7] [flora=1] [sprigLevel=0]"""
import json, sys, time, urllib.request

API = "http://127.0.0.1:8120"


def call(method, path, body=None, timeout=3600):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


a = sys.argv[1:]
levels = int(a[0]) if len(a) > 0 else 7
flora = (a[1] != "0") if len(a) > 1 else True
sprig = int(a[2]) if len(a) > 2 else 0
body = {"type": "Perlin", "seed": 7, "flora": flora, "hide_world": False, "cluster_blocks": 4, "lod_levels": levels,
        "lod_sprig_level": sprig, "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
t = time.time()
r = call("POST", "/api/debug/voxfound_preview", body)
r.pop("palette", None)
for k in ("faces", "lod_levels", "lod_faces", "lod_solid_volume_vs_l0", "build_wall_ms", "region_lod_ms",
          "gpu_face_build_ms_sum", "lod_top_view", "error"):
    print(k, json.dumps(r.get(k)))
print(f"wall {time.time() - t:.1f}s")
print(call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True}))
