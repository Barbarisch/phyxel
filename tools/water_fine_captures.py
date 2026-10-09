"""WaterCore G4 (docs/WaterCore.md 18.8) captures on the Coast bench: the shore-eye and elevated poses
with the 1 m band alone, then with the 1/3 m fine band nested, plus the foam/flow tap (debug 6) of
each, and the band records beside them. Same poses, same swell; one variable (fine on/off)."""
import json, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set, camera_get
from water_feel import capture

EV = Path(__file__).resolve().parents[1] / "docs" / "evidence" / "water_core_g"
POSES = {"eye": {"x": 165, "y": 18.5, "z": 664, "yaw": 90, "pitch": -8},
         "elevated": {"x": 165, "y": 40, "z": 650, "yaw": 90, "pitch": -35}}
api = Api("http://127.0.0.1:8109"); api.wait_status(900)
out = {}
for fine in (False, True):
    tag = "fine" if fine else "coarse"
    for pose_name, pose in POSES.items():
        camera_set(api, pose); time.sleep(4)
        api.debug("water_shore", {"on": True, "radius": 48, "fine": fine, "resite": True})
        time.sleep(10)
        st = api.debug("water_shore", {})
        api.debug("water_render_core", {"debug": 0}); time.sleep(1.0)
        look = capture(api, EV / f"g4_{pose_name}_{tag}.png")
        api.debug("water_render_core", {"debug": 6}); time.sleep(1.0)
        tap = capture(api, EV / f"g4_{pose_name}_{tag}_foamtap.png")
        api.debug("water_render_core", {"debug": 0})
        f = st.get("fine") or {}
        out[f"{pose_name}_{tag}"] = {"pose": camera_get(api), "look": look, "tap": tap,
                                     "band": {k: st.get(k) for k in ("wet", "foam_max", "step_ms", "edge_columns", "edge_foam_columns", "hidden", "centre")},
                                     "fine": {k: f.get(k) for k in ("active", "box", "wet", "foam_max", "step_ms", "edge_columns", "edge_foam_columns")}}
        print(pose_name, tag, json.dumps(out[f"{pose_name}_{tag}"])[:400], flush=True)
(EV / "g4_captures.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
