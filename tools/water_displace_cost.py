"""WaterCore 34 / 43.6 - debris displacement on vs off, WHOLE FRAME, before the owner decides its default (2026-10-29: the owner
preferred the displacement-on half of a2h_look_side_by_side.mp4; "On, but measure first").

The Small bench (port 8111), Release. Two scenes, each condition alternated on / off / off / on / on / off, a fresh scene each:
  pond  the 4 x 4 m pond of water_float_rest (1.5 m deep, auto-sleep off), the camera as a2h's look: three 1/3 m Wood blocks
        and one 1/3 m Stone dropped 1 m at seeded points every 2.5 s (lifetime 8 s, so the pond keeps a few bodies and stays
        stirred - a play-like scene, not a worst case), a --seconds window;
  pit   water_a2g_safe's stress: 1,100 1/9 m Wood blocks into the 8 x 8 m S9 pit (the worst case), its own window.
Per window: fps mean, the CPU frame median (engine_timing cpuFrameTime) and its worst frame, the GPU frame median and max
(gpu_timing), and the water volumes' device ms at the end (water_av_list gpu_device_ms, one frame). The pit also asserts its own
invariants (no block above its spawn height; rasterized <= windows at every sample).
Not measured: a creek with debris; many ponds at once.

    python tools/water_displace_cost.py [--scenes pond,pit] [--seconds 12] [--tag displace_cost]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, random, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_float_rest as w
import water_a2g_safe as g

ORDER = ["on", "off", "off", "on", "on", "off"]


def frame_window(api, seconds, drops=None):
    fps, cpu, fmax = [], [], []
    api.get("/api/debug/engine_timing")   # clears the worst-frame tracker
    t0 = time.time(); nxt = t0; k = 0
    while time.time() - t0 < seconds:
        if drops and time.time() >= nxt:
            drops(k); k += 1; nxt += 2.5
        et = api.get("/api/debug/engine_timing"); fps.append(et.get("fps", 0)); cpu.append(et.get("cpuFrameTime", 0))
        if et.get("frame_ms_max_since_last_read") is not None: fmax.append(et["frame_ms_max_since_last_read"])
        time.sleep(0.05)
    gt = api.get("/api/debug/gpu_timing").get("gpu_frame_ms", {})
    vols = api.debug("water_av_list").get("volumes", [])
    return {"fps_mean": round(statistics.mean(fps), 1), "cpu_frame_ms_median": round(statistics.median(cpu), 3),
            "frame_ms_max": round(max(fmax), 2) if fmax else None,
            "gpu_frame_ms_median": round(gt.get("median_ms", 0.0), 3), "gpu_frame_ms_max": round(gt.get("max_ms", 0.0), 3),
            "water_gpu_device_ms_end": round(sum(v.get("gpu_device_ms") or 0.0 for v in vols), 3)}


def pond_window(api, displace, seconds):
    w.AUTO_SLEEP = False; w.PRESSURE = True; w.DISPLACE = displace
    w.pond(api); time.sleep(3.0)
    got = api.debug("water_coupling", {}).get("displace")
    if got is not displace: raise SystemExit(f"displace not taken: {got} (wanted {displace})")
    rng = random.Random(5)
    def drops(k):
        for mat in ("Wood", "Wood", "Wood", "Stone"):
            api.debug("spawn_gpu_particle", {"x": 100.4 + rng.random() * 3.2, "y": w.LEVEL + 1.0 + 1 / 6, "z": 12.4 + rng.random() * 3.2,
                                             "material": mat, "scale": 1 / 3, "lifetime": 8.0, "spin": 0})
    api.debug("water_av_host_phases", {"reset": True})
    r = frame_window(api, seconds, drops)
    hp = api.debug("water_av_host_phases", {})
    ph = hp.get("phases", {})
    # 44.1: where the cost is - the manager's phases (ms a frame, the longest call) and the editor's water block at its worst frame
    r["phases_top"] = sorted(([k, round(v["ms_per_frame"], 3), round(v["max_ms_per_call"], 2)] for k, v in ph.items()), key=lambda e: -e[1])[:14]
    r["phases_worst_call"] = sorted(([k, round(v["max_ms_per_call"], 2)] for k, v in ph.items()), key=lambda e: -e[1])[:8]
    r["app_water_worst_frame"] = hp.get("app_water_cpu_ms_worst_frame")
    api.debug("clear_dynamics")
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--scenes", default="pond,pit")
    ap.add_argument("--seconds", type=float, default=12.0)
    ap.add_argument("--tag", default="displace_cost")
    a = ap.parse_args()
    api = Api(a.url); st = api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    d0 = api.debug("water_coupling", {}).get("displace")
    out = {"build": st.get("build_config"), "order": ORDER, "rows": []}
    path = w.EV / f"{a.tag}.json"
    try:
        for scene in a.scenes.split(","):
            for c in ORDER:
                on = c == "on"
                if scene == "pond":
                    api.post("/api/camera", {"mode": "free", "position": {"x": 102.0, "y": 18.4, "z": 20.5}, "yaw": -90, "pitch": -22})
                    r = pond_window(api, on, a.seconds)
                else:
                    api.post("/api/camera", {"mode": "free", "position": {"x": 102.0, "y": 20.0, "z": 8.0}, "yaw": 90, "pitch": -40})
                    w.DISPLACE = on
                    spots = [(151.0 + 1.6 * i, 11.0 + 1.6 * j) for j in range(3) for i in range(4)][:11]
                    s = g.stress(api, a.seconds, 100, spots)
                    gt = api.get("/api/debug/gpu_timing").get("gpu_frame_ms", {})
                    r = {"fps_mean": round(s["fps_mean"], 1), "fps_min": s["fps_min"], "cpu_frame_ms_mean": round(s["cpu_ms_mean"], 3),
                         "gpu_frame_ms_median": round(gt.get("median_ms", 0.0), 3), "gpu_frame_ms_max": round(gt.get("max_ms", 0.0), 3),
                         "max_rise_above_spawn_m": s["max_rise_above_spawn_m"], "rasterized_le_windows": s["refused_exact"],
                         "bodies": s["bodies"]}
                    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})
                row = {"scene": scene, "displace": c, **r}
                print(row, flush=True); out["rows"].append(row)
                path.write_text(json.dumps(out, indent=1), encoding="utf-8")
    finally:
        api.debug("clear_dynamics")
        if d0 is not None: api.debug("water_coupling", {"displace": d0})
    summ = {}
    for scene in a.scenes.split(","):
        for c in ("on", "off"):
            rs = [r for r in out["rows"] if r["scene"] == scene and r["displace"] == c]
            summ[f"{scene}_{c}"] = {k: [r[k] for r in rs] for k in rs[0] if k not in ("scene", "displace")} if rs else {}
    out["summary"] = summ
    path.write_text(json.dumps(out, indent=1), encoding="utf-8")
    print(json.dumps(summ, indent=1))


if __name__ == "__main__":
    main()
