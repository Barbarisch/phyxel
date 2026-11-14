"""D9 diagnosis: which polled call makes the ~80 ms frames after a splash, and where in the frame row its time shows."""
import json, sys, threading, time, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_record_lake_clip import lay_lake, splash, destroy_all, POSES
import water_splash_frames as W

def poller(api, stop, which):
    while not stop.is_set():
        try:
            if which in ("list", "both"): api.debug("water_av_list", {})
            if which in ("timing", "both"): api.get("/api/debug/engine_timing")
        except Exception: pass
        time.sleep(0.25)

def take(api, which, seconds=8.0):
    lay_lake(api); camera_set(api, POSES["close"]); time.sleep(5.0)
    stop = threading.Event()
    th = threading.Thread(target=poller, args=(Api(api.url), stop, which), daemon=True) if which != "none" else None
    f0 = W.mark(api)
    if th: th.start()
    time.sleep(1.0)
    splash(api)
    time.sleep(seconds)
    stop.set()
    if th: th.join()
    rows = W.frames_since(api, f0)
    t = 0.0; out = []
    for i, r in enumerate(rows):
        if r["dt_ms"] > 33.3 and t > 2.0:   # past the splash burst
            p = rows[i - 1] if i else r
            out.append({"t": round(t, 2), "dt": round(r["dt_ms"], 1), "api": round(r["api_ms"], 1), "api_prev": round(p["api_ms"], 1),
                        "water": round(r["water_ms"], 1), "water_prev": round(p["water_ms"], 1)})
        t += r["dt_ms"] / 1000.0
    print(f"{which:7s} late slow frames {len(out)}", flush=True)
    for o in out[:8]: print("   ", o)
    return {"which": which, "late_slow": out}

api = Api("http://127.0.0.1:8114"); api.wait_status(60)
api.debug("water_av_realtime", {"on": True})
draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
api.debug("water_av_record_draw", {"radius": 200.0})
res = []
try:
    for which in ("list", "timing", "both", "none"):
        res.append(take(api, which))
finally:
    destroy_all(api); api.debug("water_av_record_draw", {"radius": draw0})
(W.EV / "d9_split_poll.json").write_text(json.dumps(res, indent=1))
