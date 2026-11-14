"""F2.13: build the gate region preview with pyramid levels 0..3 (4-block clusters), today's world hidden, level 0 drawn.
Prints faces and solid volume per level."""
import json, time, urllib.request

API = "http://127.0.0.1:8120"


def call(method, path, body=None, timeout=3600):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


body = {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "cluster_blocks": 4, "lod_levels": 3, "lod_sprig_level": int(__import__("sys").argv[1]) if len(__import__("sys").argv) > 1 else 0,
        "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
t = time.time()
r = call("POST", "/api/debug/voxfound_preview", body)
r.pop("palette", None)
for k in ("faces", "lod_levels", "lod_faces", "lod_solid_volume_vs_l0", "build_wall_ms", "lod_sprig_level", "lod_leaf_mass_faces", "gpu_face_build_ms_sum",
          "error"):
    print(k, r.get(k))
print(f"wall {time.time() - t:.1f}s")
print(call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True}))

