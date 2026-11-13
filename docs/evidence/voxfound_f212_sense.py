"""F2.12 sensitivity: can the viewport diff see the preview's face shadows at all? Per pose: casters on, on again
(control), face casters OFF ({"cast":{"faces":false}}). Viewport box only (the status bar text changes per frame)."""
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
call("POST", "/api/debug/voxfound_preview", {"shadow_cull": True, "shadow_mdi": False})
try:
    for name in ("overview_south", "canopy_close"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        time.sleep(6.0)
        s = {}
        for label, faces in (("on", True), ("on2", True), ("off", False)):
            call("POST", "/api/debug/voxfound_preview", {"cast": {"faces": faces, "sprigs": True}})
            time.sleep(2.0)
            s[label] = shot(f"f212_sense_{name}_{label}.png")
        print(name, "control:", diff(s["on"], s["on2"]))
        print(name, "face casters off:", diff(s["on"], s["off"]))
finally:
    call("POST", "/api/debug/voxfound_preview", {"cast": {"faces": True, "sprigs": True}})
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    call("POST", "/api/debug/gi", {"enabled": True})
