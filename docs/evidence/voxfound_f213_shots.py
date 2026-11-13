"""F2.13 owner-sees-it + pixel check: the whole region drawn at each pyramid level (lod_draw 0..3), same pose, wind 0,
GI probes off, today's world hidden. Per pose: L0, L0 again (control), L1, L2, L3. Viewport-box diffs vs L0 (the
status bar changes per frame). Run from the repo root. Usage: f213_shots.py poses.json"""
import json, os, shutil, subprocess, sys, time, urllib.request

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))


def call(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read().decode())


def shot(name):
    dst = os.path.join(HERE, name)
    shutil.copy(call("GET", "/api/screenshot")["path"], dst)
    return dst


def diff(a, b):
    return subprocess.check_output([sys.executable, os.path.join(HERE, "imgdiff.py"), a, b, "45", "670", "242", "1196"],
                                   text=True).strip()


poses = json.load(open(sys.argv[1]))
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
try:
    for name in ("overview_south", "forest_floor_open", "canopy_close"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        call("POST", "/api/debug/voxfound_preview", {"lod_draw": 0})
        time.sleep(6.0)
        s = {}
        for label, k in (("L0", 0), ("L0b", 0), ("L1", 1), ("L2", 2), ("L3", 3)):
            r = call("POST", "/api/debug/voxfound_preview", {"lod_draw": k})
            time.sleep(2.5)
            s[label] = shot(f"f213_{name}_{label}.png")
            st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            print(f"{name} {label}: faces {r.get('faces')}, drawn clusters {st.get('clusters_drawn')}"
                  f" faces {st.get('cluster_faces_drawn')}")
        print(f"   {name} control L0/L0b:", diff(s["L0"], s["L0b"]))
        for label in ("L1", "L2", "L3"):
            print(f"   {name} L0 vs {label}:", diff(s["L0"], s[label]))
finally:
    call("POST", "/api/debug/voxfound_preview", {"lod_draw": 0})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    call("POST", "/api/debug/gi", {"enabled": True})
