"""D9 diagnosis: is the burst after a splash the fixed tick's catch-up (up to kMaxTicksPerFrame ticks in the call after a long frame)?
A/B interleaved: fixed tick default (1/60) vs 0 (one tick of the frame's dt - DIAGNOSTIC ONLY, restored after). Frame rows from the trace."""
import json, sys, time, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_record_lake_clip import lay_lake, splash, destroy_all, POSES
import water_splash_frames as W

def take(api, tick, seconds=6.0):
    api.debug("water_av_fixed_tick", {"seconds": tick})
    lay_lake(api); camera_set(api, POSES["close"]); time.sleep(5.0)
    d0 = api.debug("water_av_fixed_tick", {}).get("ticks_dropped")
    f0 = W.mark(api); time.sleep(1.0)
    splash(api); time.sleep(seconds)
    rows = W.frames_since(api, f0)
    d1 = api.debug("water_av_fixed_tick", {}).get("ticks_dropped")
    # water/api are recorded one row before the dt that pays for them: pair row i's dt with row i-1's water/api
    burst = []; t = 0.0
    for i in range(1, len(rows)):
        t += rows[i]["dt_ms"] / 1000.0
        if rows[i]["dt_ms"] > 33.3: burst.append((round(t, 2), round(rows[i]["dt_ms"], 1), round(rows[i-1]["water_ms"], 1), round(rows[i-1]["api_ms"], 1)))
    s = W.summarize(rows)
    over = sum(b[1] for b in burst)
    print(f"tick {tick!s:8s} frames>33ms {len(burst):2d}  ms in them {over:6.0f}  fps {s['fps_mean']}  p99 {s['dt_p99']}  dropped {d1 - d0}")
    print("    (t, dt, water, api):", burst[:12])
    return {"tick": tick, "burst": burst, "ticks_dropped": d1 - d0, "summary": {k: v for k, v in s.items() if k != "slow_frames"}}

api = Api("http://127.0.0.1:8114"); api.wait_status(60)
api.debug("water_av_realtime", {"on": True})
tick0 = api.debug("water_av_fixed_tick", {})["seconds"]
draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
api.debug("water_av_record_draw", {"radius": 200.0})
res = []
try:
    for rep in range(2):
        for tick in (tick0, 0.0):
            res.append(take(api, tick))
finally:
    api.debug("water_av_fixed_tick", {"seconds": tick0})
    destroy_all(api); api.debug("water_av_record_draw", {"radius": draw0})
(W.EV / "d9_tick_ab.json").write_text(json.dumps(res, indent=1))
print("restored tick", api.debug("water_av_fixed_tick", {})["seconds"])
