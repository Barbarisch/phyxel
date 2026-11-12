"""WaterCore.md 51.8 - where the water's CPU and GPU go after a splash near the camera. The 150 m lake in the record
(water_record_lake_clip.py's rig, WaterBench_Creek), the camera at a pose, a splash at the lake's centre, then the water's host phases
(water_av_host_phases: ms a frame per phase, worst frame) and the water GPU call's stages (water_av_kernel_timing: ms a call per
stage) over the window after it. Prints the top phases and stages.

    python tools/water_splash_phases.py [--pose close|wide] [--window-s 8] [--tag splash_phases_close]
        [--water-viewer wide]        the water's viewer (water_locality test_viewer) at another pose, the camera unmoved: does the
                                     extra cost follow the water's viewer (its locality) or the camera (the frame's rendering)?
        [--seed-whole-map]           51.9: tile seeds by the old path (the live A/B; the same device state)
        [--line-sweeps N] [--tall-sweeps N] [--quiet-scale S] [--rest-ticks N]   knobs for the run (restored after)
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_record_lake_clip import EV, POSES, lay_lake, splash, destroy_all


def top(d, n=12):
    rows = sorted(((v.get("ms_per_frame", 0.0) if isinstance(v, dict) else v, k) for k, v in d.items()), reverse=True)
    return rows[:n]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--pose", default="close")
    ap.add_argument("--water-viewer", default=None)
    ap.add_argument("--window-s", type=float, default=8.0)
    ap.add_argument("--line-sweeps", type=int, default=None)
    ap.add_argument("--tall-sweeps", type=int, default=None)
    ap.add_argument("--quiet-scale", type=float, default=None)
    ap.add_argument("--rest-ticks", type=int, default=None)
    ap.add_argument("--seed-whole-map", action="store_true", help="51.9 A/B: tile seeds by the path before 51.9 (TEST ONLY switch, restored after)")
    ap.add_argument("--tag", default="splash_phases_close")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    api.debug("water_av_realtime", {"on": True})
    draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
    api.debug("water_av_record_draw", {"radius": 48.0})
    ls0, tall0, pw0 = api.debug("water_av_line_sweeps", {}), api.debug("water_av_tall", {}), api.debug("water_av_partial_wake", {})
    knobs = {}
    if a.line_sweeps is not None: knobs["line_sweeps"] = api.debug("water_av_line_sweeps", {"count": a.line_sweeps})
    if a.tall_sweeps is not None: knobs["tall"] = api.debug("water_av_tall", {"sweeps": a.tall_sweeps})
    pw = {k: v for k, v in (("quiet_scale", a.quiet_scale), ("rest_ticks", a.rest_ticks)) if v is not None}
    if pw: knobs["partial_wake"] = api.debug("water_av_partial_wake", pw)
    knobs["seed_whole_map_test"] = api.debug("water_av_record_tiles", {"seed_whole_map_test": a.seed_whole_map}).get("seed_whole_map_test")
    out = {"pose": POSES[a.pose], "window_s": a.window_s, "knobs": knobs, "defaults": {"line_sweeps": ls0, "tall": tall0, "partial_wake": pw0}}
    if a.water_viewer:
        wv = POSES[a.water_viewer]
        out["water_viewer"] = api.debug("water_locality", {"test_viewer": [wv["x"], wv["y"], wv["z"]]})
    try:
        out["record"] = lay_lake(api)
        camera_set(api, POSES[a.pose]); time.sleep(6.0)
        api.debug("water_av_host_phases", {"reset": True}); time.sleep(a.window_s)
        out["rest"] = api.debug("water_av_host_phases", {})
        api.debug("water_av_record_tiles", {"event_log": True, "phases_reset": True})
        out["splash"] = splash(api)
        api.debug("water_av_host_phases", {"reset": True})
        api.debug("water_av_kernel_timing", {"on": True, "reset": True})
        time.sleep(a.window_s)
        out["after"] = api.debug("water_av_host_phases", {})
        out["kernels"] = api.debug("water_av_kernel_timing", {})
        api.debug("water_av_kernel_timing", {"on": False})
        tiles = api.debug("water_av_record_tiles", {"events_take": True})
        api.debug("water_av_record_tiles", {"event_log": False})
        out["tile_events"], out["tile_phases"] = tiles.get("events", []), tiles.get("phases", {})
        out["droplets"] = api.debug("water_droplets", {})   # 51.11: cosmetic - born / landed are the volume shown
        print("droplets:", {k: out["droplets"].get(k) for k in ("births", "landings", "born_m3", "alive", "in_flight_m3")}, flush=True)
        ev = out["tile_events"]
        print(f"tile events in the window: {len(ev)} (" + ", ".join(f"{c} {sum(1 for e in ev if e['cause'] == c)}" for c in sorted({e['cause'] for e in ev})) + ")", flush=True)
        for e in sorted(ev, key=lambda e: -e.get("ms", 0))[:8]: print(f"    t {e['t']:.2f}  {e['cause']}  tile ({e['tx']},{e['tz']})  {e.get('ms', 0):.1f} ms", flush=True)
        tp = out["tile_phases"]
        if tp: print("  wake phases max (ms):", dict(zip(tp.get("wake_names", []), [round(x, 1) for x in tp.get("wake_max", [])])), "wakes", tp.get("wakes"), flush=True)
        for k in ("rest", "after"):
            print(f"{k}: {out[k].get('frames')} frames; top phases (ms a frame):", flush=True)
            for v, n in top(out[k].get("phases", {})): print(f"    {v:8.3f}  {n}", flush=True)
            print("  app water cpu (last frame):", out[k].get("app_water_cpu_ms_last"), flush=True)
        kt = out["kernels"]
        print(f"GPU call: {kt.get('total_ms_per_call', 0):.2f} ms over {kt.get('calls')} calls; stages:", flush=True)
        for n, v in sorted(kt.get("ms_per_call", {}).items(), key=lambda e: -e[1])[:14]: print(f"    {v:8.3f}  {n}", flush=True)
        time.sleep(30.0)
    finally:
        destroy_all(api)
        api.debug("water_av_record_tiles", {"seed_whole_map_test": False})
        api.debug("water_av_record_draw", {"radius": draw0})
        api.debug("water_av_line_sweeps", {"count": ls0.get("count", 0)})
        api.debug("water_av_tall", {"sweeps": tall0.get("sweeps", 20)})
        api.debug("water_av_partial_wake", {"quiet_scale": pw0.get("quiet_scale", 10.0), "rest_ticks": pw0.get("rest_ticks", 0)})
        if a.water_viewer: api.debug("water_locality", {"clear_test_viewer": True})
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
        print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
