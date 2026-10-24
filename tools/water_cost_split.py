"""Where the water's time goes that nobody sees (owner, 2026-10-23: "nail down some of these performance issues first").

Two questions from the 27.20 limits pass, measured on WaterBench_Creek (port 8114, Release, GPU idle):
  --depth   a 32 x 28 m walled pond at 0.3 / 1 / 2 m, a stone at the centre: the solver's GPU time per STAGE
            (water_av_kernel_timing - timestamps on single-tick, single-substep calls; a barrier each, so totals read a little
            high), with the partial wake ON (shipped) and OFF (every stored brick steps), plus bricks / stepped slots. Separates
            the per-cell work (grows with depth) from the call's own cost (does not).
  --count   N sleeping 5 x 5 m ponds (0.3 m), N in --counts: the CPU frame and the water's CPU parts (water_render_core
            water_cpu_ms: update and its parts, feed, motion, the tiler) - which part grows ~0.09 ms per sleeping volume (27.20 #6).
Writes docs/evidence/water_core_h1/cost_split.json.

    python tools/water_cost_split.py --depth --count
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
H, TOP = 1.0 / 9.0, 16


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def vol(api, vid):
    return next(v for v in api.debug("water_av_list")["volumes"] if v["id"] == vid)


def pond(api, x, z, l, w, depth):
    hv = int(depth) + 1
    box = {"x1": x, "y1": TOP, "z1": z, "x2": x + l - 1, "y2": TOP + hv - 1, "z2": z + w - 1}
    r = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False})
    if "error" in r: raise SystemExit(r["error"])
    vid = r["volume"]["id"]
    full, frac = int(depth), depth - int(depth)
    for k in range(full): api.debug("place_water_box", {"target": "core", **box, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
    if frac > 1e-6: api.debug("place_water_box", {"target": "core", **box, "y1": TOP + full, "y2": TOP + full, "mass": frac})
    return vid, box


def depth_split(api, a):
    out = []
    for pw in (True, False):
        for depth in (0.3, 1.0, 2.0):
            destroy_all(api)
            vid, box = pond(api, 2, 0, 32, 28, depth)
            api.debug("water_av_partial_wake", {"id": vid, "on": pw})
            t0 = time.time()
            while time.time() - t0 < 30 and not vol(api, vid).get("asleep"): time.sleep(0.5)
            api.debug("water_av_impulse", {"x": 18.0, "y": TOP + depth - 0.1, "z": 14.0, "radius": 1 / 3, "strength": 4.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
            time.sleep(0.5)
            api.debug("water_av_kernel_timing", {"on": True, "reset": True})
            rows, t1 = [], time.time()
            while time.time() - t1 < a.seconds:
                v = vol(api, vid)
                rows.append({"solver_ms": v.get("gpu_device_ms"), "bricks": v.get("gpu_bricks"), "step_slots": v.get("step_slots"), "substeps": v["last"]["substeps"]})
                time.sleep(0.25)
            kt = api.debug("water_av_kernel_timing", {"on": False})
            row = {"partial_wake": pw, "depth_m": depth, "calls_timed": kt.get("calls"), "stage_ms": kt.get("ms_per_call"), "stage_total_ms": kt.get("total_ms_per_call"),
                   "solver_ms_median": statistics.median(r["solver_ms"] or 0 for r in rows), "bricks": statistics.median(r["bricks"] or 0 for r in rows),
                   "step_slots_median": statistics.median(r["step_slots"] or 0 for r in rows), "substeps_max": max(r["substeps"] for r in rows)}
            out.append(row)
            top = sorted((row["stage_ms"] or {}).items(), key=lambda kv: -kv[1])[:8]
            print(f"  pw {int(pw)} depth {depth} m: solver {row['solver_ms_median']:.2f} ms, stages sum {row['stage_total_ms']:.2f} over {row['calls_timed']} calls, "
                  f"bricks {row['bricks']}, stepped slots {row['step_slots_median']} | " + ", ".join(f"{k} {v:.3f}" for k, v in top), flush=True)
    destroy_all(api)
    return out


def flat(d, pre=""):
    o = {}
    for k, v in d.items():
        if isinstance(v, dict): o.update(flat(v, pre + k + "."))
        elif isinstance(v, (int, float)): o[pre + k] = v
    return o


def count_split(api, a):
    out = []
    for n in [int(x) for x in a.counts.split(",")]:
        destroy_all(api)
        slots = [(2 + i * 6, j * 6) for j in range(4) for i in range(20)][:n]
        ids = [pond(api, x, z, 5, 5, 0.3)[0] for (x, z) in slots]
        t0 = time.time()
        while time.time() - t0 < 30 and any(not v.get("asleep") for v in api.debug("water_av_list")["volumes"]): time.sleep(0.5)
        time.sleep(1.5)
        rows = []
        for _ in range(20):
            et = api.get("/api/debug/engine_timing")
            t = api.debug("water_render_core", {}).get("tiles") or {}
            rows.append({"fps": et.get("fps"), "frame_ms": (et.get("detailed") or {}).get("totalFrameTime"), "tiler_ms": t.get("tiler_ms", 0.0), **flat(t.get("water_cpu_ms", {}), "w.")})
            time.sleep(0.2)
        keys = sorted({k for r in rows for k in r})
        row = {"n": len(ids), **{k: statistics.median(r.get(k, 0.0) or 0.0 for r in rows) for k in keys}}
        out.append(row)
        print(f"  {n} sleeping ponds: fps {row['fps']:.0f}, frame {row['frame_ms']:.2f} ms, water total {row.get('w.total', 0):.3f}, update {row.get('w.update', 0):.3f}, "
              f"feed {row.get('w.feed', 0):.3f}, motion {row.get('w.motion', 0):.3f}, tiler {row['tiler_ms']:.3f}", flush=True)
    destroy_all(api)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--depth", action="store_true")
    ap.add_argument("--count", action="store_true")
    ap.add_argument("--seconds", type=float, default=4.0)
    ap.add_argument("--counts", default="0,1,16,64")
    ap.add_argument("--out", default="cost_split.json")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    api.post("/api/camera", {"mode": "free", "position": {"x": 62.0, "y": 70.0, "z": 60.0}, "yaw": -90.0, "pitch": -55.0})
    time.sleep(2.0)
    res = {"args": vars(a)}
    if a.count: res["count"] = count_split(api, a)
    if a.depth: res["depth"] = depth_split(api, a)
    EV.mkdir(parents=True, exist_ok=True); (EV / a.out).write_text(json.dumps(res, indent=1))
    print("wrote", EV / a.out)


if __name__ == "__main__":
    main()
