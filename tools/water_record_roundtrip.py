"""§27 W1 step 0 (2026-10-19): what today's sleep (write-back to the record) and seed:"spans" (wake from it) do on the GPU's
sparse volumes. Measures, changes nothing.

  A  a pond on flat ground (full cubes): created with the engine defaults (auto_sleep ON, the partial wake ON) - does it
     sleep and write back by itself; the books (water before vs the record after); the call's time; then woken from the
     record (seed:"spans") - the books, where the water is (water_av_census), the level against before.
  B  the creek's 1/9 m gravel bed (a 20 m reach, closed box, still water): forced to sleep, woken from the record - water
     inside the ground (the seed has no occupancy test; predicted RED), the books, the level.

    python tools/water_record_roundtrip.py [--tag record_roundtrip]
Writes docs/evidence/water_core_h1/<tag>.json. The bench engine (WaterBench_Creek, 8114) must be running.
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"


def vol(api, vid):
    return next((x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid), None)


def spans(api, r):
    d = api.debug("water_spans_stored", {"x1": r[0], "z1": r[1], "x2": r[2], "z2": r[3]})
    return {k: d.get(k) for k in ("spans", "total_depth", "chunks_with_spans", "chunks_loaded", "min_top", "max_top")}


def levels(api, pts):
    out = []
    for i in range(0, len(pts), 64):
        for c in api.debug("water_av_column", {"points": pts[i:i + 64]})["columns"]:
            out.append(c.get("surface_mean"))
    return out


def timed(api, route, body):
    t0 = time.perf_counter(); r = api.debug(route, body); return r, 1000 * (time.perf_counter() - t0)


def roundtrip(api, name, box, fill_box, wait_auto, h=1 / 9):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    rect = (box["x1"], box["z1"], box["x2"], box["z2"])
    out = {"name": name, "box": box, "spans_before": spans(api, rect)}
    cr = api.debug("water_av_create", {**box, "cellSize": h, "backend": "gpu"})   # the engine defaults: auto_sleep, partial wake
    vid = cr["volume"]["id"]; out["created"] = {k: cr["volume"].get(k) for k in ("cells", "gpu_bricks", "partial_wake", "gpu_sweeps")}
    api.debug("place_water_box", {"target": "core", **fill_box, "mass": 1.0})
    t0 = time.time()
    pts = [[x + 0.5, z + 0.5] for z in range(box["z1"], box["z2"] + 1) for x in range(box["x1"], box["x2"] + 1)]
    c0 = api.debug("water_av_census", {"id": vid}); out["census_placed"] = c0   # at once: a still pond may sleep within ~2 s
    if "error" in c0: c0 = {"mass_m3": float("nan"), "in_solid_m3": float("nan")}; out["census_placed"] = c0
    # does it sleep and write back by itself?
    auto = None
    while time.time() - t0 < wait_auto:
        v = vol(api, vid)
        if v is None: auto = round(time.time() - t0, 1); break
        lv_awake = v
        time.sleep(0.5)
    out["auto_slept_after_s"] = auto
    if auto is None:
        v = vol(api, vid); out["before_sleep"] = {k: v.get(k) for k in ("asleep", "awake_columns", "frozen_columns", "gpu_device_ms", "kinetic_energy")}
        lv0 = levels(api, pts); out["levels_before"] = lv0
        c1 = api.debug("water_av_census", {"id": vid}); out["census_before_sleep"] = c1
        r, ms = timed(api, "water_av_sleep", {"id": vid})
        if not r.get("success"):
            out["sleep_unforced"] = r
            r, ms = timed(api, "water_av_sleep", {"id": vid, "force": True})
        out["sleep"] = r; out["sleep_ms"] = ms
        mass_before = c1["mass_m3"]
    else:
        mass_before = c0["mass_m3"]; lv0 = None
    out["mass_before_sleep_m3"] = mass_before
    out["spans_after_sleep"] = sa = spans(api, rect)
    out["volumes_after_sleep"] = [v["id"] for v in api.debug("water_av_list")["volumes"]]
    # wake from the record
    r, ms = timed(api, "water_av_create", {**box, "cellSize": h, "backend": "gpu", "auto_sleep": False, "seed": "spans"})
    out["wake_ms"] = ms; out["wake"] = {k: r.get(k) for k in ("seeded_mass", "error")}
    if r.get("success"):
        wid = r["volume"]["id"]
        out["census_woken"] = api.debug("water_av_census", {"id": wid})
        time.sleep(5.0)
        out["census_woken_5s"] = api.debug("water_av_census", {"id": wid})
        lv1 = levels(api, pts); out["levels_after_wake_5s"] = lv1
        if lv0:
            d = [b - a for a, b in zip(lv0, lv1) if a is not None and b is not None]
            out["level_change_mm"] = {"max_abs": 1000 * max((abs(x) for x in d), default=0), "mean": 1000 * statistics.mean(d) if d else None, "n": len(d)}
        api.debug("water_av_destroy", {"id": wid})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--tag", default="record_roundtrip")
    ap.add_argument("--only", default="ABC", help="which rigs to run (letters)")
    ap.add_argument("--build-step", action="store_true", help="author rig C's microcube step first (once per world)")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(300)
    if args.build_step:
        items = [{"x": vx, "y": 16, "z": vz, "sx": k // 3, "sy": 0, "sz": j // 3, "mx": k % 3, "my": 0, "mz": j % 3, "material": "Stone"}
                 for vx in range(2, 8) for vz in range(10, 16) for k in range(5) for j in range(9)]
        print("rig C step:", api.post("/api/world/microcubes/batch", {"microcubes": items}))
    api.debug("water_av_realtime", {"on": True})
    res = {}
    # A: a 12 x 8 m pond on the sand slab, inside one chunk (x 2-13, z 0-7), 1 m deep in a 2 m box
    boxA = {"x1": 2, "y1": 16, "z1": 0, "x2": 13, "y2": 17, "z2": 7}
    if "A" in args.only: res["A_flat"] = roundtrip(api, "A flat pond", boxA, {**boxA, "y2": 16}, wait_auto=30.0)
    # B: 20 m of the creek's carve (the bed at ~15.4-15.5 m in gravel microcube steps, the ground's top at 16), a closed box,
    # the voxel layer 15 filled (water only where the carve leaves air: placeBox never puts water in rock)
    boxB = {"x1": 40, "y1": 15, "z1": 30, "x2": 59, "y2": 16, "z2": 33}
    if "B" in args.only: res["B_stepped"] = roundtrip(api, "B stepped creek bed", boxB, {**boxB, "y2": 15}, wait_auto=15.0)
    # C: a 1/9 m step INSIDE every voxel column (the creek's steps fall on voxel edges, so B never tests it): Stone microcubes
    # under 5 of each column's 9 sub-columns along x on the floor layer, x 2-7, z 10-15 - authored by --build-step (once)
    boxC = {"x1": 2, "y1": 16, "z1": 10, "x2": 7, "y2": 17, "z2": 15}
    if "C" in args.only: res["C_step_in_column"] = roundtrip(api, "C a step inside each column", boxC, {**boxC, "y2": 16}, wait_auto=15.0)
    (EV / f"{args.tag}.json").write_text(json.dumps(res, indent=1))
    for k, o in res.items():
        s = o.get("sleep", {}); cw = o.get("census_woken", {}); c5 = o.get("census_woken_5s", {})
        print(f"{k}: placed {o['census_placed']['mass_m3']:.4f} m3 (in ground {o['census_placed']['in_solid_m3']:.4f}); auto-slept {o['auto_slept_after_s']}; "
              f"before sleep {o['mass_before_sleep_m3']:.4f}; sleep ok {s.get('success')} forced {s.get('forced')} {o.get('sleep_ms', 0):.0f} ms, "
              f"written {s.get('mass_written')} stored {s.get('mass_stored')} held {s.get('held_columns')} spread {s.get('spread_mm')} mm; "
              f"record total depth {o['spans_after_sleep'].get('total_depth')} (before {o['spans_before'].get('total_depth')}); wake {o['wake_ms']:.0f} ms seeded {o['wake'].get('seeded_mass')} {o['wake'].get('error') or ''}; "
              f"woken in air {cw.get('in_air_m3')} in ground {cw.get('in_solid_m3')} ({cw.get('in_solid_cells')} cells) over-full {cw.get('over_full_m3')}; "
              f"after 5 s total {c5.get('mass_m3')} in ground {c5.get('in_solid_m3')}; level change {o.get('level_change_mm')}", flush=True)
    print("wrote", EV / f"{args.tag}.json")


if __name__ == "__main__":
    main()
