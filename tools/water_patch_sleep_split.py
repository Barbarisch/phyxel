"""WaterCore 57.7 - the frame a lake's patch goes back to the record, split by part (the sleep.* laps in WaterCoreManager::sleep).
57.6 found it: 255-264 ms, in update, outside every timed lap. WaterBench_Creek (port 8114), the 57.6 lake: 60 x 60 m, 2 m deep,
in the record; the visible stone (2 m / strength 8) at its centre; the player off the lake; then wait for the patch to sleep
(only the phase timers and engine_timing polled - never water_av_list while it matters, D9) and read each part's longest call.

    python tools/water_patch_sleep_split.py [--url http://127.0.0.1:8114] [--runs 2] [--out ...json]
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_play_scene as P


def one(api, k):
    api.post("/api/entity/move", {"id": "player", "position": {"x": 2.0, "y": P.TOP + 1.2, "z": 0.0}})
    P.destroy_all(api)
    time.sleep(0.5)
    rl = api.debug("water_av_record_lake", {"x1": P.X0, "z1": P.Z0, "x2": P.X0 + P.W - 1, "z2": P.Z0 + P.W - 1, "bottom": P.TOP, "top": P.TOP + 2.0})
    camera_set(api, P.CHURN_CAM)
    time.sleep(4.0)
    api.debug("water_av_host_phases", {"reset": True})
    api.get("/api/debug/engine_timing")
    st = api.debug("water_av_impulse", {"x": P.X0 + P.W / 2, "y": P.TOP + 1.9, "z": P.Z0 + P.W / 2, "radius": 2.0, "strength": 8.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    t0, worst, slept_at = time.time(), [], None
    while time.time() - t0 < 45:
        et = api.get("/api/debug/engine_timing")
        worst.append((round(time.time() - t0, 1), et.get("frame_ms_max_since_last_read")))
        ph = api.debug("water_av_host_phases", {}).get("phases", {})
        if slept_at is None and "sleep.9 build the record tiles" in ph: slept_at = round(time.time() - t0, 1)
        if slept_at is not None and time.time() - t0 > slept_at + 3: break
        time.sleep(0.5)
    ph = api.debug("water_av_host_phases", {})
    parts = {n: round(v.get("max_ms_per_call", 0.0), 2) for n, v in ph.get("phases", {}).items() if n.startswith(("sleep.", "tilesNow."))}
    big = max(worst, key=lambda r: r[1] or 0)
    r = {"run": k, "record_lake": rl, "stone": st, "slept_at_s": slept_at, "sleep_parts_ms": dict(sorted(parts.items())),
         "sleep_total_ms": round(sum(v for n, v in parts.items() if n.startswith("sleep.")), 2), "worst_frame": big, "worst_frame_ms_by_read": worst,
         "foreign_gpu_processes": P.foreign()}
    print(f"run {k}: slept at {slept_at} s, worst frame {big}, sleep parts {r['sleep_parts_ms']} (sum {r['sleep_total_ms']} ms)", flush=True)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--runs", type=int, default=2)
    ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4" / "s57_patch_sleep_split.json"))
    a = ap.parse_args()
    if P.foreign(): raise SystemExit(f"other phyxel processes share the GPU: {P.foreign()}")
    api = Api(a.url); api.wait_status(300)
    if "error" in P.ps(api):
        api.post("/api/entity/spawn", {"type": "animated", "position": {"x": 2.0, "y": P.TOP + 1.2, "z": 0.0}, "id": "player"})
    api.debug("water_av_realtime", {"on": True})
    out = []
    try:
        P.destroy_all(api); P.clear_record(api); P.lay_bed(api)
        time.sleep(30.0)   # the far skin's re-read of the clear (57.4 item 2) done before the first run
        for k in range(a.runs): out.append(one(api, k))
    finally:
        P.destroy_all(api)
        pathlib.Path(a.out).write_text(json.dumps(out, indent=1), encoding="utf-8")
        print("wrote", a.out)


if __name__ == "__main__":
    main()
