"""WaterCore.md 26.29 - the pond size ladder: at what size does one awake pond no longer fit the 2 ms water budget?

Owner, 2026-10-18: "we need to be sure. lets take some measurements." The estimate from the 26.11 cost law (0.75 ms a call +
~2.6 ns per stored cell-substep) was ~26 m (0.3 m deep), ~20 m (1 m), ~16 m (2 m) on a side. This bench measures it.

The rig (WaterBench_Creek, port 8114, Release, the GPU otherwise idle): one rectangular pond on the flat slab at z 0-27 (clear
of the creek at z 29-34), its floor the grass top (y 16), its walls the volume's box faces (a box face is closed - no shore,
which a real pond would have; the cost counts water and its halo, not the walls). One sparse GPU pond-water volume at 1/9 m,
pond friction (no Manning: still water), the engine's default bricks (8 x 2 x 8) and sweeps, auto-sleep OFF (the volume stays,
so its sleeping cost is measured too). Each rung, in order:
  CONTROL  no volume                                   - the frame's own cost
  REST     the water placed flat (it sleeps at once), poll --still s
  STONE    an impulse at the pond's centre (a stone-sized downward kick, radius 1/3 m), poll --stone s
Per poll: the volume's GPU device ms (the solver's call), substeps, stored bricks; the engine's fps. Per phase: the GPU's own
pass timings (/api/debug/gpu_timing) - the whole frame and the WaterSurface pass (measured 2026-10-18: a resting 20 x 20 m pond
costs ~3.7 ms there with the solver at 0, and ~3.4 ms with the camera turned away).
Sizes are AREAS (m^2); a pond wider than 28 m becomes a 28 m x (A / 28) m rectangle. A rung whose box would pass the volume
cap (2,000,000 cells, the CPU reference's) is reported as refused, not run.

    python tools/water_pond_ladder.py [--depths 0.3,1,2] [--areas 25,100,225,400,625,900,1300]
Writes docs/evidence/water_core_h1/pond_ladder.json.
"""
import argparse, json, math, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
H = 1.0 / 9.0
CAP = 2_000_000
GROUND_TOP = 16          # the slab's grass top (water_creek_bench.py --build)
X0, Z0, ZMAX = 2, 0, 28  # the pond's corner and the widest it may be (z 0-27)


def med(xs):
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else None


def timing(api):
    et = api.get("/api/debug/engine_timing")
    out = {"fps": et.get("fps")}
    for k, v in et.items():
        if isinstance(v, (int, float)) and k.endswith("_ms"): out[k] = v
    return out


def volume(api, vid):
    for v in api.debug("water_av_list").get("volumes", []):
        if v["id"] == vid: return v
    return None


def poll(api, vid, seconds, period=0.25):
    rows, t0 = [], time.time()
    while time.time() - t0 < seconds:
        row = timing(api)
        if vid:
            v = volume(api, vid)
            row.update({"gpu_device_ms": v.get("gpu_device_ms"), "gpu_ms_last_call": v.get("gpu_ms_last_call"), "substeps": v["last"]["substeps"],
                        "bricks": v.get("gpu_bricks"), "asleep": v.get("asleep"), "mass": v.get("mass"), "ke": v.get("kinetic_energy")})
        rows.append(row)
        time.sleep(period)
    return rows


def passes(api, frames=120):
    """The GPU's own pass timings over the last `frames` frames (/api/debug/gpu_timing): the whole frame, the water's surface
    pass, and every other pass over 0.005 ms - the solver's call is not among them (it is gpu_device_ms)."""
    d = api.get(f"/api/debug/gpu_timing?frames={frames}")
    out = {"frame_gpu_ms": (d.get("gpu_frame_ms") or {}).get("median_ms")}
    for sc in d.get("scopes", []):
        if sc.get("median_ms", 0) > 0.005: out["pass:" + sc["name"]] = round(sc["median_ms"], 4)
    return out


def summarise(rows, api=None, frames=120):
    s = {"n": len(rows), "fps_median": med([r.get("fps") for r in rows])}
    if api is not None: s.update(passes(api, frames))
    for k in sorted({k for r in rows for k in r if k.endswith("_ms")}):
        vals = [r.get(k) for r in rows if r.get(k) is not None]
        if vals: s[k + "_median"] = med(vals); s[k + "_max"] = max(vals)
    if any("substeps" in r for r in rows):
        s["substeps_max"] = max(r.get("substeps") or 0 for r in rows)
        s["bricks_max"] = max(r.get("bricks") or 0 for r in rows)
        s["asleep_fraction"] = sum(1 for r in rows if r.get("asleep")) / len(rows)
        awake = [r.get("gpu_device_ms") for r in rows if not r.get("asleep") and r.get("gpu_device_ms")]
        s["solver_ms_awake_median"] = med(awake); s["solver_ms_awake_max"] = max(awake) if awake else None; s["awake_polls"] = len(awake)
        s["mass_first"], s["mass_last"] = rows[0].get("mass"), rows[-1].get("mass")
    return s


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})


