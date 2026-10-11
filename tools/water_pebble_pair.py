"""WaterCore 24.9 P-L3: a 1/3 m and a 1/9 m stone dropped side by side into the Small bench pond, recorded from the
demo camera's low pose, with each stone's depth traced (water_coupling wet_body_list). Small stones should sink
visibly slower (form drag ~ 1 / size): terminal ~3.2 vs ~1.8 m/s for granite.

    python tools/water_pebble_pair.py [--tag NAME]
Writes docs/evidence/water_core_e/pebble_pair_<tag>.{json,mp4}.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_splash_demo import POSES, build_pond

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_e"
PAIR = [((100.7, 13.5), 1.0 / 3.0), ((102.3, 13.5), 1.0 / 9.0)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--tag", default="now")
    ap.add_argument("--seconds", type=float, default=6.0)
    args = ap.parse_args()
    ff = shutil.which("ffmpeg")
    if not ff: raise SystemExit("ffmpeg not on PATH")
    api = Api(args.url); api.wait_status(60)
    build_pond(api)
    api.debug("water_droplets", {"enabled": False})
    camera_set(api, POSES["low"]); time.sleep(6)
    mp4 = EV / f"pebble_pair_{args.tag}.mp4"
    rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.seconds),
                            "-i", "title=WaterBench_Small", "-c:v", "libx264", "-crf", "24", "-pix_fmt", "yuv420p", str(mp4)])
    t0 = time.time(); time.sleep(0.8)
    for (x, z), s in PAIR:
        api.debug("spawn_gpu_particle", {"x": x, "y": 18.0, "z": z, "material": "Stone", "scale": s, "lifetime": 60.0})
    t_drop = time.time() - t0
    trace = []
    while rec.poll() is None:
        r = api.debug("water_coupling", {})
        trace.append({"t": round(time.time() - t0 - t_drop, 3), "bodies": r.get("wet_body_list", [])})
        time.sleep(0.05)
    out = {"captured": time.strftime("%Y-%m-%d %H:%M:%S"), "pair": PAIR, "drop_t": round(t_drop, 2), "video": str(mp4), "trace": trace}
    p = EV / f"pebble_pair_{args.tag}.json"
    p.write_text(json.dumps(out, indent=1)); print("wrote", p, len(trace), "samples; first sample keys:", list(trace[5].keys()) if len(trace) > 5 else None)
    # per stone (matched by its half size): speed at each depth of its bottom below the rest level (16.5)
    for (x, z), sz in PAIR:
        rows = []
        for smp in trace:
            for b in smp["bodies"]:
                if abs(b["half"] - sz / 2) < 0.01 and abs(b["centre"][0] - x) < 0.6:
                    rows.append((smp["t"], 16.5 - (b["centre"][1] - b["half"]), -b["velocity"][1]))
        sink = [(t, d, v) for t, d, v in rows if d > 0.2]
        print(f"  {sz:.3f} m stone: " + ", ".join(f"{d:.2f} m down {v:.2f} m/s" for t, d, v in sink[::3][:8]))


if __name__ == "__main__":
    main()
