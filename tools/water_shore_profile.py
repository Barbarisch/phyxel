"""Profile the live Coast band along x = 165.5 (z 660..703) at one instant, list lateral ring roles,
and capture the normals debug view at the shore-eye pose."""
import sys, time, json
sys.path.insert(0, r"G:/Github/phyxel/tools")
from water_bench import Api, camera_set, camera_get
from water_feel import capture
from pathlib import Path
EV = Path(r"G:/Github/phyxel/docs/evidence/water_core_g")
api = Api("http://127.0.0.1:8109"); api.wait_status(300)
camera_set(api, {"x": 165, "y": 18.5, "z": 664, "yaw": 90, "pitch": -8}); time.sleep(6)
st = api.debug("water_shore", {"on": True})
print("status", {k: st.get(k) for k in ("active", "n", "still", "prescribed", "sponge", "walls", "free", "wet", "runup_m", "step_ms", "sitings", "origin")})
time.sleep(5)
prof = []
for z in range(660, 704):
    c = api.debug("water_shore", {"probe": [165.5, z + 0.5]}).get("column") or {}
    prof.append((z, c.get("bed"), c.get("eta"), c.get("role"), c.get("w"), c.get("foam")))
print("profile x=165.5 (z, bed, eta-16, role, w, foam):")
for z, b, e, r, w, f in prof:
    print(f"  {z:4d} bed {b:7.3f} eta-still {e - 16.0:+7.3f} role {r} w {w:+6.3f} foam {f:5.3f}" if b is not None else f"  {z} none")
print("lateral ring roles (x, z, bed, role):")
for (x, z) in [(126.5, 684.5), (126.5, 690.5), (126.5, 696.5), (126.5, 702.5), (203.5, 684.5), (203.5, 696.5), (165.5, 702.5), (140.5, 702.5)]:
    c = api.debug("water_shore", {"probe": [x, z]}).get("column") or {}
    print("  ", x, z, c.get("bed"), c.get("role"), c.get("eta"))
api.debug("water_render_core", {"debug": 1}); time.sleep(1.5)
print("normals", capture(api, EV / "coast_shore_eye_after_normals.png"))
api.debug("water_render_core", {"debug": 0})
# an elevated pose INSIDE the band's reach (12 m back from the waterline, 25 m up, looking down-north)
camera_set(api, {"x": 165, "y": 40, "z": 650, "yaw": 90, "pitch": -35}); time.sleep(8)
st = api.debug("water_shore", {})
print("elevated status", {k: st.get(k) for k in ("active", "sitings", "wet", "step_ms")})
print("elevated", capture(api, EV / "coast_shore_elevated_after.png"), camera_get(api))
