"""WaterCore 50.6g live: the 60 x 60 m lake after a VISIBLE push (2 m / strength 8), shipped settings (fixed tick, tall band, partial
wake, recording OFF), the pressure sweeps 20 (the shipped automatic count) vs 40, interleaved, 3 runs each. Per run: awake columns
every second for 70 s, the time until every column is frozen, the whole frame awake (0-8 s) and at 20-28 s, the books."""
import sys, time, json, pathlib
sys.path.insert(0, r"G:/Github/phyxel/tools")
from water_bench import Api
from water_a3_cost import window

H, TOP, X0, Z0, L = 1.0 / 9.0, 16, 4, 2, 60
api = Api("http://127.0.0.1:8114"); api.wait_status(120)
print(api.debug("water_av_realtime", {"on": True}), api.debug("water_av_fixed_tick", {}), flush=True)


def destroy_all():
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def vol(vid):
    return next(v for v in api.debug("water_av_list")["volumes"] if v["id"] == vid)


def run(sweeps):
    destroy_all()
    box = {"x1": X0, "y1": TOP, "z1": Z0, "x2": X0 + L - 1, "y2": TOP + 2, "z2": Z0 + L - 1}
    r = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False, "sweeps": sweeps})
    vid = r["volume"]["id"]
    got = vol(vid).get("gpu_sweeps")
    for k in range(2): api.debug("place_water_box", {"target": "core", **box, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
    t0 = time.time()
    while time.time() - t0 < 60 and not vol(vid).get("asleep"): time.sleep(0.5)
    c0 = api.debug("water_av_census", {"id": vid})
    api.debug("water_av_impulse", {"x": X0 + L / 2, "y": TOP + 1.9, "z": Z0 + L / 2, "radius": 2.0, "strength": 8.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    t1 = time.time()
    w_awake = window(api, 8.0, f"sweeps {got} awake 0-8s")
    rows = []; w_mid = None
    while time.time() - t1 < 70:
        if w_mid is None and time.time() - t1 >= 20: w_mid = window(api, 8.0, f"sweeps {got} 20-28s"); continue
        v = vol(vid)
        rows.append({"t": round(time.time() - t1, 1), "awake": v.get("awake_columns"), "moving": v.get("moving_columns"), "asleep": v.get("asleep"), "ms": v.get("gpu_device_ms")})
        time.sleep(1.0)
    c1 = api.debug("water_av_census", {"id": vid})
    quiet = next((r["t"] for r in rows if (r["awake"] or 0) == 0 or r["asleep"]), None)
    full_until = max((r["t"] for r in rows if (r["awake"] or 0) >= 4000), default=None)
    def books(c): return (c or {}).get("mass_m3", 0) + (c or {}).get("record_m3", 0)
    print(f"SWEEPS {got}: whole lake awake until {full_until} s, all frozen {quiet} s; awake at 30 s {next((r['awake'] for r in rows if r['t'] >= 30), None)}, "
          f"40 s {next((r['awake'] for r in rows if r['t'] >= 40), None)}; books {books(c0):.4f} -> {books(c1):.4f}", flush=True)
    return {"sweeps": got, "rows": rows, "awake_window": w_awake, "mid_window": w_mid, "census_before": c0, "census_after": c1}


res = [run(s) for s in (20, 40, 20, 40, 20, 40)]
destroy_all()
pathlib.Path(r"G:/Github/phyxel/docs/evidence/water_core_m4/w50_6g_live.json").write_text(json.dumps(res, indent=1))
print("written", flush=True)
