"""F2.12 pixel check: 1/8 m store alone (today hidden), wind 0, GI probes off. Per pose: no caster culling, again
(control), per-chunk caster test (shadow_cull), + per-cluster light-frustum multidraw (shadow_mdi). Diffs vs the first.
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
    out = subprocess.check_output([PY, os.path.join(HERE, "imgdiff.py"), a, b], text=True)
    return out.strip()


poses = json.load(open(sys.argv[1]))
tag = sys.argv[2]
cfg = {"hide_world": True, "show_preview": True, "chunk_cull": True, "occlusion": True, "mdi": True}
arms = (("none", False, False), ("none2", False, False), ("chunk", True, False), ("cluster", True, True))
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
gi0 = call("POST", "/api/debug/gi", {}).get("enabled", True)
call("POST", "/api/debug/gi", {"enabled": False})
try:
    for name, (x, y, z, yaw, pitch) in poses.items():
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        call("POST", "/api/debug/voxfound_preview", dict(cfg, shadow_cull=False, shadow_mdi=False))
        time.sleep(6.0)
        shots = {}
        for label, sc, sm in arms:
            call("POST", "/api/debug/voxfound_preview", dict(cfg, shadow_cull=sc, shadow_mdi=sm))
            time.sleep(2.0)
            shots[label] = shot(f"f212_{tag}_{name}_{label}.png")
            st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            keys = ("shadow_faces_mid", "shadow_faces_near", "shadow_sprigs_mid", "shadow_ranges_mid", "shadow_ranges_near",
                    "shadow_clusters_mid", "shadow_clusters_near", "shadow_cluster_faces_mid", "shadow_cluster_faces_near")
            print(f"{name} {label}: " + ", ".join(f"{k[7:]} {st.get(k)}" for k in keys))
        print("   control none/none2:", diff(shots["none"], shots["none2"]))
        print("   none vs chunk:     ", diff(shots["none"], shots["chunk"]))
        print("   none vs cluster:   ", diff(shots["none"], shots["cluster"]))
finally:
    call("POST", "/api/debug/foliage", {"windStrength": 0.05})
    call("POST", "/api/debug/gi", {"enabled": gi0})
    call("POST", "/api/debug/voxfound_preview", {"shadow_cull": True, "shadow_mdi": True})
