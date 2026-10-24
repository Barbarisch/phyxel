"""WaterCore.md 27.22 TC5 - tall cells live (L4): the cost, off vs on INTERLEAVED on the same water (separate runs drift ~0.2 ms).

WaterBench_Creek (port 8114, Release, GPU idle). One walled 24 x 24 m pond at x 4-27, z 2-25 (inside the chunk at x 0-31, z 0-31),
1 m and 2 m deep, the engine's defaults (partial wake on, 20 sweeps, quiet scale 3 when measured; 10 since 27.24), auto-sleep off. Per run: the volume created with
tall_band_m = 0 (off) or B; the water placed; it sleeps (the rest level is read then); a stone-sized impulse at the centre; 6 s
polled: the solver's GPU ms (gpu_device_ms), substeps, bricks, the tall echo (tall_active, tall_floor_y, tall_columns, tall_reason).
Order: off, on, off, on per depth. CONTROL: on with B larger than the depth - must cost what off costs (no column under the floor).
Prediction (written in 27.22a before the run): 2 m 2.4 -> 1.6 ms (+-0.3), 1 m 1.5 -> 1.3 ms.

    python tools/water_tall_l4.py [--band 0.5] [--json out.json]
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
H, TOP = 1.0 / 9.0, 16
X0, Z0, L = 4, 2, 24


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})


def vol(api, vid):
    return next(v for v in api.debug("water_av_list")["volumes"] if v["id"] == vid)


def run(api, depth, band, seconds):
    destroy_all(api)
    hv = int(depth) + 1
    box = {"x1": X0, "y1": TOP, "z1": Z0, "x2": X0 + L - 1, "y2": TOP + hv - 1, "z2": Z0 + L - 1}
    r = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False, "tall_band_m": band})
    if "error" in r: raise SystemExit(r["error"])
    vid = r["volume"]["id"]
    full, frac = int(depth), depth - int(depth)
    for k in range(full): api.debug("place_water_box", {"target": "core", **box, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
    if frac > 1e-6: api.debug("place_water_box", {"target": "core", **box, "y1": TOP + full, "y2": TOP + full, "mass": frac})
    want = L * L * depth
    t0 = time.time()   # the placed water reaches the GPU at the next call: wait for it, then for real rest (an empty volume reads asleep)
    while time.time() - t0 < 30 and vol(api, vid)["mass"] < 0.99 * want: time.sleep(0.25)
    while time.time() - t0 < 60 and not vol(api, vid).get("asleep"): time.sleep(0.5)
    m0 = vol(api, vid)["mass"]
    time.sleep(0.5)
    api.debug("water_av_impulse", {"x": X0 + L / 2, "y": TOP + depth - 0.1, "z": Z0 + L / 2, "radius": 1 / 3, "strength": 4.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    rows, t1 = [], time.time()
    while time.time() - t1 < seconds:
        v = vol(api, vid)
        rows.append({"ms": v.get("gpu_device_ms"), "asleep": v.get("asleep"), "substeps": v["last"]["substeps"], "bricks": v.get("gpu_bricks"),
                     "tall_active": v.get("tall_active"), "tall_columns": v.get("tall_columns"), "floor": v.get("tall_floor_y"), "reason": v.get("tall_reason"), "offs": v.get("tall_offs"), "last_off": v.get("tall_last_off"),
                     "mass": v["mass"], "ke": v.get("kinetic_energy")})
        time.sleep(0.25)
    awake = [x["ms"] for x in rows if not x["asleep"] and x["ms"]]
    last = rows[-1]
    out = {"depth_m": depth, "band_m": band, "solver_ms_median": statistics.median(awake) if awake else None, "solver_ms_max": max(awake) if awake else None,
           "substeps_max": max(x["substeps"] for x in rows), "bricks_median": statistics.median(x["bricks"] or 0 for x in rows),
           "tall_active_any": any(x["tall_active"] for x in rows), "tall_columns": max(x["tall_columns"] or 0 for x in rows), "floor_y": last["floor"],
           "reasons": sorted({x["reason"] for x in rows if x["reason"]}), "offs": rows[-1]["offs"], "last_off": rows[-1]["last_off"], "mass_start": m0, "mass_end": last["mass"], "ke_max": max((x["ke"] or 0) for x in rows), "polls": rows}
    print(f"  {depth} m, band {band}: solver {out['solver_ms_median']} ms median (max {out['solver_ms_max']}), bricks {out['bricks_median']}, substeps {out['substeps_max']}, "
          f"tall {out['tall_active_any']} ({out['tall_columns']} columns, floor {out['floor_y']}), reasons {out['reasons']}, offs {out['offs']} ({out['last_off']}), mass {m0:.4f} -> {last['mass']:.4f}, KE max {out['ke_max']:.3e}", flush=True)
    destroy_all(api)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--band", type=float, default=0.5)
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--depths", default="1,2,3")
    ap.add_argument("--json", default=str(EV / "tall_l4.json"))
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    api.post("/api/camera", {"mode": "free", "position": {"x": 16.0, "y": 30.0, "z": 40.0}, "yaw": -90.0, "pitch": -40.0})
    time.sleep(2.0)
    res = {"runs": [], "args": vars(a)}
    for depth in [float(x) for x in a.depths.split(",")]:
        for band in (0.0, a.band, 0.0, a.band):
            res["runs"].append(run(api, depth, band, a.seconds))
        res["runs"].append({"control": True, **run(api, depth, depth + 1.0, a.seconds)})
        pathlib.Path(a.json).write_text(json.dumps(res, indent=1))
    print("wrote", a.json)


if __name__ == "__main__":
    main()
