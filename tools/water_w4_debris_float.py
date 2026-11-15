"""WaterCore 52 W4 control: a 1/3 m wood GPU debris block dropped into the RiverCarve lake (the 51.13 body: water 90) must float -
dip, then rest with its centre near the surface (wood's draft ~0.8 of its size), never sink to the bed (87).
Run with WaterBench_RiverCarve up (port 8113):  python tools/water_w4_debris_float.py --tag w4_old_after
Writes docs/evidence/water_core_m4/<tag>_debris_float.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, particle_tracks

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    a = ap.parse_args()
    api = Api("http://127.0.0.1:8113"); api.wait_status(600)
    x, z, level = -2227.5, -1349.5, 90.0
    camera_set(api, {"x": -2219.5, "y": 93.0, "z": -1349.5, "yaw": 180.0, "pitch": -20.0}); time.sleep(4)
    log = (EV / f"{a.tag}_debris_tmp.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    size = 1.0 / 3.0
    r = api.debug("spawn_gpu_particle", {"x": x, "y": level + size / 2 + 0.5, "z": z, "material": "Wood", "scale": size, "lifetime": 60.0, "spin": 0.0})
    print("spawn", r.get("success"))
    time.sleep(12)
    api.debug("particle_log", {"action": "stop"})
    tr = particle_tracks(log)
    ys = max(tr.values(), key=len) if tr else []
    log.unlink(missing_ok=True)
    tail = [p[1] for p in ys if p[0] > ys[-1][0] - 3.0] if ys else []
    res = {"level": level, "size": size, "samples": len(ys), "start_y": ys[0][1] if ys else None, "min_y": min(p[1] for p in ys) if ys else None,
           "rest_centre_y_mean_last3s": sum(tail) / len(tail) if tail else None}
    if tail: res["draft"] = (level - (res["rest_centre_y_mean_last3s"] - size / 2)) / size
    print(json.dumps(res))
    (EV / f"{a.tag}_debris_float.json").write_text(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
