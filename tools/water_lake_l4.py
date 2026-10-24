"""WaterCore.md 27.23 W1e - the lake live (L4): a 60 x 60 m lake, 2 m deep, a stone - what a call costs on the device as the stone's
patch wakes, waves, freezes and (recording on) records, with the rest sums cached (27.23 W1e) or not, INTERLEAVED.

WaterBench_Creek (port 8114, Release). The walled box x 4-63, z 2-61, y 16-18 (2 m of water, 1 m of air). The engine's defaults
(partial wake on, tall band 0.5 m, 20 sweeps) except: auto-sleep off; --quiet-scale (the freeze test, default the engine's 3);
--record (tiles into the record after 2 s frozen). Per run: create, place, wait for the water, settle --settle s, the stone at the
centre, poll --seconds s: gpu_device_ms, awake / frozen columns, recorded tiles, bricks, reduce slots / cached, the mass.

    python tools/water_lake_l4.py [--quiet-scale 10] [--record] [--cache both|on|off] [--json out.json]
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

H, TOP, X0, Z0, L, W = 1.0 / 9.0, 16, 4, 2, 60, 60


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})


def vol(api, vid):
    return next(v for v in api.debug("water_av_list")["volumes"] if v["id"] == vid)


def run(api, cache, record, settle, seconds):
    print(api.debug("water_av_reduce_cache", {"on": cache}))
    destroy_all(api)
    box = {"x1": X0, "y1": TOP, "z1": Z0, "x2": X0 + L - 1, "y2": TOP + 2, "z2": Z0 + W - 1}
    rec0 = api.debug("water_av_record_tiles", {}).get("default", False)
    api.debug("water_av_record_tiles", {"default": record})   # applied at create
    # a lake left recorded by the last run stays in the world record (destroying a volume writes its water back): every run but the
    # first starts from it - the lake asleep in the record, as W1e means it - seeded into the new volume
    r = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False, "seed": "spans"})
    api.debug("water_av_record_tiles", {"default": rec0})
    if "error" in r: raise SystemExit("create refused: " + r["error"])
    vid = r["volume"]["id"]
    print("  created:", {k: r["volume"].get(k) for k in ("id", "record_tiles", "partial_wake", "tall_band_m", "cells")})
    seeded = r.get("seeded_mass", 0.0)
    print("  seeded from the record:", round(seeded, 4))
    for k in range(2 if seeded < 1.0 else 0):
        rr = api.debug("place_water_box", {"target": "core", **box, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
        if "error" in rr: raise SystemExit(rr["error"])
    want = L * W * 2.0
    t0 = time.time()
    while time.time() - t0 < 60 and vol(api, vid)["mass"] + vol(api, vid).get("recorded_m3", 0.0) < 0.99 * want: time.sleep(0.5)
    time.sleep(settle)
    v0 = vol(api, vid)
    m0 = v0["mass"] + v0.get("recorded_m3", 0.0)
    api.debug("water_av_impulse", {"x": X0 + L / 2, "y": TOP + 1.9, "z": Z0 + W / 2, "radius": 0.5, "strength": 4.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    rows, t1 = [], time.time()
    while time.time() - t1 < seconds:
        v = vol(api, vid)
        rows.append({"t": round(time.time() - t1, 2), "ms": v.get("gpu_device_ms"), "awake": v.get("awake_columns"), "frozen": v.get("frozen_columns"),
                     "recorded_tiles": v.get("recorded_tiles"), "bricks": v.get("gpu_bricks"), "reduce_slots": v.get("gpu_reduce_slots"),
                     "cached": v.get("gpu_reduce_cached"), "asleep": v.get("asleep"), "mass": v["mass"] + v.get("recorded_m3", 0.0)})
        time.sleep(0.25)
    bins = {}
    for x in rows:
        if x["ms"] is None or x["asleep"]: continue
        bins.setdefault(int(x["t"] // 5), []).append(x["ms"])
    med = {f"{5 * b}-{5 * b + 5}s": round(statistics.median(v), 2) for b, v in sorted(bins.items())}
    allms = [x["ms"] for x in rows if x["ms"] is not None and not x["asleep"]]
    out = {"cache": cache, "record": record, "medians_by_5s": med, "mean_ms": round(statistics.mean(allms), 3) if allms else None,
           "mass_start": m0, "mass_end": rows[-1]["mass"], "last": rows[-1], "rows": rows}
    print(f"  cache {cache} record {record}: mean {out['mean_ms']} ms, medians {med}, last {rows[-1]}, books {m0:.4f} -> {rows[-1]['mass']:.4f}", flush=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8114)
    ap.add_argument("--quiet-scale", type=float, default=None)
    ap.add_argument("--record", action="store_true")
    ap.add_argument("--cache", default="both")
    ap.add_argument("--settle", type=float, default=8.0)
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--no-droplets", action="store_true")
    ap.add_argument("--z0", type=int, default=2)
    ap.add_argument("--width", type=int, default=60)
    ap.add_argument("--json")
    a = ap.parse_args()
    global Z0, W
    Z0, W = a.z0, a.width
    api = Api(f"http://localhost:{a.port}")
    api.debug("water_av_realtime", {"on": True})   # the bench steps volumes only in realtime mode
    drop0 = api.debug("water_droplets", {}).get("enabled", True)
    if a.no_droplets: api.debug("water_droplets", {"enabled": False})
    qs0 = api.debug("water_av_partial_wake", {}).get("quiet_scale")
    if a.quiet_scale is not None: print(api.debug("water_av_partial_wake", {"quiet_scale": a.quiet_scale}))
    order = [True, False, True, False] if a.cache == "both" else [a.cache == "on"]
    res = []
    try:
        for c in order: res.append(run(api, c, a.record, a.settle, a.seconds))
    finally:
        api.debug("water_av_reduce_cache", {"on": True})
        if a.quiet_scale is not None and qs0 is not None: api.debug("water_av_partial_wake", {"quiet_scale": qs0})
        api.debug("water_droplets", {"enabled": drop0})
        destroy_all(api)
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps({"quiet_scale": a.quiet_scale, "record": a.record, "runs": res}, indent=1))


if __name__ == "__main__":
    main()
