"""Which setting moves the B-side sprig cost at forest_floor_open: wind and GI, one at a time, with a repeat of the
first config as a control. GPU scope medians over 240 frames."""
import json, time, urllib.request

API = "http://127.0.0.1:8120"


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=600) as resp:
        return json.loads(resp.read().decode())


call("POST", "/api/debug/voxfound_preview", {"hide_world": True, "show_preview": True})
call("POST", "/api/camera", {"mode": "free", "position": {"x": 8200, "y": 29.5, "z": 150}, "yaw": -90, "pitch": -5})
for label, wind, gi in (("default", 0.05, True), ("wind0", 0.0, True), ("gi_off", 0.05, False),
                        ("wind0_gi_off", 0.0, False), ("default_again", 0.05, True)):
    call("POST", "/api/debug/foliage", {"windStrength": wind})
    call("POST", "/api/debug/gi", {"enabled": gi})
    time.sleep(6)
    g = call("GET", "/api/debug/gpu_timing?frames=240")
    sc = {s["key"]: round(s["median_ms"], 2) for s in g["scopes"]}
    print(label, "frame", round(g["gpu_frame_ms"]["median_ms"], 2), "sprigs", sc.get("Scene Pass/VoxFound Sprigs"),
          "faces", sc.get("Scene Pass/VoxFound Preview"), "gi", sc.get("GI Probes"), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.05})
call("POST", "/api/debug/gi", {"enabled": True})
call("POST", "/api/debug/voxfound_preview", {"hide_world": False, "show_preview": False})
