"""WaterCore.md 51.8 - where the water's CPU goes after a splash near the camera. The 150 m lake in the record
(water_record_lake_clip.py's rig, WaterBench_Creek), the camera at a pose, a splash at the lake's centre, then the water's host phases
(water_av_host_phases: ms a frame per phase, worst frame) over the window after it. Prints the top phases.

    python tools/water_splash_phases.py [--pose close|wide] [--window-s 8] [--tag splash_phases_close]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_record_lake_clip import EV, POSES, lay_lake, splash, destroy_all


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--pose", default="close")
    ap.add_argument("--window-s", type=float, default=8.0)
    ap.add_argument("--tag", default="splash_phases_close")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    api.debug("water_av_realtime", {"on": True})
    draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
    api.debug("water_av_record_draw", {"radius": 48.0})
    out = {"pose": POSES[a.pose], "window_s": a.window_s}
    try:
        out["record"] = lay_lake(api)
        camera_set(api, POSES[a.pose]); time.sleep(6.0)
        api.debug("water_av_host_phases", {"reset": True}); time.sleep(a.window_s)
        out["rest"] = api.debug("water_av_host_phases", {})
        out["splash"] = splash(api)
        api.debug("water_av_host_phases", {"reset": True}); time.sleep(a.window_s)
        out["after"] = api.debug("water_av_host_phases", {})
        for k in ("rest", "after"):
            ph = out[k].get("phases", {})
            rows = sorted(((v.get("ms_per_frame", 0.0) if isinstance(v, dict) else v, n) for n, v in ph.items()), reverse=True)
            print(f"{k}: {out[k].get('frames')} frames; top phases (ms a frame):", flush=True)
            for v, n in rows[:12]: print(f"    {v:8.3f}  {n}", flush=True)
            print("  app water cpu (last frame):", out[k].get("app_water_cpu_ms_last"), flush=True)
        time.sleep(30.0)
    finally:
        destroy_all(api)
        api.debug("water_av_record_draw", {"radius": draw0})
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
        print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
