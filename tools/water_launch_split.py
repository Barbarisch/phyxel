"""WaterCore.md 28 step 0 - the launch count: how many compute dispatches and barriers one water call records, and where its GPU time
goes by stage, for a small pond and a large one (owner, 2026-10-23: "yes" - go after the batch count).

WaterBench_Creek (port 8114, Release, GPU idle), the shipped settings (partial wake on, 20 sweeps). Walled ponds, a stone at the
centre, 4 s polled: gpu_dispatches / gpu_barriers (water_av_list, the last call), the solver's GPU ms, and the per-stage GPU time
(water_av_kernel_timing, single-tick single-substep calls). Rigs: 5 x 5 m 0.3 m and 24 x 24 m 1 m, inside one chunk.

    python tools/water_launch_split.py [--json out.json]
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


def run(api, size, depth, seconds=4.0):
    destroy_all(api)
    x0, z0 = 4, 2
    hv = int(depth) + 1
    box = {"x1": x0, "y1": TOP, "z1": z0, "x2": x0 + size - 1, "y2": TOP + hv - 1, "z2": z0 + size - 1}
    vid = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
    full, frac = int(depth), depth - int(depth)
    for k in range(full): api.debug("place_water_box", {"target": "core", **box, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
    if frac > 1e-6: api.debug("place_water_box", {"target": "core", **box, "y1": TOP + full, "y2": TOP + full, "mass": frac})
    time.sleep(3.0)
    api.debug("water_av_impulse", {"x": x0 + size / 2, "y": TOP + depth - 0.1, "z": z0 + size / 2, "radius": 1 / 3, "strength": 4.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    time.sleep(0.3)
    api.debug("water_av_kernel_timing", {"on": True, "reset": True})
    rows, t0 = [], time.time()
    while time.time() - t0 < seconds:
        v = vol(api, vid)
        if not v.get("asleep"): rows.append({"ms": v.get("gpu_device_ms"), "d": v.get("gpu_dispatches"), "b": v.get("gpu_barriers"), "s": v["last"]["substeps"], "bricks": v.get("gpu_bricks"), "slots": v.get("step_slots")})
        time.sleep(0.2)
    kt = api.debug("water_av_kernel_timing", {"on": False})
    med = lambda k: statistics.median(r[k] or 0 for r in rows) if rows else None
    out = {"size_m": size, "depth_m": depth, "solver_ms": med("ms"), "dispatches": med("d"), "barriers": med("b"), "substeps": med("s"), "bricks": med("bricks"),
           "step_slots": med("slots"), "calls_timed": kt.get("calls"), "stage_ms": kt.get("ms_per_call"), "stage_total_ms": kt.get("total_ms_per_call")}
    top = sorted((out["stage_ms"] or {}).items(), key=lambda kv: -kv[1])
    print(f"  {size} x {size} m, {depth} m: solver {out['solver_ms']:.3f} ms, {out['dispatches']} dispatches, {out['barriers']} barriers, substeps {out['substeps']}, "
          f"bricks {out['bricks']}, stepped slots {out['step_slots']}; per dispatch {out['solver_ms'] / max(1, out['dispatches']) * 1000:.1f} us", flush=True)
    print("    stages: " + ", ".join(f"{k} {v:.3f}" for k, v in top), flush=True)
    destroy_all(api)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--json", default=str(EV / "launch_split.json"))
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    api.post("/api/camera", {"mode": "free", "position": {"x": 16.0, "y": 30.0, "z": 40.0}, "yaw": -90.0, "pitch": -40.0})
    time.sleep(2.0)
    res = {"runs": [run(api, 5, 0.3), run(api, 24, 1.0)]}
    pathlib.Path(a.json).write_text(json.dumps(res, indent=1))
    print("wrote", a.json)


if __name__ == "__main__":
    main()
