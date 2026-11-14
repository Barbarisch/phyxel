"""F2.14: main-view faces drawn per level at each pose, LOD off / P = 1, 2, 4 (MDI path, read back from the cull)."""
import json, sys, time, urllib.request

API = "http://127.0.0.1:8120"


def call(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read().decode())


poses = json.load(open(sys.argv[1]))
for name, (x, y, z, yaw, pitch) in poses.items():
    call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
    row = []
    for P in (None, 1, 2, 4):
        call("POST", "/api/debug/voxfound_preview", {"lod_auto": P is not None, **({"lod_px": P} if P else {})})
        time.sleep(1.0)
        st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
        row.append(f"{'off' if P is None else 'P' + str(P)}: {st['cluster_faces_drawn']} {st['faces_by_level']}")
    print(name, " | ".join(row), flush=True)
call("POST", "/api/debug/voxfound_preview", {"lod_auto": False})
