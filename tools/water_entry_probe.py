"""One stone dropped 3 m into the rested pond: its speed and height every ~15 ms through entry, and the
momentum the exchange handed to the water (water_coupling debris_exchange totals before/after).
Stone 1/3 m: V = 0.037 m^3, buoyancy b -> mass in water-volume units = V / b."""
import json, sys, time
sys.path.insert(0, r"G:/Github/phyxel/tools")
from water_bench import Api
api = Api(sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8111"); api.wait_status(900)
tag = sys.argv[2] if len(sys.argv) > 2 else "entry"
for v in api.debug("water_av_list").get("volumes", []):
    api.debug("water_av_destroy", {"id": v["id"]})
api.debug("water_av_create", {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16, "cellSize": 1.0 / 3.0, "transport": "eulerian", "backend": "auto", "auto_sleep": False})
api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
api.debug("water_av_realtime", {"on": True}); api.debug("water_coupling", {"enabled": True, "exchange": True})
time.sleep(10)
ex0 = api.debug("water_coupling", {})["debris_exchange"]
api.debug("settle_probe", {"op": "start"})
api.debug("spawn_gpu_particle", {"x": 101.5, "y": 19.5, "z": 13.5, "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 30.0})
t0 = time.time(); rows = []
while time.time() - t0 < 1.6:
    st = api.debug("settle_probe", {"op": "status", "bodies": 8})
    b = (st.get("awake_bodies") or [None])[0]
    if b: rows.append((round(time.time() - t0, 3), round(b["pos"][1], 3), round(b["vel"][1], 3)))
ex1 = api.debug("water_coupling", {})["debris_exchange"]
api.debug("settle_probe", {"op": "stop"})
surf = 16.5
entry = next((r for r in rows if r[1] - 1.0 / 6.0 <= surf), None)
deep = next((r for r in rows if r[1] <= surf - 1.5 + 1.0 / 6.0), None)
print("samples", len(rows))
for r in rows[::2]: print("  t %.3f  y %.3f  vy %.3f" % r)
print("entry (bottom touches 16.5):", entry, " near floor:", deep)
mom = ex1["momentum_to_water_total"] - ex0["momentum_to_water_total"]
print("momentum to water during the drop (|.| summed, water-volume units m^3 m/s): %.4f; records %d" % (mom, ex1["records"] - ex0["records"]))
print("stone entry momentum ~ V/b * v = 0.037/b * |vy at entry|")
json.dump({"rows": rows, "ex0": ex0, "ex1": ex1}, open(rf"G:/Github/phyxel/docs/evidence/water_core_e/{tag}.json", "w"))
