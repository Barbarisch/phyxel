"""WaterCore §58 item 1 - 52.5's periodic freeze: "at 80 s and 120 s, the camera still, the frame rate fell to ~0.6 fps (> 1 s) as
record_pending jumped to 131 (every tile queued at once). Cause not looked at." From a fresh launch of WaterBench_RiverCarve at the
W4 clip's pose (tools/water_w4_launch_fill.py's), every second for --seconds: the worst frame since the last read, the record draw's
pending / tiles, and the worst call of each water host phase in that second (water_av_host_phases, reset each read). A second whose
worst frame passes --slow ms is printed with its top phases.
    python tools/water_freeze_probe.py [--seconds 160] [--slow 100] [--out docs/evidence/water_core_m4/s58_freeze_probe.json]
(start WaterBench_RiverCarve first - `phyxel up`, port 8113)."""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

POSE = {"x": -2224.0, "y": 93.0, "z": -1349.5, "yaw": 180.0, "pitch": -12.0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--seconds", type=int, default=160)
    ap.add_argument("--slow", type=float, default=100.0)
    ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4" / "s58_freeze_probe.json"))
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(600)
    t0 = time.time()
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    camera_set(api, POSE)
    api.debug("water_av_host_phases", {"reset": True}); api.get("/api/debug/engine_timing")
    # 58.3: the engine's frame recorder (PerfApi I15: every main-loop scope a frame) - the multi-second frames are not the water's
    try: print("record:", api.post("/api/debug/record", {"start": True, "max_frames": 65536}), flush=True)
    except Exception as e: print("record start:", e, flush=True)
    rows = []
    for k in range(a.seconds):
        time.sleep(max(0.0, (k + 1) * 1.0 - (time.time() - t0)))
        et = api.get("/api/debug/engine_timing")
        rd = api.debug("water_av_record_draw", {})
        hp = api.debug("water_av_host_phases", {"reset": True})
        ph = hp.get("phases", {})
        top = sorted(((v.get("max_ms_per_call", 0.0), n) for n, v in ph.items()), reverse=True)[:8]
        row = {"t": round(time.time() - t0, 1), "frame_ms_max": et.get("frame_ms_max_since_last_read"), "fps": et.get("fps"),
               "pending": rd.get("pending"), "tiles": rd.get("tiles"), "wet": rd.get("wet_tiles"), "built_last": rd.get("built_last_call"),
               "occ_staled_by_place": rd.get("occ_staled_by_place"), "occ_all_stale": rd.get("occ_all_stale"),
               "top": [[round(v, 2), n] for v, n in top], "worst_water_block": hp.get("app_water_cpu_ms_worst_frame", {}).get("total")}
        rows.append(row)
        if (row["frame_ms_max"] or 0) >= a.slow or k % 10 == 9:
            print(json.dumps({kk: row[kk] for kk in ("t", "frame_ms_max", "fps", "pending", "tiles", "wet", "worst_water_block", "occ_staled_by_place", "occ_all_stale")}), "|", "; ".join(f"{n} {v}" for v, n in row["top"][:5]), flush=True)
    slow = []
    try:
        api.post("/api/debug/record", {"stop": True})
        k0 = 0
        while True:
            pg = api.get(f"/api/debug/record?from={k0}&count=2048")
            fr = pg.get("rows", pg.get("frames", []))
            if not fr: break
            keys = pg.get("phase_keys", [])
            for row in fr:
                ph = row.get("phases") or []
                named = {keys[i]: round(v, 2) for i, v in enumerate(ph) if i < len(keys) and isinstance(v, (int, float)) and v >= 5.0}
                dt = row.get("frame_ms", row.get("dt_ms", row.get("cpu_ms")))
                if (isinstance(dt, (int, float)) and dt >= a.slow) or any(v >= a.slow for v in named.values()):
                    slow.append({"row": k0, "frame": {kk: v for kk, v in row.items() if kk not in ("phases", "gpu")}, "phases_over_5ms": named})
                k0 += 1
            if len(fr) < 2048: break
        print(f"recorder: {k0} frames, {len(slow)} slow", flush=True)
        for s in slow[:40]: print("  ", json.dumps(s)[:400], flush=True)
    except Exception as e: print("record dump:", e, flush=True)
    pathlib.Path(a.out).write_text(json.dumps({"pose": POSE, "rows": rows, "recorder_slow": slow}, indent=1), encoding="utf-8")
    print("wrote", a.out)


if __name__ == "__main__":
    main()
