"""WaterCore.md D9 - why the frame rate swings during and after a splash (the owner, 2026-11-14: "i am concerned with the fps
fluctuating so much"). cosm511_clip.json read 12-367 fps after a splash, with ~12 fps dips every 1-4 s - but that clip ran the screen
recorder, polled water_av_list + engine_timing 4x a second, and shared the GPU with another session's engine.

This measures EVERY frame (the engine's frame_trace ring: dt, the API drain, the water block, the GPU's last resolved frame), read
only AFTER each window, so the measurement itself adds no API work during it. No recorder. The rig is the clip's: WaterBench_Creek
(port 8114), the 150 x 150 m record lake, the clip's splash (2 m, strength 8) at the clip's CLOSE pose.

Conditions, interleaved, each --reps times:
  rest         no splash, nothing polled
  rest_poll    no splash, the clip's polling (water_av_list + engine_timing every 0.25 s)
  splash       the splash, nothing polled
  splash_poll  the splash with the clip's polling (the clip's condition, minus the recorder)

    python tools/water_splash_frames.py [--seconds 12] [--reps 2] [--tag d9_frames]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, statistics, sys, threading, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_record_lake_clip import lay_lake, splash, destroy_all, POSES

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"


def mark(api):
    return api.debug("frame_trace", {"since": 1 << 62, "timing": True})["next"]


def frames_since(api, f0):
    r = api.debug("frame_trace", {"since": f0, "timing": True})
    cols = r["columns"]
    return [dict(zip(cols, row)) for row in r["frames"]]


def poller(api, stop):
    while not stop.is_set():
        try:
            api.debug("water_av_list", {})
            api.get("/api/debug/engine_timing")
        except Exception:
            pass
        time.sleep(0.25)


def summarize(rows, t_splash_frame=None):
    dts = [r["dt_ms"] for r in rows]
    if not dts: return {}
    s = sorted(dts)
    q = lambda p: s[min(len(s) - 1, int(p * len(s)))]
    total_s = sum(dts) / 1000.0
    slow = [r for r in rows if r["dt_ms"] > 33.3]
    t = 0.0
    times = []
    for r in rows:
        times.append(t); t += r["dt_ms"] / 1000.0
    out = {"frames": len(rows), "seconds": round(total_s, 2), "fps_mean": round(len(rows) / total_s, 1),
           "dt_p50": round(q(0.50), 2), "dt_p95": round(q(0.95), 2), "dt_p99": round(q(0.99), 2), "dt_max": round(s[-1], 2),
           "over_16ms": sum(1 for d in dts if d > 16.7), "over_33ms": len(slow), "over_50ms": sum(1 for d in dts if d > 50.0),
           "api_ms_total": round(sum(r["api_ms"] for r in rows), 1), "water_ms_mean": round(statistics.mean(r["water_ms"] for r in rows), 3),
           "water_ms_max": round(max(r["water_ms"] for r in rows), 2), "api_ms_max": round(max(r["api_ms"] for r in rows), 2),
           "gpu_ms_median": round(statistics.median(r["gpu_ms"] for r in rows if r["gpu_ms"] >= 0), 2) if any(r["gpu_ms"] >= 0 for r in rows) else None,
           "gpu_ms_max": round(max(r["gpu_ms"] for r in rows), 2)}
    # the slow frames, each with where its time went (the water and API of the SAME frame; GPU is the last resolved one)
    out["slow_frames"] = [{"t_s": round(times[i], 2), "dt": round(r["dt_ms"], 1), "api": round(r["api_ms"], 1), "water": round(r["water_ms"], 1),
                           "water_update": round(r["water_update_ms"], 1), "gpu_last": round(r["gpu_ms"], 1)}
                          for i, r in enumerate(rows) if r["dt_ms"] > 33.3][:40]
    return out


def take(api, cond, seconds):
    lay_lake(api)
    camera_set(api, POSES["close"])
    time.sleep(5.0)
    stop = threading.Event()
    th = None
    if cond.endswith("_poll"):
        th = threading.Thread(target=poller, args=(Api(api.url), stop), daemon=True)
    f0 = mark(api)
    if th: th.start()
    time.sleep(1.0)
    k = splash(api) if cond.startswith("splash") else None
    time.sleep(seconds)
    stop.set()
    if th: th.join()
    rows = frames_since(api, f0)
    s = summarize(rows)
    s["cond"] = cond
    s["splash"] = k
    print(f"  {cond:12s} frames {s['frames']:5d}  fps {s['fps_mean']:6.1f}  dt p50 {s['dt_p50']:6.2f}  p95 {s['dt_p95']:6.2f}  p99 {s['dt_p99']:6.2f}"
          f"  max {s['dt_max']:7.2f}  >33ms {s['over_33ms']:3d}  >50ms {s['over_50ms']:3d}  water mean {s['water_ms_mean']:.3f} max {s['water_ms_max']:.1f}"
          f"  api max {s['api_ms_max']:.1f}  gpu med {s['gpu_ms_median']} max {s['gpu_ms_max']}", flush=True)
    for sf in s["slow_frames"][:12]: print("      slow:", sf)
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--seconds", type=float, default=12.0)
    ap.add_argument("--reps", type=int, default=2)
    ap.add_argument("--tag", default="d9_frames")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    api.debug("water_av_realtime", {"on": True})
    draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
    api.debug("water_av_record_draw", {"radius": 200.0})
    out = {"rig": "WaterBench_Creek, the 150 m record lake, the clip's close pose and splash", "seconds": a.seconds, "takes": []}
    try:
        for rep in range(a.reps):
            for cond in ("rest", "splash", "rest_poll", "splash_poll"):
                out["takes"].append(take(api, cond, a.seconds))
    finally:
        destroy_all(api)
        api.debug("water_av_record_draw", {"radius": draw0})
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
