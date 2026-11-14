"""WaterCore.md 51.16 live (L4): the far skin's generator-summary cache on WaterBench_RiverCarve (port 8113). Run once per session
with the bench already up: from the moment the skin's generator source exists, poll water_av_skin until the reach is filled (gen
workers idle, no pending summaries), then a window at rest. Session 1 (no file) writes the cache; session 2 reads it.

    python tools/water_gencache_live.py --tag gencache_s1 [--wait-s 400] [--rest-s 20]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_a3_cost import window

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
POSE = {"x": -1728.0, "y": 115.0, "z": -1320.0, "yaw": 90.0, "pitch": -28.0}   # game.json's camera (water_skin_river_live's)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--wait-s", type=float, default=400.0)
    ap.add_argument("--rest-s", type=float, default=20.0)
    a = ap.parse_args()
    api = Api(a.url)
    camera_set(api, POSE)
    out = {"tag": a.tag, "pose": POSE}
    t0 = None
    trace = []
    deadline = time.time() + a.wait_s
    while time.time() < deadline:
        s = api.debug("water_av_skin", {})
        if t0 is None and s.get("generator_source"): t0 = time.time()
        if t0 is not None:
            gc = s.get("gen_cache", {})
            trace.append({"t": round(time.time() - t0, 1), **{k: s.get(k) for k in ("gen_pending", "gen_held", "gen_built", "pending", "summaries")},
                          "cache": {k: gc.get(k) for k in ("state", "hits", "misses", "written", "verified", "columns_loaded", "load_ms", "file_bytes")}})
            if time.time() - t0 > 5 and s.get("gen_pending", 1) == 0 and s.get("pending", 1) == 0: break
        time.sleep(0.5)
    out["filled_after_s"] = trace[-1]["t"] if trace else None
    out["fill"] = trace
    print("filled:", trace[-1] if trace else None, flush=True)
    out["rest"] = window(api, a.rest_s, "rest")
    out["skin_after"] = api.debug("water_av_skin", {})
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    print(json.dumps({"filled_after_s": out["filled_after_s"], "cache": out["skin_after"].get("gen_cache"),
                      "rest": {k: out["rest"].get(k) for k in ("fps_mean", "cpu_frame_ms_mean", "gpu_frame_ms_median", "frame_ms_max")}}, indent=1))


if __name__ == "__main__":
    main()
