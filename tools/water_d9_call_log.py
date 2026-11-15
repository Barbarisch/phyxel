"""WaterCore.md §53 (D9 bounded) - what the ONE long water call after a splash spends its time on. D9 measured a single fence wait of
79-129 ms in one water GPU call right after the splash (d9_phases_r1/_r2.json); the GPU kernel timing was a mean over single-tick
calls only, so which stage it is was not known.

This turns on the engine's per-call log (water_av_call_log: every collected call - the host's fence wait, submit-to-signal, the
device's own time, ticks / substeps / bricks / dispatches and device ms PER STAGE; the stamps are armed on every call while on) and
runs D9's rig: WaterBench_Creek (port 8114), the 150 m record lake, the close pose, the clip's splash (2 m, strength 8). Each rep:
lay the lake, 5 s settle, 1 s rest, the splash, --seconds after; the log and the frame trace are read only AFTER the window.
Arms, interleaved: log ON (the split) and log OFF (the control: the stamps add a barrier each, so does the burst change with them?).

Prediction (written before the run): if the long wait is the GPU's own work, the long call's device_ms ~ its wait and one stage
(or the tick count) holds most of it; if submit_to_signal >> device_ms the call queued behind other GPU work (the frame's rendering).

    python tools/water_d9_call_log.py [--seconds 4] [--reps 3] [--tag d9_call_log]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_record_lake_clip import lay_lake, splash, destroy_all, POSES
from water_splash_frames import mark, frames_since, summarize

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"


def take(api, log_on, seconds):
    lay_lake(api)
    camera_set(api, POSES["close"])
    time.sleep(5.0)
    api.debug("water_av_call_log", {"on": log_on, "reset": True})
    f0 = mark(api)
    time.sleep(1.0)
    t_splash0 = api.debug("water_av_call_log", {"since": 1 << 40})["now_ms"]
    k = splash(api)
    time.sleep(seconds)
    log = api.debug("water_av_call_log", {"on": False})
    rows = frames_since(api, f0)
    s = summarize(rows)
    calls = log["rows"]
    for c in calls: c["t_rel_s"] = round((c["t_ms"] - t_splash0) / 1000.0, 3)
    longest = sorted(calls, key=lambda c: -c["wait_ms"])[:6]
    s.update({"log_on": log_on, "splash": k, "calls": len(calls), "longest_calls": longest,
              "call_rows": [{kk: c[kk] for kk in ("t_rel_s", "wait_ms", "submit_to_signal_ms", "in_flight_ms", "device_ms", "ticks", "substeps", "bricks", "dispatches")}
                            for c in calls]})
    print(f"  log {'ON ' if log_on else 'OFF'} frames {s['frames']:5d} fps {s['fps_mean']:6.1f} max {s['dt_max']:7.2f} >33ms {s['over_33ms']:3d}"
          f" >50ms {s['over_50ms']:3d} calls {len(calls)}", flush=True)
    for c in longest[:4]:
        stg = sorted(c["stage_ms"].items(), key=lambda kv: -kv[1])[:5]
        print(f"      t {c['t_rel_s']:+.2f}s wait {c['wait_ms']:6.1f} sub->sig {c['submit_to_signal_ms']:6.1f} inflight {c['in_flight_ms']:5.1f}"
              f" device {c['device_ms']:6.1f} ticks {c['ticks']} sub {c['substeps']} bricks {c['bricks']} disp {c['dispatches']}"
              f"  {', '.join(f'{n} {v:.1f}' for n, v in stg)}", flush=True)
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--seconds", type=float, default=4.0)
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--tag", default="d9_call_log")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    api.debug("water_av_realtime", {"on": True})
    draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
    api.debug("water_av_record_draw", {"radius": 200.0})
    out = {"rig": "WaterBench_Creek, the 150 m record lake, the clip's close pose and splash (D9's rig)", "seconds": a.seconds, "takes": []}
    try:
        for rep in range(a.reps):
            for on in (True, False):
                out["takes"].append(take(api, on, a.seconds))
    finally:
        try: api.debug("water_av_call_log", {"on": False, "reset": True})
        except Exception: pass
        destroy_all(api)
        api.debug("water_av_record_draw", {"radius": draw0})
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
