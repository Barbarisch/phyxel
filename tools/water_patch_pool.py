"""WaterCore.md 51.9b - a patch's brick pool through a splash: its capacity at the wake (51.3b's sizing), every grow, and the peak of
bricks stored. The 150 m recorded lake (water_record_lake_clip.py's rig, WaterBench_Creek), the camera at a pose, the same visible splash;
water_av_list polled every ~50 ms until the patch sleeps (or --seconds).

    python tools/water_patch_pool.py [--pose close] [--seconds 40] [--tag patch_pool_close]
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
    ap.add_argument("--seconds", type=float, default=40.0)
    ap.add_argument("--tag", default="patch_pool_close")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
    api.debug("water_av_record_draw", {"radius": 48.0})
    out = {"pose": POSES[a.pose], "rows": []}
    try:
        out["record"] = lay_lake(api)
        camera_set(api, POSES[a.pose]); time.sleep(5.0)
        out["splash"] = splash(api)
        t0 = time.time(); seen = False
        while time.time() - t0 < a.seconds:
            vols = api.debug("water_av_list", {}).get("volumes", [])
            if vols:
                seen = True
                v = vols[0]
                out["rows"].append({"t": round(time.time() - t0, 3), "capacity": v.get("gpu_brick_capacity"), "bricks": v.get("gpu_bricks"),
                                    "grows": v.get("gpu_pool_grows"), "recorded_tiles": v.get("recorded_tiles"), "tiles_woken": v.get("tiles_woken")})
            elif seen:
                break   # slept back into the record
            time.sleep(0.05)
        rows = out["rows"]
        if rows:
            caps = sorted({r["capacity"] for r in rows if r["capacity"] is not None})
            peak = max((r["bricks"] or 0) for r in rows)
            out["summary"] = {"capacity_first": rows[0]["capacity"], "capacities": caps, "grows_last": rows[-1]["grows"], "bricks_peak": peak,
                              "alive_s": rows[-1]["t"]}
            for r in rows:
                if r["capacity"] != rows[0]["capacity"]:
                    out["summary"]["first_grow_t"] = r["t"]; break
            print(json.dumps(out["summary"]), flush=True)
    finally:
        destroy_all(api)
        api.debug("water_av_record_draw", {"radius": draw0})
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
        print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
