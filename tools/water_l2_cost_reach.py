"""WaterCore L2 (43.2) - what the reach saves, whole frame: four disturbed ponds, the camera by one, ripple_reach on vs off.

WaterBench_Creek (port 8114, Release). The 60 x 28 m lake area of 27.24 split into four 15 x 28 m GPU ponds (x 4+15k .. 18+15k,
z 2-29, 2 m of water, auto-sleep off), the camera (the water's viewer - no test viewer) at (11, 21, -1) looking north: the ponds'
boxes lie 3, 8, 23 and 38 m away, so with ripple_m 21 the two far ponds have no layer. A 1/3 m Stone drops 1 m into EVERY pond in
turn, one every 0.125 s (each pond every 0.5 s: all four layers stay awake with the reach off). Conditions alternate on / off three
times, each a fresh set of ponds, a --seconds window of:
  the whole frame - fps, the CPU frame (engine_timing cpuFrameTime) median, its worst frame, the GPU frame (gpu_timing median);
  the ripples' share - the host profile's `ripples.2 masks + layers` in ms per frame; at the end water_ripples (layers, awake,
  cells, slots, device states, gpu_ms - one frame's device time).
Prediction (43.1): on saves ~0.2-0.7 ms of the frame per far disturbed pond, so ~0.4-1.4 ms here; the GPU frame within noise.
Not measured: ponds beyond the reach that are NOT disturbed (43.1: ~0.01 ms each - nothing to save).

    python tools/water_l2_cost_reach.py [--seconds 10] [--tag l2_cost_reach]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
H, TOP, X0, Z0, W = 1.0 / 9.0, 16, 4, 2, 28
PONDS = [(X0 + 15 * k, Z0, X0 + 15 * k + 14, Z0 + W - 1) for k in range(4)]
CAM = {"x": 11.0, "y": 21.0, "z": -1.0}


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def make(api):
    api.debug("clear_dynamics"); destroy_all(api)
    for (x1, z1, x2, z2) in PONDS:
        b = {"x1": x1, "y1": TOP, "z1": z1, "x2": x2, "y2": TOP + 2, "z2": z2}
        r = api.debug("water_av_create", {**b, "cellSize": H, "backend": "gpu", "auto_sleep": False})
        if "error" in r: raise SystemExit(r["error"])
        for k in range(2): api.debug("place_water_box", {"target": "core", **b, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
    time.sleep(10.0)


def window(api, seconds):
    fps, cpu, fmax = [], [], []
    api.debug("water_av_host_phases", {"reset": True})
    api.get("/api/debug/engine_timing")
    t0, k, nxt = time.time(), 0, time.time()
    while time.time() - t0 < seconds:
        if time.time() >= nxt:
            x1, z1, x2, z2 = PONDS[k % 4]
            api.debug("spawn_gpu_particle", {"x": x1 + 7.0 + (k // 4) % 3 - 1, "y": TOP + 3.17, "z": 8.0 + (k // 12) % 3, "material": "Stone",
                                             "scale": 1 / 3, "lifetime": 60, "spin": 0})
            k += 1; nxt += 0.125
        et = api.get("/api/debug/engine_timing"); fps.append(et.get("fps", 0)); cpu.append(et.get("cpuFrameTime", 0))
        if et.get("frame_ms_max_since_last_read") is not None: fmax.append(et["frame_ms_max_since_last_read"])
        time.sleep(0.05)
    ph = api.debug("water_av_host_phases", {}).get("phases", {})
    gt = api.get("/api/debug/gpu_timing")
    s = api.debug("water_ripples", {"shape": False})
    return {"fps_mean": round(statistics.mean(fps), 1), "cpu_frame_ms_median": round(statistics.median(cpu), 3),
            "frame_ms_max": round(max(fmax), 2) if fmax else None,
            "gpu_frame_ms_median": round(gt.get("gpu_frame_ms", {}).get("median_ms", 0.0), 3),
            "ripple_layers_ms": round(ph.get("ripples.2 masks + layers", {}).get("ms_per_frame", 0.0), 3),
            "ripple_gpu_ms_end": round(s.get("gpu_ms", 0.0), 3), "layers": s.get("layers"), "awake_end": s.get("awake"),
            "cells": s.get("cells"), "slots_held": s.get("slots_held"), "gpu_states": s.get("gpu_states"), "drops": k}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8114)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--tag", default="l2_cost_reach")
    a = ap.parse_args()
    api = Api(f"http://127.0.0.1:{a.port}"); st = api.wait_status(300)
    loc0 = api.debug("water_locality", {})
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_ripples", {"enabled": True, "shape": False})
    api.debug("water_locality", {"clear_test_viewer": True})
    api.post("/api/camera", {"mode": "free", "position": CAM, "yaw": 90, "pitch": -25})
    rows = []
    try:
        for c in ["on", "off", "off", "on", "on", "off"]:
            api.debug("water_locality", {"ripple_reach": c == "on", "ripple_m": 21.0, "ripple_fade_m": 8.0})
            make(api)
            r = {"cond": c, **window(api, a.seconds)}
            print(r, flush=True); rows.append(r)
    finally:
        api.debug("clear_dynamics"); destroy_all(api)
        api.debug("water_locality", {"ripple_reach": loc0.get("ripple_reach", False), "ripple_m": loc0.get("ripple_m", 21.0), "ripple_fade_m": loc0.get("ripple_fade_m", 8.0)})
    summ = {c: {k: [r[k] for r in rows if r["cond"] == c] for k in ("fps_mean", "cpu_frame_ms_median", "frame_ms_max", "gpu_frame_ms_median", "ripple_layers_ms", "ripple_gpu_ms_end", "layers", "cells")} for c in ("on", "off")}
    print(json.dumps(summ, indent=1))
    (EV / f"{a.tag}.json").write_text(json.dumps({"build": st.get("build_config"), "seconds": a.seconds, "rows": rows, "summary": summ}, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
