"""WaterCore.md 25.25 - on the simple creek bench (25.24, the creek running): (1) does the pond work carry over - a stone
dropped in the creek, the ripple layer / droplet / moving-solid counters before and after; (2) the current in action - wood
blocks dropped at the head, their positions tracked as they drift downstream (wet_body_list), a clip.

    python tools/water_creek_current.py [--seconds 60] [--pieces 4]
Writes docs/evidence/water_core_h1/creek_current.{json,mp4}.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"


def counters(api):
    rp = api.debug("water_ripples", {}); dr = api.debug("water_droplets", {}); cp = api.debug("water_coupling", {})
    return {"ripple_layers": rp.get("layers"), "ripple_awake": rp.get("awake"), "droplet_births": dr.get("births"),
            "droplets_alive": dr.get("alive"), "coupling_enabled": cp.get("enabled"),
            "moving_solid_bodies": (cp.get("moving_solids") or {}).get("bodies"), "wet_bodies": (cp.get("moving_solids") or {}).get("wet_bodies")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--pieces", type=int, default=4)
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(60)
    regs = api.debug("water_columns", {}).get("regions", [])
    if not regs or regs[0]["wet_columns"] < 100: raise SystemExit("start the creek first: python tools/water_creek_bench.py --run")
    api.debug("water_coupling", {"enabled": True})
    out = {"before": counters(api)}
    # (1) a stone into the creek, mid-reach
    api.debug("spawn_gpu_particle", {"x": 40.5, "y": 19.0, "z": 31.9, "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 120.0})
    time.sleep(3.0)
    out["after_stone"] = counters(api)
    print("pond-work counters before / after a stone in the creek:", json.dumps(out))
    # (2) wood at the head, tracked downstream
    camera_set(api, {"x": 14.0, "y": 19.5, "z": 35.0, "yaw": -20.0, "pitch": -24.0})
    time.sleep(1.5)
    ff = shutil.which("ffmpeg"); EV.mkdir(parents=True, exist_ok=True)
    rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.seconds), "-i", "title=WaterBench_Creek",
                            "-vf", "crop=960:630:240:42", "-c:v", "libx264", "-crf", "28", "-pix_fmt", "yuv420p", str(EV / "creek_current.mp4")]) if ff else None
    t0 = time.time()
    for k in range(args.pieces):
        api.debug("spawn_gpu_particle", {"x": 13.0 + 2.0 * k, "y": 16.6, "z": 31.5 + 0.4 * (k % 2), "material": "Wood", "scale": 1.0 / 3.0, "lifetime": 600.0})
    track = []
    while time.time() - t0 < args.seconds:
        bl = api.debug("water_coupling", {}).get("wet_body_list", [])
        track.append({"t": round(time.time() - t0, 2), "bodies": [{"x": round(b["centre"][0], 2), "y": round(b["centre"][1], 2), "z": round(b["centre"][2], 2),
                                                                   "vx": round(b["velocity"][0], 3), "vz": round(b["velocity"][2], 3)} for b in bl]})
        time.sleep(0.5)
    if rec: rec.wait()
    out["track"] = track
    (EV / "creek_current.json").write_text(json.dumps(out, indent=1))
    for p in track[::12]:
        print(f"  t {p['t']:5.1f} s: " + ", ".join(f"x {b['x']:.1f} (vx {b['vx']:+.2f})" for b in p["bodies"] if b["x"] < 120))


if __name__ == "__main__":
    main()
