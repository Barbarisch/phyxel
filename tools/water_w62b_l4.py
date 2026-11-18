"""WaterCore.md 55.5 (W6.2b) L4 on Coast (port 8109, `phyxel up` the bench first): the band parked (test viewer) at the shore pose; stills with the sea's drawn swell ON / OFF (water_sea_draw) at
eye height and from 30 m over the band's seaward edge; whole-frame ABBA (engine_timing) drawn swell on / off at the L4 shore pose."""
import json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
OUT = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4" / "w62b_l4"
OUT.mkdir(exist_ok=True)
ff = shutil.which("ffmpeg")
CROP = "954:560:243:60"
api = Api("http://127.0.0.1:8109"); api.wait_status(900)
def follow(b=None): return api.post("/api/debug/water_av_shore_follow", b or {})
def sea(b=None): return api.post("/api/debug/water_sea_draw", b or {})
def grab(name):
    raw = OUT / f"{name}.raw.png"
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", "title=WaterBench_Coast", "-frames:v", "1", str(raw)], check=True)
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", str(raw), "-vf", f"crop={CROP}", str(OUT / f"{name}.png")], check=True)
    raw.unlink(missing_ok=True)
def wait_band(s=25):
    t0 = time.time()
    while time.time() - t0 < s:
        st = follow()
        if st.get("band_id"): return st
        time.sleep(0.5)
    return follow()
res = {}
api.debug("water_av_realtime", {"on": True})
V = [160.0, 18.5, 684.0]
api.debug("water_locality", {"test_viewer": V})
res["sea_default"] = sea()
camera_set(api, {"x": V[0], "y": V[1], "z": V[2], "yaw": 90.0, "pitch": -8.0})
follow({"on": True}); st = wait_band(); time.sleep(6.0); st = follow()
res["band"] = {k: st.get(k) for k in ("band_id", "min", "max", "swell_columns", "makes", "last_error")}
print(res["band"])
mn, mx = st["min"], st["max"]; cx = 0.5 * (mn[0] + mx[0])
poses = {"eye": {"x": V[0], "y": V[1], "z": V[2], "yaw": 90.0, "pitch": -8.0},
         "eye_edge": {"x": cx, "y": 18.0, "z": mx[2] - 4.0, "yaw": 90.0, "pitch": -6.0},
         "high": {"x": cx, "y": 46.0, "z": mn[2] - 6.0, "yaw": 90.0, "pitch": -45.0}}
for on in (True, False):
    sea({"on": on}); time.sleep(1.0)
    for n, p in poses.items():
        camera_set(api, p); time.sleep(2.0); grab(f"{n}_{'on' if on else 'off'}")
sea({"on": True})
# ABBA at the L4 shore pose (55.4's: x 163 z 668 y 22)
camera_set(api, {"x": 163.0, "y": 22.0, "z": 668.0, "yaw": 90.0, "pitch": -10.0}); time.sleep(4.0)
abba = []
for on in (True, False, False, True, True, False, False, True):
    sea({"on": on}); time.sleep(3.0)
    s = []
    for k in range(12):
        s.append(api.get("/api/debug/engine_timing")); time.sleep(0.5)
    abba.append({"on": on, "samples": s})
res["abba"] = abba
(OUT / "l4b.json").write_text(json.dumps(res, indent=1))
print("keys", list(abba[0]["samples"][0].keys()))
print("done")
