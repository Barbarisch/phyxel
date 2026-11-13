"""F2.9 pixel check: 1/8 m store alone (today hidden), wind 0, per pose: cull off, cull off again (control), cull on.
Diffs control and on vs the first off shot. Prints stats (ranges drawn/culled). Restores wind afterwards.
Usage: python f29_pix.py poses.json tag"""
import json, os, shutil, subprocess, sys, time, urllib.request

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable


def call(method, path, body=None, timeout=120):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def shot(name):
    p = call("GET", "/api/screenshot")["path"]
    dst = os.path.join(HERE, name)
    shutil.copy(p, dst)
    return dst


def diff(a, b):
    # viewport only: x 242-1196, y 45-670 (the editor UI around it is static)
    out = subprocess.check_output([PY, os.path.join(HERE, "imgdiff.py"), a, b, "45", "670", "242", "1196"], text=True)
    return out.strip()


poses = json.load(open(sys.argv[1]))
tag = sys.argv[2]
wind0 = call("POST", "/api/debug/foliage", {}).get("windStrength")
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
gi0 = call("POST", "/api/debug/gi", {}).get("enabled", True)
call("POST", "/api/debug/gi", {"enabled": False})   # stochastic probe re-tracing is per-frame noise
try:
    for name, (x, y, z, yaw, pitch) in poses.items():
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True, "chunk_cull": False})
        time.sleep(12.0)   # let the probe field converge after the camera move
        shots = {}
        for label, cull in (("off", False), ("off2", False), ("on", True)):
            call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True, "chunk_cull": cull})
            time.sleep(2.0)
            shots[label] = shot(f"f29_{tag}_{name}_{label}.png")
            if label == "on":
                st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
        print(f"{name}: ranges drawn {st.get('ranges_drawn')} culled {st.get('ranges_culled')}, faces {st.get('faces_drawn')}, "
              f"sprigs {st.get('sprigs_drawn')}")
        print("   control off/off2:", diff(shots["off"], shots["off2"]))
        print("   off vs on:      ", diff(shots["off"], shots["on"]))
finally:
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})   # FoliageRenderPipeline default
    call("POST", "/api/debug/gi", {"enabled": gi0})
