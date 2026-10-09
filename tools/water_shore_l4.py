"""Phase G L4 on the Coast bench (port 8109): shore-eye BEFORE (band off) / AFTER (band on) / CALM
(amplitude 0 control) captures at one stated pose, the band record sampled over 20 s, raw JSON
into docs/evidence/water_core_g/coast_shore_g_l4.jsonl."""
import sys, time, json
sys.path.insert(0, r"G:/Github/phyxel/tools")
from water_bench import Api, camera_set, camera_get
from water_feel import capture
from pathlib import Path

EV = Path(r"G:/Github/phyxel/docs/evidence/water_core_g"); EV.mkdir(exist_ok=True)
RADIUS = float(sys.argv[1]) if len(sys.argv) > 1 else 48.0
api = Api("http://127.0.0.1:8109"); api.wait_status(900)
log = []
def rec(tag, d):
    print(tag, json.dumps(d)[:700], flush=True)
    log.append({"tag": tag, "t": time.time(), "data": d})

POSE = {"x": 165, "y": 18.5, "z": 664, "yaw": 90, "pitch": -8}
camera_set(api, POSE); time.sleep(10)   # streaming + occupancy settle at the shore
rec("waves", api.debug("water_waves", {}))
rec("shore_off", api.debug("water_shore", {"on": False}))
rec("render_core_before", api.debug("water_render_core", {}))
time.sleep(2)
rec("before", {"png": str(capture(api, EV / "coast_shore_eye_before.png")), "pose": camera_get(api)})
rec("shore_on", api.debug("water_shore", {"on": True, "radius": RADIUS}))
samples = []
for i in range(80):
    time.sleep(0.25)
    s = api.debug("water_shore", {"probe": [165.5, 676.5]})
    samples.append({k: s.get(k) for k in ("runup_m", "runup_peak_m", "swash_columns", "swash_eta_m", "wet", "mass_m3", "exchanged_m3", "step_ms", "field_ms", "substeps", "max_speed", "mean_free_rise_m", "foam_max", "sitings", "walls")} | {"t": round((i + 1) * 0.25, 2), "col": s.get("column")})
rec("samples", {"samples": samples})
rec("status_20s", api.debug("water_shore", {}))
rec("render_core_after", api.debug("water_render_core", {}))
rec("after", {"png": str(capture(api, EV / "coast_shore_eye_after.png")), "pose": camera_get(api)})
# the calm control: amplitude 0 -> no run-up, nothing stays above the still line
rec("calm_set", api.debug("water_waves", {"amplitude": 0.0}))
time.sleep(20)
rec("status_calm", api.debug("water_shore", {"probe": [165.5, 676.5]}))
rec("calm", {"png": str(capture(api, EV / "coast_shore_eye_calm.png")), "pose": camera_get(api)})
rec("waves_restore", api.debug("water_waves", {"amplitude": 0.45}))
time.sleep(8)
camera_set(api, {"x": 165, "y": 40, "z": 650, "yaw": 90, "pitch": -35}); time.sleep(8)
rec("status_elevated", api.debug("water_shore", {}))
rec("elevated", {"png": str(capture(api, EV / "coast_shore_elevated_after.png")), "pose": camera_get(api)})
(EV / "coast_shore_g_l4.jsonl").write_text("\n".join(json.dumps(r) for r in log) + "\n", encoding="utf-8")
print("done", EV)
