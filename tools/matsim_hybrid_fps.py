"""MaterialSimPlan 6i - what the hybrid costs LIVE: the lab box in realtime mode (the game loop steps it, up to
4 ticks a frame, the rest dropped), the raw-cell view on, the video's camera. Samples every 0.5 s of wall time:
the engine's fps and frame ms, the sim ticks advanced (sim speed = ticks / (60 * wall s); 1.0 = real time),
the ticks dropped, particles and the lab's per-stage tick ms. Rig: the hand-placed MatSimLab project (port 8120).

    python tools/matsim_hybrid_fps.py --cpm 3 [--seconds 40]
Writes docs/evidence/matsim_video/fps_<cpm>.json and prints a summary.
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import matsim_lab as L
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "matsim_video"


def timing(api):
    t = api.get("/api/debug/engine_timing")
    return t.get("fps"), t.get("frame_ms_max_since_last_read")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8120")
    ap.add_argument("--cpm", type=int, default=3)
    ap.add_argument("--seconds", type=float, default=40.0, help="wall seconds sampled after the dam is removed")
    ap.add_argument("--pose", default="0,38,16,0,-60")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(120)
    p = [float(v) for v in a.pose.split(",")]
    camera_set(api, {"x": p[0], "y": p[1], "z": p[2], "yaw": p[3], "pitch": p[4]})
    time.sleep(2.0); timing(api); time.sleep(2.0)
    base_fps, _ = timing(api)   # control: the same view with no lab box
    L.fill(api, L.DAM); L.wait_occupancy()
    L.must(L.ms(api, "create", {"min": L.BOX_MIN, "size": L.BOX_SIZE, "cells_per_m": a.cpm, "particles": "hybrid"}), "create")
    L.must(L.ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 8}, "size": {"x": 3, "y": 3, "z": 8}, "fraction": 1.0}), "fill")
    L.must(L.ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 17}, "size": {"x": 8, "y": 5, "z": 7}, "fraction": 1.0}), "fill")
    L.must(L.ms(api, "view", {"on": True}), "view")
    world_job(api, "/api/world/clear", L.DAM); L.wait_occupancy()
    L.must(L.ms(api, "resolidify"), "resolidify")
    L.must(L.ms(api, "realtime", {"on": True}), "realtime")
    rows = []
    t0 = time.time(); timing(api)
    st0 = L.ms(api, "stats")
    prev_t, prev_tick = time.time(), st0["tick"]
    while time.time() - t0 < a.seconds:
        time.sleep(0.5)
        fps, fmax = timing(api)
        st = L.ms(api, "stats")
        now = time.time()
        rows.append({"wall_s": round(now - t0, 2), "fps": fps, "frame_ms_max": fmax, "tick": st["tick"],
                     "sim_speed": (st["tick"] - prev_tick) / (60.0 * (now - prev_t)), "particles": st.get("particles", 0),
                     "dropped": st.get("realtime_ticks_dropped"), "view_faces": st.get("view_faces"),
                     "view_faces_cut": st.get("view_faces_cut"), "tick_ms": st.get("last_tick_ms"), "ledger_exact": st["ledger"]["exact"]})
        prev_t, prev_tick = now, st["tick"]
        print(rows[-1], flush=True)
    L.ms(api, "realtime", {"on": False}); L.ms(api, "view", {"on": False})
    L.fill(api, L.DAM)
    busy = [r for r in rows if r["particles"] > 0]
    summ = {"cpm": a.cpm, "control_fps_no_box": base_fps,
            "busy_samples": len(busy),
            "fps_median_busy": statistics.median(r["fps"] for r in busy) if busy else None,
            "fps_min_busy": min(r["fps"] for r in busy) if busy else None,
            "sim_speed_median_busy": statistics.median(r["sim_speed"] for r in busy) if busy else None,
            "fps_median_settled": statistics.median(r["fps"] for r in rows if r["particles"] == 0) if len(busy) < len(rows) else None,
            "sim_s_reached": round((rows[-1]["tick"] - st0["tick"]) / 60.0, 2), "wall_s": a.seconds,
            "ledger_exact_every_sample": all(r["ledger_exact"] for r in rows)}
    EV.mkdir(parents=True, exist_ok=True)
    (EV / f"fps_{a.cpm}.json").write_text(json.dumps({"summary": summ, "rows": rows}, indent=1))
    print(json.dumps(summ, indent=1))


if __name__ == "__main__":
    main()
