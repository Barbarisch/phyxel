"""WaterCore.md 26.30 - the look gate for the surface tiles: the tiles must draw what the whole mesh draws.

A resting 20 x 20 m pond (as tools/water_surface_split.py) at a fixed pose, captured from the window (gdigrab - /api/screenshot
omits pond water): WHOLE (the per-frame whole mesh, --tiles off) twice - the CONTROL, how much two captures of the same thing
differ (the water's shading moves with time) - then TILES (on, culled) twice. Pass: the whole-vs-tiles difference is no larger
than the whole-vs-whole one (mean absolute difference per channel, and the share of pixels differing by more than 8 of 255).

    python tools/water_surface_ab.py [--size 20x20] [--tag 2630]
Writes <--shots>/surface_ab_<tag>_{whole1,whole2,tiles1,tiles2}.png + docs/evidence/water_core_h1/surface_ab_<tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
POSE = {"position": {"x": 12.0, "y": 22.0, "z": 30.0}, "yaw": -90.0, "pitch": -22.0}   # the pond (x 2-21, z 0-19) fills the lower half


def grab(path, title):
    subprocess.run([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={title}", "-frames:v", "1", str(path)], check=True)
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)


def diff(a, b):
    d = np.abs(a - b)
    return {"mean_abs": round(float(d.mean()), 4), "share_over_8": round(float((d.max(axis=2) > 8).mean()), 6)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--size", default="20x20")
    ap.add_argument("--tag", default="2630")
    ap.add_argument("--title", default="WaterBench_Creek")
    ap.add_argument("--shots", default=str(EV), help="where the captures go")
    ap.add_argument("--gap", type=float, default=0.5, help="seconds between captures (the same for every pair)")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(120)
    api.debug("water_av_realtime", {"on": True})
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    l, w = (int(v) for v in args.size.split("x"))
    box = {"x1": 2, "y1": 16, "z1": 0, "x2": 2 + l - 1, "y2": 17, "z2": w - 1}
    vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
    api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
    api.post("/api/camera", POSE)
    time.sleep(3.0)
    shots, out = {}, {"pose": POSE, "size_m": [l, w]}
    # one cadence for every pair (the shading moves with time: a longer gap alone differs more - measured 2026-10-18, the first
    # run's whole-vs-tiles pair was ~2 s apart against 0.5 s for the controls); the switch lands on the next frame
    api.debug("water_render_core", {"tiles": False, "cull": True}); time.sleep(1.5)
    for name, switch in (("whole1", None), ("whole2", True), ("tiles1", None), ("tiles2", None)):
        shots[name] = grab(pathlib.Path(args.shots) / f"surface_ab_{args.tag}_{name}.png", args.title)
        if switch is not None: out["tiles"] = api.debug("water_render_core", {"tiles": switch}).get("tiles")
        time.sleep(args.gap)
    api.debug("water_render_core", {"mode": "off"}); time.sleep(1.0)   # the positive control: the water must be IN the frame
    shots["nowater"] = grab(pathlib.Path(args.shots) / f"surface_ab_{args.tag}_nowater.png", args.title)
    api.debug("water_render_core", {"mode": "mesh"})
    out["water_vs_nowater"] = diff(shots["whole1"], shots["nowater"])
    out["control_whole_vs_whole"] = diff(shots["whole1"], shots["whole2"])
    out["tiles_vs_tiles"] = diff(shots["tiles1"], shots["tiles2"])
    out["whole_vs_tiles"] = diff(shots["whole2"], shots["tiles1"])
    out["pass"] = out["water_vs_nowater"]["share_over_8"] > 0.05 and out["whole_vs_tiles"]["mean_abs"] <= max(out["control_whole_vs_whole"]["mean_abs"], out["tiles_vs_tiles"]["mean_abs"]) * 1.25 + 0.02
    api.debug("water_av_destroy", {"id": vid})
    (EV / f"surface_ab_{args.tag}.json").write_text(json.dumps(out, indent=1))
    print(json.dumps({k: out[k] for k in ("water_vs_nowater", "control_whole_vs_whole", "tiles_vs_tiles", "whole_vs_tiles", "pass")}, indent=1))


if __name__ == "__main__":
    main()
