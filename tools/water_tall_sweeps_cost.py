import sys, time, json
sys.path.insert(0, "tools")
from water_bench import Api
import water_tall_l4 as tt

api = Api("http://127.0.0.1:8114"); api.wait_status(300); api.debug("water_av_realtime", {"on": True})
out = []
for depth in (3.0, 2.0):
    for sw in (20, 10, 5, 0):
        api.debug("water_av_tall", {"sweeps": sw})
        for band in ((0.0, 0.5) if sw == 20 else (0.5,)):
            tt.destroy_all(api)
            hv = int(depth) + 1
            box = {"x1": tt.X0, "y1": tt.TOP, "z1": tt.Z0, "x2": tt.X0 + tt.L - 1, "y2": tt.TOP + hv - 1, "z2": tt.Z0 + tt.L - 1}
            vid = api.debug("water_av_create", {**box, "cellSize": tt.H, "backend": "gpu", "auto_sleep": False, "tall_band_m": band})["volume"]["id"]
            for k in range(int(depth)): api.debug("place_water_box", {"target": "core", **box, "y1": tt.TOP + k, "y2": tt.TOP + k, "mass": 1.0})
            t0 = time.time()
            while time.time() - t0 < 40 and band > 0 and not tt.vol(api, vid).get("tall_active"): time.sleep(0.5)
            time.sleep(2)
            api.debug("water_av_impulse", {"x": tt.X0 + tt.L / 2, "y": tt.TOP + depth - 0.1, "z": tt.Z0 + tt.L / 2, "radius": 1 / 3, "strength": 4.0, "dx": 0, "dy": -1, "dz": 0})
            time.sleep(1.0)
            api.debug("water_av_kernel_timing", {"on": True, "reset": True}); time.sleep(3)
            kt = api.debug("water_av_kernel_timing", {"on": False}); v = tt.vol(api, vid)
            row = {"depth": depth, "band": band, "tall_sweeps": sw, "total": kt["total_ms_per_call"], "sweeps": kt["ms_per_call"].get("sweeps"), "tall": v.get("tall_active"),
                   "ke": v.get("kinetic_energy"), "mass": v.get("mass")}
            out.append(row); print(row, flush=True)
api.debug("water_av_tall", {"sweeps": 20})
tt.destroy_all(api)
json.dump(out, open("docs/evidence/water_core_h1/tall_sweeps_cost.json", "w"), indent=1)
