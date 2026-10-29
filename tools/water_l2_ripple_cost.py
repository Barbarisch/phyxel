"""WaterCore L2 (ripple detail by distance) - measure first: what the ripple layer costs on a big awake pond, by window size.

WaterBench_Creek (port 8114, Release). The 60 x 28 m walled lake of 27.24 (x 4-63, z 2-29, y 16-18, 2 m of water; it settles,
unlike the 60 x 60 box over the trench). Auto-sleep off, so the lake stays awake. The camera at the lake's south edge, looking
north. Per condition the lake is made again (the ripple window's side is fixed when its layer is made): ripples off (the
control), or `water_locality {ripple_window_cells}` = 128 / 256 / 384 (the default) / 512. Then 1/3 m Stones dropped 1 m, one
every 0.25 s, at fixed points within 10 m of the camera (the rings stay awake; nothing rises), and a --seconds window:
  the whole frame - fps, the CPU frame (engine_timing cpuFrameTime), its worst frame, the GPU frame (gpu_timing median / max);
  the ripples' own share - the host profile's `ripples.2 masks + layers` (collect, masks, sources, record, submit) and
  `ripples.1` (the surface read, which other readers would pay without ripples) in ms per frame; at the end, water_ripples'
  `gpu_ms` (the last frame's device time), `cells`, `awake`.
Conditions run in an interleaved order, twice. Not measured: the draw's ripple sampling alone (inside the water surface draw);
more than one pond; a creek's moving window.

    python tools/water_l2_ripple_cost.py [--seconds 10] [--tag l2_cost] [--port 8114]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, math, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

H, TOP, X0, Z0, L, W = 1.0 / 9.0, 16, 4, 2, 60, 28
CAM = {"x": X0 + L / 2, "y": TOP + 5.0, "z": Z0 - 3.0}
DROPS = [(CAM["x"] + 6 * math.cos(a), Z0 + 4 + 3 * (1 + math.sin(a))) for a in [k * 0.9 for k in range(12)]]


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def make_lake(api, settle):
    api.debug("clear_dynamics")
    destroy_all(api)
    box = {"x1": X0, "y1": TOP, "z1": Z0, "x2": X0 + L - 1, "y2": TOP + 2, "z2": Z0 + W - 1}
    r = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False})
    if "error" in r: raise SystemExit("create refused: " + r["error"])
    vid = r["volume"]["id"]
    for k in range(2):
        rr = api.debug("place_water_box", {"target": "core", **box, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
        if "error" in rr: raise SystemExit(rr["error"])
    t0, want = time.time(), L * W * 2.0
    while time.time() - t0 < 60:
        v = next(v for v in api.debug("water_av_list")["volumes"] if v["id"] == vid)
        if v["mass"] >= 0.99 * want: break
        time.sleep(0.5)
    time.sleep(settle)
    return vid


def window(api, seconds):
    # never water_ripples inside the window: it computes the shape stats when asked (~0.1 s on the main thread, 43.1) - the
    # ripples' CPU comes from the host profile (ms per frame over the window), their device time from one read at the end
    fps, cpu, fmax = [], [], []
    api.debug("water_av_host_phases", {"reset": True})
    api.get("/api/debug/engine_timing")   # clears the worst-frame tracker
    t0, k, nextDrop = time.time(), 0, time.time()
    while time.time() - t0 < seconds:
        if time.time() >= nextDrop:   # a stone every 0.25 s keeps the rings awake (one stone's rings sleep within ~3 s)
            x, z = DROPS[k % len(DROPS)]; k += 1; nextDrop += 0.25
            api.debug("spawn_gpu_particle", {"x": x, "y": TOP + 2 + 1.0 + 1 / 6, "z": z, "material": "Stone", "scale": 1 / 3, "lifetime": 120, "spin": 0})
        et = api.get("/api/debug/engine_timing"); fps.append(et.get("fps", 0)); cpu.append(et.get("cpuFrameTime", 0))
        if et.get("frame_ms_max_since_last_read") is not None: fmax.append(et["frame_ms_max_since_last_read"])
        time.sleep(0.05)
    ph = api.debug("water_av_host_phases", {}).get("phases", {})
    gt = api.get("/api/debug/gpu_timing")
    s = api.debug("water_ripples", {})
    pf = lambda key: round(ph.get(key, {}).get("ms_per_frame", 0.0), 3)
    med = lambda xs: round(statistics.median(xs), 3) if xs else None
    return {"fps_mean": round(statistics.mean(fps), 1), "cpu_frame_ms_median": med(cpu), "frame_ms_max": round(max(fmax), 2) if fmax else None,
            "gpu_frame_ms_median": round(gt.get("gpu_frame_ms", {}).get("median_ms", 0.0), 3), "gpu_frame_ms_max": round(gt.get("gpu_frame_ms", {}).get("max_ms", 0.0), 3),
            "ripple_surface_read_ms": pf("ripples.1 surfaceFields (= surf.*)"), "ripple_layers_ms": pf("ripples.2 masks + layers"),
            "ripple_gpu_ms_end": round(s.get("gpu_ms", 0.0), 3), "ripple_cells": s.get("cells", 0), "ripple_awake_end": s.get("awake", 0),
            "layers": s.get("layers", 0), "drops": k}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8114)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--settle", type=float, default=6.0)
    ap.add_argument("--tag", default="l2_cost")
    a = ap.parse_args()
    api = Api(f"http://127.0.0.1:{a.port}")   # not "localhost": it tries IPv6 first, 2 s a call on this machine (43.1)
    st = api.get("/api/status")
    print("build", st.get("build_config"))
    api.debug("water_av_realtime", {"on": True})
    loc0 = api.debug("water_locality", {})
    rip0 = api.debug("water_ripples", {}).get("enabled", True)
    api.post("/api/camera", {"mode": "free", "position": CAM, "yaw": 90, "pitch": -25})
    conds = ["384", "off", "128", "512", "256"]
    order = conds + conds[::-1]
    rows = []
    try:
        for c in order:
            api.debug("water_ripples", {"enabled": c != "off"})
            api.debug("water_locality", {"ripple_window_cells": loc0.get("ripple_window_cells", 384) if c == "off" else int(c)})
            make_lake(api, a.settle)
            r = {"cond": c, **window(api, a.seconds)}
            print(r, flush=True)
            rows.append(r)
    finally:
        api.debug("clear_dynamics")
        destroy_all(api)
        api.debug("water_ripples", {"enabled": rip0})
        api.debug("water_locality", {"ripple_window_cells": loc0.get("ripple_window_cells", 384)})
    summary = {}
    for c in conds:
        rs = [r for r in rows if r["cond"] == c]
        summary[c] = {k: [r[k] for r in rs] for k in ("fps_mean", "cpu_frame_ms_median", "frame_ms_max", "gpu_frame_ms_median", "ripple_layers_ms", "ripple_surface_read_ms", "ripple_gpu_ms_end", "ripple_awake_end", "ripple_cells")}
    print(json.dumps(summary, indent=1))
    out = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4" / f"{a.tag}.json"
    out.write_text(json.dumps({"build": st.get("build_config"), "seconds": a.seconds, "rows": rows, "summary": summary}, indent=1))
    print("wrote", out)


if __name__ == "__main__":
    main()
