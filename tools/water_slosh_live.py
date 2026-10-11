"""WaterCore 24 W-L2 (owner 2026-10-10: "water surface deforms a little slow making it look a thicker than water").

Small bench: the demo pond (rest level half-way up a 1/3 m cell - 24.1's worst case), five 1/3 m stones, the demo
camera. Records a clip and polls the pond's kinetic energy and its surface spread (max - min of the drawn surface over
the pond's columns) at ~10 Hz, so the run says how long the water keeps moving after the stones.

    python tools/water_slosh_live.py [--tag NAME] [--seconds 25]
Writes docs/evidence/water_core_e/slosh_live_<tag>.json + .mp4.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_splash_demo import POSES, build_pond

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_e"
STONES = [(100.6, 12.6), (102.4, 12.7), (101.5, 13.5), (100.7, 14.4), (102.5, 14.3)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--tag", default="now")
    ap.add_argument("--seconds", type=float, default=25.0)
    args = ap.parse_args()
    ff = shutil.which("ffmpeg")
    if not ff: raise SystemExit("ffmpeg not on PATH")
    api = Api(args.url); api.wait_status(60)
    build_pond(api)
    api.debug("water_droplets", {"enabled": False})
    camera_set(api, POSES["mid"]); time.sleep(6)
    mp4 = EV / f"slosh_live_{args.tag}.mp4"
    rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.seconds),
                            "-i", "title=WaterBench_Small", "-c:v", "libx264", "-crf", "26", "-pix_fmt", "yuv420p", str(mp4)])
    t0 = time.time(); time.sleep(1.0); t_drop = time.time() - t0
    for x, z in STONES:
        api.debug("spawn_gpu_particle", {"x": x, "y": 20.0, "z": z, "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 120.0})
    polls = []
    while rec.poll() is None:
        av = api.debug("water_av_list")
        vols = av.get("volumes", [])
        v = vols[0] if vols else {}
        polls.append({"t": round(time.time() - t0 - t_drop, 2), **{k: v.get(k) for k in v if isinstance(v.get(k), (int, float, bool))}})
        time.sleep(0.1)
    out = {"captured": time.strftime("%Y-%m-%d %H:%M:%S"), "tag": args.tag, "video": str(mp4), "drop_t": round(t_drop, 2),
           "polls": polls}
    p = EV / f"slosh_live_{args.tag}.json"
    p.write_text(json.dumps(out, indent=1)); print("wrote", p, len(polls), "polls; keys", sorted(polls[0].keys()) if polls else [])


if __name__ == "__main__":
    main()
