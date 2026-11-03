"""WaterCore.md 51.2 live (L4): lakes written STRAIGHT INTO THE RECORD (water_av_record_lake - spans + one slept box, no volume), a
visible splash at the centre (water_av_impulse wakes a patch: 2 m / strength 8), the whole frame while the patch lives and after.
Lakes of W x W m on the flat WaterBench_Creek (port 8114), a sand bed and a stone kerb laid first (world edits), the record drawn
round the camera. Default: a 60 m and a 150 m lake (150 m: a 1,350 x 27 x 1,350-cell box - over the 16 M-cell volume cap; it can
only exist as the record).

    python tools/water_record_lake_live.py [--sizes 60,150] [--seconds 40]
Writes docs/evidence/water_core_m4/w51_record_lake_live.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job
from water_a3_cost import window

X0, Z0, TOP = 4, 2, 16


def lay_bed(api, W):
    world_job(api, "/api/world/clear", {"x1": X0, "y1": TOP - 1, "z1": Z0, "x2": X0 + W - 1, "y2": TOP + 3, "z2": Z0 + W - 1})
    world_job(api, "/api/world/fill", {"x1": X0, "y1": TOP - 1, "z1": Z0, "x2": X0 + W - 1, "y2": TOP - 1, "z2": Z0 + W - 1}, material="Sand")
    for b in ((X0 - 1, Z0 - 1, X0 - 1, Z0 + W), (X0 + W, Z0 - 1, X0 + W, Z0 + W), (X0 - 1, Z0 - 1, X0 + W, Z0 - 1), (X0 - 1, Z0 + W, X0 + W, Z0 + W)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": TOP, "z1": b[1], "x2": b[2], "y2": TOP + 1, "z2": b[3]}, material="StoneBricks")


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def run(api, W, seconds):
    destroy_all(api)
    lay_bed(api, W)
    r = api.debug("water_av_record_lake", {"x1": X0, "z1": Z0, "x2": X0 + W - 1, "z2": Z0 + W - 1, "bottom": TOP, "top": TOP + 2})
    print(f"{W} m lake into the record:", r, flush=True)
    cx, cz = X0 + W / 2, Z0 + W / 2
    camera_set(api, {"x": cx + 4, "y": TOP + 8, "z": cz + 18, "yaw": -90.0, "pitch": -30.0})
    time.sleep(3.0)
    w_rest = window(api, 6.0, f"{W} m at rest (the record only)")
    k = api.debug("water_av_impulse", {"x": cx, "y": TOP + 1.9, "z": cz, "radius": 2.0, "strength": 8.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    print("  splash:", k, flush=True)
    t1 = time.time()
    w_awake = window(api, 8.0, f"{W} m 0-8 s after the splash")
    rows = []
    while time.time() - t1 < seconds:
        vols = api.debug("water_av_list").get("volumes", [])
        rows.append({"t": round(time.time() - t1, 1), "volumes": len(vols), "awake": sum(v.get("awake_columns", 0) for v in vols if not v.get("asleep")),
                     "boxes": [v.get("box") for v in vols], "patch": [v.get("is_patch") for v in vols]})
        time.sleep(1.0)
    w_late = window(api, 6.0, f"{W} m after")
    slept = None
    return {"W": W, "record": r, "splash": k, "rest": w_rest, "awake": w_awake, "late": w_late, "rows": rows, "slept": slept}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--sizes", default="60,150")
    ap.add_argument("--seconds", type=float, default=40.0)
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
    api.debug("water_av_record_draw", {"radius": 200.0})   # the rig: the far lake drawn (W2 / S3's skin is not built)
    out = []
    try:
        for W in [int(s) for s in a.sizes.split(",")]:
            out.append(run(api, W, a.seconds))
            last = out[-1]["rows"]
            peak = max((r["awake"] for r in last), default=0)
            gone = next((r["t"] for r in last if r["volumes"] == 0), None)
            print(f"  {W} m: peak awake {peak}, live volumes max {max((r['volumes'] for r in last), default=0)}, back in the record at {gone} s, "
                  f"first patch box {last[0]['boxes'][:1] if last else None}", flush=True)
    finally:
        destroy_all(api)
        api.debug("water_av_record_draw", {"radius": draw0})
        ev = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4" / "w51_record_lake_live.json"
        ev.write_text(json.dumps(out, indent=1))
        print("wrote", ev)


if __name__ == "__main__":
    main()
