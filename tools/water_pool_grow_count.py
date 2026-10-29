"""WaterCore 44.3 item 2 - how often a pond's brick pool grows in play (each grow re-creates and copies every capacity buffer,
11-12 ms in one frame, 44.1). Measured before choosing a fix.

The Small bench (port 8111), Release, the 4 x 4 m pond of water_float_rest; displacement ON. Three 1/3 m Wood blocks and a Stone
dropped 1 m every 2.5 s (8 s lifetime) for --seconds. Two runs: auto-sleep OFF (the pond stays one awake volume) and ON (as shipped:
it sleeps into the record between bursts and wakes again - on kept memory, L3). Sampled every 0.25 s: the engine's grow count
(water_coupling slept.last_grow_split_ms.grows, cumulative) and the worst frame since the last sample.

    python tools/water_pool_grow_count.py [--seconds 60] [--tag pool_grow]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, random, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_float_rest as w


def grows(api):
    return ((api.debug("water_coupling", {}).get("slept") or {}).get("last_grow_split_ms") or {}).get("grows", 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--tag", default="pool_grow")
    a = ap.parse_args()
    api = Api(a.url); st = api.wait_status(300)
    d0 = api.debug("water_coupling", {}).get("displace")
    out = {"build": st.get("build_config"), "runs": []}
    try:
        for auto in (False, True):
            w.AUTO_SLEEP = auto; w.PRESSURE = True; w.DISPLACE = True
            w.pond(api); time.sleep(3.0)
            g0 = grows(api); api.get("/api/debug/engine_timing")
            rng = random.Random(5); t0 = time.time(); nxt = t0; rows = []; drops = 0
            while time.time() - t0 < a.seconds:
                if time.time() >= nxt:
                    for mat in ("Wood", "Wood", "Wood", "Stone"):
                        api.debug("spawn_gpu_particle", {"x": 100.4 + rng.random() * 3.2, "y": w.LEVEL + 1.17, "z": 12.4 + rng.random() * 3.2,
                                                         "material": mat, "scale": 1 / 3, "lifetime": 8.0, "spin": 0})
                    nxt += 2.5; drops += 1
                et = api.get("/api/debug/engine_timing")
                rows.append({"t": round(time.time() - t0, 2), "grows": grows(api) - g0, "frame_ms_max": et.get("frame_ms_max_since_last_read"),
                             "volumes": len(api.debug("water_av_list").get("volumes", []))})
                time.sleep(0.25)
            g = [r for r in rows if r["grows"] > (rows[rows.index(r) - 1]["grows"] if rows.index(r) else 0)]
            run = {"auto_sleep": auto, "seconds": a.seconds, "bursts": drops, "grows": rows[-1]["grows"],
                   "grow_times_s": [r["t"] for r in g], "frames_over_10ms": sum(1 for r in rows if (r["frame_ms_max"] or 0) > 10.0),
                   "worst_frame_ms": max((r["frame_ms_max"] or 0) for r in rows), "rows": rows}
            print(f"auto-sleep {auto}: {drops} bursts in {a.seconds:.0f} s -> {run['grows']} pool grows at {run['grow_times_s']} s; "
                  f"samples with a frame over 10 ms: {run['frames_over_10ms']}, worst {run['worst_frame_ms']:.1f} ms", flush=True)
            out["runs"].append(run)
            api.debug("clear_dynamics")
    finally:
        api.debug("clear_dynamics")
        if d0 is not None: api.debug("water_coupling", {"displace": d0})
        (w.EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
