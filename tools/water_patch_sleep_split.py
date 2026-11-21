"""WaterCore 57.7 - the frame a lake's patch goes back to the record, split by part (the sleep.* laps in WaterCoreManager::sleep).
57.6 found it: 255-264 ms, in update, outside every timed lap. WaterBench_Creek (port 8114), the 57.6 lake: 60 x 60 m, 2 m deep,
in the record; the visible stone (2 m / strength 8) at its centre; the player off the lake; then wait for the patch to sleep
(only the phase timers and engine_timing polled - never water_av_list while it matters, D9) and read each part's longest call.

    python tools/water_patch_sleep_split.py [--url http://127.0.0.1:8114] [--runs 2] [--handover N] [--out ...json]

57.8: --handover sets water_av_record_draw handover_tiles_per_frame (0 = the one-frame return; omitted = unchanged); the sleep is
seen by either lap (sleep.9 the one-frame tiles, sleep.8 retire the spread hand-over); the worst frame is reported for the whole run
(the wake's included) AND for the sleep window (1 s before the sleep is seen to 3 s after - the hand-over's frames), apart.

58 item 1: --windows resets the host phases at every read and keeps each window's longest call of every phase over 1 ms (so a
long frame is matched to the parts that ran in its window), and the wake's create split (water_coupling) is read right after the
splash. Off by default: the sleep parts are then the run's maxima, as before.

58 item 1: --trace reads nothing while the splash lives except the phase timers every 5 s (to see the sleep), then the engine's
per-frame ring (frame_trace timing: dt, API drain, water, water update, GPU) once, from the splash to 3 s after the sleep - every
frame of the splash's life, each split; frames over 2x the median of the 2 s before the splash are listed (the 58 gate).
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_play_scene as P


def trace_run(api, k):
    api.post("/api/entity/move", {"id": "player", "position": {"x": 2.0, "y": P.TOP + 1.2, "z": 0.0}})
    P.destroy_all(api)
    time.sleep(0.5)
    rl = api.debug("water_av_record_lake", {"x1": P.X0, "z1": P.Z0, "x2": P.X0 + P.W - 1, "z2": P.Z0 + P.W - 1, "bottom": P.TOP, "top": P.TOP + 2.0})
    camera_set(api, P.CHURN_CAM)
    time.sleep(4.0)
    api.debug("water_av_host_phases", {"reset": True})
    f_pre = api.post("/api/debug/frame_trace", {"timing": True, "since": 0}).get("next", 0)
    time.sleep(2.0)   # the baseline: 2 s of the settled lake
    f0 = api.post("/api/debug/frame_trace", {"timing": True, "since": 1 << 62}).get("next", 0)
    st = api.debug("water_av_impulse", {"x": P.X0 + P.W / 2, "y": P.TOP + 1.9, "z": P.Z0 + P.W / 2, "radius": 2.0, "strength": 8.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    t0, slept_at = time.time(), None
    while time.time() - t0 < 45:
        time.sleep(5.0)
        ph = api.debug("water_av_host_phases", {}).get("phases", {})
        if slept_at is None and ("sleep.9 build the record tiles" in ph or "sleep.8 retire (57.8)" in ph): slept_at = round(time.time() - t0, 1)
        if slept_at is not None:
            time.sleep(3.0); break
    tr = api.post("/api/debug/frame_trace", {"timing": True, "since": f_pre})
    cols = tr.get("columns", [])
    rows = [dict(zip(cols, r)) for r in tr.get("frames", [])]
    base = sorted(r["dt_ms"] for r in rows if f_pre <= r["f"] < f0)
    life = [r for r in rows if r["f"] >= f0]
    med = base[len(base) // 2] if base else None
    lmed = sorted(r["dt_ms"] for r in life)[len(life) // 2] if life else None
    hitches = [{**{c: round(v, 2) if isinstance(v, float) else v for c, v in r.items()}, "i": r["f"] - f0} for r in life if med and r["dt_ms"] > 2.0 * med]
    r = {"run": k, "record_lake": rl, "stone": st, "slept_at_s": slept_at, "frames_before": len(base), "median_before_ms": med,
         "frames_life": len(life), "median_life_ms": lmed, "max_life_ms": max((x["dt_ms"] for x in life), default=None),
         "hitches_over_2x_median": hitches, "foreign_gpu_processes": P.foreign(), "frames": rows}
    print(f"run {k}: slept at {slept_at} s, median before {med} ms over {len(base)} frames, life {len(life)} frames median {lmed} max {r['max_life_ms']}, {len(hitches)} frames over 2x", flush=True)
    for h in hitches: print("   ", h, flush=True)
    return r


def one(api, k, handover=None, windows=False):
    api.post("/api/entity/move", {"id": "player", "position": {"x": 2.0, "y": P.TOP + 1.2, "z": 0.0}})
    P.destroy_all(api)
    time.sleep(0.5)
    rl = api.debug("water_av_record_lake", {"x1": P.X0, "z1": P.Z0, "x2": P.X0 + P.W - 1, "z2": P.Z0 + P.W - 1, "bottom": P.TOP, "top": P.TOP + 2.0})
    camera_set(api, P.CHURN_CAM)
    time.sleep(4.0)
    if handover is not None: api.debug("water_av_record_draw", {"handover_tiles_per_frame": handover})
    api.debug("water_av_host_phases", {"reset": True})
    api.debug("water_av_record_tiles", {"phases_reset": True})   # 58 item 1: the tile record split, from the splash on
    api.get("/api/debug/engine_timing")
    st = api.debug("water_av_impulse", {"x": P.X0 + P.W / 2, "y": P.TOP + 1.9, "z": P.Z0 + P.W / 2, "radius": 2.0, "strength": 8.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    t0, worst, slept_at, wins = time.time(), [], None, []
    wake = None
    if windows:
        time.sleep(0.3)
        cp = api.debug("water_coupling", {})
        sw = cp.get("slept", cp)
        wake = {k2: sw.get(k2) for k2 in ("last_wake_ms", "max_wake_ms", "last_wake_split_ms", "last_patch_split_ms", "last_upload_split_ms", "last_grow_split_ms")}
    while time.time() - t0 < 45:
        et = api.get("/api/debug/engine_timing")
        worst.append((round(time.time() - t0, 1), et.get("frame_ms_max_since_last_read")))
        ph = api.debug("water_av_host_phases", {}).get("phases", {})
        if slept_at is None and ("sleep.9 build the record tiles" in ph or "sleep.8 retire (57.8)" in ph): slept_at = round(time.time() - t0, 1)
        if windows:
            hpr = api.debug("water_av_host_phases", {})
            fr = max(1, hpr.get("frames", 1))
            wins.append({"t": worst[-1][0], "frame_ms_max": worst[-1][1],
                         "parts": {n: round(v.get("max_ms_per_call", 0.0), 2) for n, v in sorted(ph.items()) if v.get("max_ms_per_call", 0.0) >= 1.0 or n.startswith("seed.")},   # 58.3: the seed's parts, all
                         # 58: each phase's total in the window (many small calls) and the worst water block's split
                         "totals_ms": {n: round(v.get("ms_per_frame", 0.0) * fr, 1) for n, v in sorted(hpr.get("phases", {}).items()) if v.get("ms_per_frame", 0.0) * fr >= 5.0},
                         "calls": {n: round(v.get("calls_per_frame", 0.0) * fr) for n, v in sorted(hpr.get("phases", {}).items()) if v.get("ms_per_frame", 0.0) * fr >= 5.0},
                         "frames": fr, "worst_water_block": hpr.get("app_water_cpu_ms_worst_frame")})
            api.debug("water_av_host_phases", {"reset": True})
        if slept_at is not None and time.time() - t0 > slept_at + 3: break
        time.sleep(0.5)
    ph = api.debug("water_av_host_phases", {})
    parts = {n: round(v.get("max_ms_per_call", 0.0), 2) for n, v in ph.get("phases", {}).items() if n.startswith(("sleep.", "tilesNow.", "destroy."))}
    big = max(worst, key=lambda r: r[1] or 0)
    win = [w for w in worst if slept_at is not None and slept_at - 1.0 <= w[0] <= slept_at + 3.0]
    big_sleep = max(win, key=lambda r: r[1] or 0) if win else None
    rd = api.debug("water_av_record_draw", {})
    tile_phases = api.debug("water_av_record_tiles", {}).get("phases")
    r = {"run": k, "record_lake": rl, "stone": st, "slept_at_s": slept_at, "sleep_parts_ms": dict(sorted(parts.items())),
         "sleep_total_ms": round(sum(v for n, v in parts.items() if n.startswith("sleep.")), 2), "worst_frame": big,
         "worst_frame_sleep_window": big_sleep, "record_draw": rd, "worst_frame_ms_by_read": worst,
         "foreign_gpu_processes": P.foreign(), "wake": wake, "tile_phases": tile_phases, "windows": wins}
    print(f"run {k}: slept at {slept_at} s, worst frame {big}, in the sleep window {big_sleep}, hand-over frames {rd.get('handover_frames_last')} max tiles a frame {rd.get('handover_tiles_max_per_frame')}, sleep parts {r['sleep_parts_ms']} (sum {r['sleep_total_ms']} ms)", flush=True)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--runs", type=int, default=2)
    ap.add_argument("--handover", type=int, default=None)
    ap.add_argument("--windows", action="store_true")
    ap.add_argument("--trace", action="store_true")
    ap.add_argument("--spare", type=int, default=None, help="58 item 1: water_locality patch_spare (1 on, 0 off; omitted = unchanged)")
    ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4" / "s57_patch_sleep_split.json"))
    a = ap.parse_args()
    if P.foreign(): raise SystemExit(f"other phyxel processes share the GPU: {P.foreign()}")
    api = Api(a.url); api.wait_status(300)
    if "error" in P.ps(api):
        api.post("/api/entity/spawn", {"type": "animated", "position": {"x": 2.0, "y": P.TOP + 1.2, "z": 0.0}, "id": "player"})
    api.debug("water_av_realtime", {"on": True})
    if a.spare is not None: print("patch_spare", api.debug("water_locality", {"patch_spare": bool(a.spare)}).get("patch_spare"), flush=True)
    out = []
    try:
        P.destroy_all(api); P.clear_record(api); P.lay_bed(api)
        time.sleep(30.0)   # the far skin's re-read of the clear (57.4 item 2) done before the first run
        if a.handover is not None: api.debug("water_av_record_draw", {"handover_tiles_per_frame": a.handover})
        for k in range(a.runs): out.append(trace_run(api, k) if a.trace else one(api, k, a.handover, a.windows))
    finally:
        P.destroy_all(api)
        pathlib.Path(a.out).write_text(json.dumps(out, indent=1), encoding="utf-8")
        print("wrote", a.out)


if __name__ == "__main__":
    main()
