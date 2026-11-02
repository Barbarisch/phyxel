"""WaterCore 50.6d live: after the pressure fix, does the 60 x 60 m lake settle with RECORDING ON (50.6a: ~40 % of runs ran away,
awake 3,500-3,800 by 90 s)? Shipped settings otherwise (fixed tick, tall cells, partial wake). Per run: a stone at the centre once the
lake has settled; awake / frozen / recorded tiles / asleep every second for 90 s; the whole frame awake (0-8 s) and late (60-68 s);
the books. Order: rec ON x3, OFF, rec ON x3, OFF."""
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


def books(c): return (c or {}).get("mass_m3", 0) + (c or {}).get("record_m3", 0)


def run(record):
    destroy_all()
    box = {"x1": X0, "y1": TOP, "z1": Z0, "x2": X0 + L - 1, "y2": TOP + 2, "z2": Z0 + L - 1}
    rec0 = api.debug("water_av_record_tiles", {}).get("default", False)
    api.debug("water_av_record_tiles", {"default": record})
    r = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False})
    api.debug("water_av_record_tiles", {"default": rec0})
    vid = r["volume"]["id"]
    for k in range(2): api.debug("place_water_box", {"target": "core", **box, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
    t0 = time.time()
    while time.time() - t0 < 60:
        v = vol(vid)
        if (record and v.get("recorded_tiles", 0) >= 64) or (not record and v.get("asleep")): break
        time.sleep(0.5)
    settled = round(time.time() - t0, 1)
    c0 = api.debug("water_av_census", {"id": vid})
    api.debug("water_av_impulse", {"x": X0 + L / 2, "y": TOP + 1.9, "z": Z0 + L / 2, "radius": 0.5, "strength": 4.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    t1 = time.time()
    w_awake = window(api, 8.0, f"rec{int(record)} awake 0-8s")
    rows = []
    w_late = None
    while time.time() - t1 < 90:
        if w_late is None and time.time() - t1 >= 60: w_late = window(api, 8.0, f"rec{int(record)} late 60-68s"); continue
        v = vol(vid)
        rows.append({"t": round(time.time() - t1, 1), "awake": v.get("awake_columns"), "frozen": v.get("frozen_columns"), "moving": v.get("moving_columns"),
                     "tiles": v.get("recorded_tiles"), "asleep": v.get("asleep"), "wakes": v.get("tiles_woken"), "ke": v.get("kinetic_energy")})
        time.sleep(1.0)
    c1 = api.debug("water_av_census", {"id": vid})
    last = rows[-1]
    quiet_at = next((r["t"] for r in rows if (r["awake"] or 0) == 0 or r["asleep"]), None)
    print(f"REC {int(record)} settled before {settled} s | after the stone: awake 10 s {next((r['awake'] for r in rows if r['t'] >= 10), None)}, "
          f"30 s {next((r['awake'] for r in rows if r['t'] >= 30), None)}, end {last['awake']}; first all-frozen/asleep {quiet_at} s; tiles end {last['tiles']} wakes {last['wakes']} | "
          f"books {books(c0):.4f} -> {books(c1):.4f} (in solid {c1.get('in_solid_m3')})", flush=True)
    return {"record": record, "settled_s": settled, "census_before": c0, "census_after": c1, "rows": rows, "awake_window": w_awake, "late_window": w_late}


res = []
for rec in (True, True, True, False, True, True, True, False):
    res.append(run(rec))
destroy_all()
pathlib.Path(r"G:/Github/phyxel/docs/evidence/water_core_m4/w50_6d_live.json").write_text(json.dumps(res, indent=1))
print("written", flush=True)
