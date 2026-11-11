"""WaterCore.md 51.4d live (L4): record tiles over DEEP generated water - WaterBench_RiverCarve (port 8113), the camera brought to the
~60 m-deep lake its record holds (y 107.8 round (-1728, -1140)). The record tiles in range are rebuilt (the radius bumped and put back),
then every frame is timed until none is pending: the record draw's build ms per call (max, mean) and the whole frame. 51.4c measured
309-513 ms a call there with the whole depth woken.

    python tools/water_record_deep_live.py [--tag record_deep]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_a3_cost import window

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--tag", default="record_deep")
    ap.add_argument("--settle-s", type=float, default=60.0)
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(600)
    pose = {"x": -1700.0, "y": 125.0, "z": -1160.0, "yaw": 90.0, "pitch": -40.0}
    camera_set(api, pose); time.sleep(a.settle_s)
    out = {"pose": pose, "rounds": []}
    for k in range(3):
        # every tile in range rebuilt: the radius dropped to 0 (tiles go), then back to 48 (tiles come back, 4 a call)
        api.debug("water_av_record_draw", {"radius": 0.0}); time.sleep(1.0)
        api.debug("water_av_record_draw", {"radius": 48.0})
        t0 = time.time(); calls = []
        while time.time() - t0 < 60.0:
            r = api.debug("water_av_record_draw", {})
            calls.append({"t": round(time.time() - t0, 2), "built": r["built_last_call"], "ms": r["build_ms_last_call"], "pending": r["pending"], "max": r["build_ms_max"]})
            if r["pending"] == 0 and time.time() - t0 > 2.0: break
            time.sleep(0.05)
        built = [c for c in calls if c["built"] > 0]
        per = [c["ms"] / c["built"] for c in built]
        rd = {"seconds_to_draw": calls[-1]["t"], "samples": len(calls), "calls_seen_building": len(built),
              "ms_per_call_max": max((c["ms"] for c in built), default=0.0), "ms_per_tile_mean": sum(per) / len(per) if per else 0.0,
              "wet_tiles": api.debug("water_av_record_draw", {}).get("wet_tiles")}
        out["rounds"].append(rd)
        print(f"round {k}: {rd}", flush=True)
    out["rest"] = window(api, 6.0, "at rest over the deep lake")
    r = out["rest"]
    print(f"rest: {r['fps_mean']} fps, cpu {r['cpu_frame_ms_mean']} ms, gpu {r['gpu_frame_ms_median']} ms, worst {r['frame_ms_max']} ms", flush=True)
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
