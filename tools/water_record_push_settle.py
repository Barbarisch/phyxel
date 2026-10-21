"""WaterCore.md 27.13: does a pushed pond with recording settle and go back into the record? The 27.12 push on the creek bench's
pond (46 x 28 m, 1 m deep, port 8114, a strength-4 push at (12, 20)), with any water_av_record_tiles switches.
Example: python tools/water_record_push_settle.py '{"dur": 60}' shipped ; '{"after_step_test": true}' red ; '{"rec": false}' OFF. argv[1] = JSON of the global switches (plus "rec":
true/false for the volume, "after_s"), argv[2] = a label. KE / moving / residual / tiles every 3 s for 28 s; appends to a JSONL."""
import sys, time, json
sys.path.insert(0, r"G:\Github\phyxel\tools")
from water_bench import Api
api = Api("http://127.0.0.1:8114"); api.wait_status(300)
api.debug("water_av_realtime", {"on": True})
cfg = json.loads(sys.argv[1]) if len(sys.argv) > 1 else {}
label = sys.argv[2] if len(sys.argv) > 2 else json.dumps(cfg)
rec = cfg.pop("rec", True); after = cfg.pop("after_s", 2.0); DUR = int(cfg.pop("dur", 28))
base = {"after_step_test": False, "settle_after_test": False}
api.debug("water_av_record_tiles", {**base, **cfg})
for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
box = {"x1": 2, "y1": 16, "z1": 0, "x2": 47, "y2": 17, "z2": 27}
vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
api.debug("water_av_record_tiles", {"id": vid, "on": rec, "after_s": after})
api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
time.sleep(4.0)
api.debug("water_av_impulse", {"x": 12.0, "y": 16.9, "z": 20.0, "radius": 0.5, "strength": 4.0, "dy": -1.0})
t0 = time.time(); samples = []
for s in range(1, DUR + 1, 3 if DUR <= 30 else 10):
    while time.time() - t0 < s: time.sleep(0.02)
    v = next(x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid)
    samples.append({"t": s, "ke": v.get("kinetic_energy"), "moving": v.get("moving_columns"), "res": v.get("rbgs_residual"),
                    "rec": v.get("recorded_tiles"), "recorded": v.get("tiles_recorded"), "woken": v.get("tiles_woken")})
    print(f"  {label:28s} {s:2d}s KE {samples[-1]['ke']:.2e} mv {samples[-1]['moving']:5d} res {samples[-1]['res']:.3g} rec {samples[-1]['rec']:3d} woken {samples[-1]['woken']}", flush=True)
c = api.debug("water_av_census", {"id": vid})
print(f"  {label:28s} sentinel {c.get('sentinel_max_fuvw')} census mass {c.get('mass_m3'):.4f} + record {c.get('record_m3'):.4f} = {c.get('mass_m3') + c.get('record_m3'):.4f}", flush=True)
api.debug("water_av_record_tiles", base)
with open(r"G:\Github\phyxel\docs\evidence\water_core_h1\w1c_bisect.jsonl", "a") as f:
    f.write(json.dumps({"label": label, "cfg": cfg, "rec": rec, "samples": samples, "census": c}) + "\n")