def rung(api, args, depth, area):
    w = min(ZMAX, max(1, round(math.sqrt(area))))
    l = max(1, round(area / w))
    hv = math.floor(depth) + 1                       # the box's voxel layers: the water + at least 0.7 m of air above it
    box = {"x1": X0, "y1": GROUND_TOP, "z1": Z0, "x2": X0 + l - 1, "y2": GROUND_TOP + hv - 1, "z2": Z0 + w - 1}
    cells = 729 * l * w * hv
    out = {"depth_m": depth, "area_m2": l * w, "size_m": [l, w], "box_voxels": box, "box_cells": cells}
    if cells > CAP:
        out["refused"] = f"the box would be {cells} cells; the volume cap is {CAP}"
        print(f"  depth {depth} m, {l} x {w} m: REFUSED ({cells} cells > cap)", flush=True)
        return out
    destroy_all(api)
    r = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False})
    if "error" in r: out["refused"] = r["error"]; print("  create:", r["error"]); return out
    vid = r["volume"]["id"]
    out["brick_height"] = r["volume"].get("gpu_brick_height"); out["sweeps"] = r["volume"].get("gpu_sweeps")
    full = math.floor(depth); frac = depth - full
    for k in range(full):
        api.debug("place_water_box", {"target": "core", "x1": box["x1"], "y1": GROUND_TOP + k, "z1": box["z1"], "x2": box["x2"], "y2": GROUND_TOP + k, "z2": box["z2"], "mass": 1.0})
    if frac > 1e-6:
        api.debug("place_water_box", {"target": "core", "x1": box["x1"], "y1": GROUND_TOP + full, "z1": box["z1"], "x2": box["x2"], "y2": GROUND_TOP + full, "z2": box["z2"], "mass": frac})
    out["mass_placed"] = volume(api, vid)["mass"]
    t0 = time.time()   # flat water placed still sleeps at once (measured 2026-10-18): REST is the placed pond, asleep
    while time.time() - t0 < args.rest_max and not volume(api, vid).get("asleep"): time.sleep(0.5)
    out["seconds_to_sleep"] = round(time.time() - t0, 1) if volume(api, vid).get("asleep") else None
    time.sleep(1.0)
    out["rest"] = summarise(poll(api, vid, args.still), api)
    cx, cz = (box["x1"] + box["x2"] + 1) / 2.0, (box["z1"] + box["z2"] + 1) / 2.0
    api.debug("water_av_impulse", {"x": cx, "y": GROUND_TOP + depth - 0.1, "z": cz, "radius": 1.0 / 3.0, "strength": args.strength, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    out["stone"] = summarise(poll(api, vid, args.stone), api, 240)
    t1 = time.time()
    while time.time() - t1 < args.rest_max and not volume(api, vid).get("asleep"): time.sleep(0.5)
    out["seconds_awake_after_stone"] = round(args.stone + time.time() - t1, 1) if volume(api, vid).get("asleep") else None
    destroy_all(api)
    rs, st = out["rest"], out["stone"]
    g = lambda d, k: d.get(k) or 0.0
    print(f"  depth {depth} m, {l} x {w} m ({l * w} m^2): bricks {st.get('bricks_max')} | ASLEEP frame {g(rs, 'frame_gpu_ms'):.2f} ms, surface {g(rs, 'pass:WaterSurface'):.2f}, "
          f"solver {g(rs, 'gpu_device_ms_median'):.2f} | STONE frame {g(st, 'frame_gpu_ms'):.2f}, surface {g(st, 'pass:WaterSurface'):.2f}, solver awake median {g(st, 'solver_ms_awake_median'):.2f} "
          f"max {g(st, 'solver_ms_awake_max'):.2f}, substeps max {st.get('substeps_max')}, awake {out['seconds_awake_after_stone']} s | fps {g(rs, 'fps_median'):.0f} / {g(st, 'fps_median'):.0f}", flush=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--depths", default="0.3,1,2")
    ap.add_argument("--areas", default="25,100,225,400,625,900,1300")
    ap.add_argument("--still", type=float, default=5.0)
    ap.add_argument("--rest-max", type=float, default=30.0)
    ap.add_argument("--stone", type=float, default=6.0)
    ap.add_argument("--strength", type=float, default=4.0)
    ap.add_argument("--out", default="pond_ladder.json")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(120)
    api.debug("water_av_realtime", {"on": True})
    destroy_all(api)
    api.post("/api/camera", {"position": {"x": 22.0, "y": 32.0, "z": 48.0}, "yaw": -90.0, "pitch": -35.0})
    time.sleep(2.0)
    result = {"control": summarise(poll(api, 0, args.still), api), "rungs": [], "args": vars(args)}
    print("control:", result["control"], flush=True)
    for d in [float(x) for x in args.depths.split(",")]:
        for a in [float(x) for x in args.areas.split(",")]:
            result["rungs"].append(rung(api, args, d, a))
            EV.mkdir(parents=True, exist_ok=True)
            (EV / args.out).write_text(json.dumps(result, indent=1))
    print("wrote", EV / args.out)


if __name__ == "__main__":
    main()
