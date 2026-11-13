"""F2.11 pixel check: 1/8 m store alone (today hidden), wind 0, GI probes off. Per pose: MDI off (per-chunk direct
draws), off again (control), MDI on (GPU cull + one indirect multidraw). Diffs vs the first shot; prints cull stats.
Run from the repo root (screenshot paths are relative to the engine's cwd). Usage: f211_pix.py poses.json tag"""
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
    out = subprocess.check_output([PY, os.path.join(HERE, "imgdiff.py"), a, b, "45", "670", "242", "1196"], text=True)
    return out.strip()


poses = json.load(open(sys.argv[1]))
tag = sys.argv[2]
cfg = {"hide_world": True, "show_preview": True, "chunk_cull": True, "occlusion": True}
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
gi0 = call("POST", "/api/debug/gi", {}).get("enabled", True)
call("POST", "/api/debug/gi", {"enabled": False})
try:
    for name, (x, y, z, yaw, pitch) in poses.items():
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        call("POST", "/api/debug/voxfound_preview", dict(cfg, mdi=False))
        time.sleep(6.0)
        shots = {}
        for label, mdi in (("off", False), ("off2", False), ("on", True)):
            call("POST", "/api/debug/voxfound_preview", dict(cfg, mdi=mdi))
            time.sleep(2.0)
            shots[label] = shot(f"f211_{tag}_{name}_{label}.png")
            st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            if label == "off":
                off_faces = st.get("faces_drawn")
        print(f"{name}: direct path faces {off_faces}; MDI clusters {st.get('clusters')} drawn {st.get('clusters_drawn')}, "
              f"faces {st.get('cluster_faces_drawn')} (cluster_blocks {st.get('cluster_blocks')})")
        print("   control off/off2:", diff(shots["off"], shots["off2"]))
        print("   off vs on:      ", diff(shots["off"], shots["on"]))
finally:
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    call("POST", "/api/debug/gi", {"enabled": gi0})
    call("POST", "/api/debug/voxfound_preview", {"mdi": True})
