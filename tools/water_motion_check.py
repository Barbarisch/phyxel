"""Watch the water IN MOTION before handing it over (feedback: owner sign-off). Records the engine window
with ffmpeg from OUTSIDE the engine (no screenshot stalls), fires one stimulus during the recording with a
single API call, then reads the engine's true frame pacing for that window. Separately (not while
recording), samples the surface at a fixed point ~30x a second to count flicker jumps.

Usage: python tools/water_motion_check.py --scenario blast|debris|floaters [--seconds 10] [--tag T]
floaters: 10 wood pieces rest on the pond first, then ONLY the water is pushed (nothing touches the wood) -
they must wake, ride the slosh and come back to rest (docs/WaterCore.md 19.6).
Writes docs/evidence/water_core_e/<tag>.mp4, <tag>_frames/ (10 fps stills of the action), <tag>.json."""
import argparse, json, os, shutil, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set

ROOT = Path(__file__).resolve().parents[1]
EV = ROOT / "docs" / "evidence" / "water_core_e"
ap = argparse.ArgumentParser()
ap.add_argument("--url", default="http://127.0.0.1:8111")
ap.add_argument("--window", default="WaterBench_Small")
ap.add_argument("--scenario", default="blast", choices=["blast", "debris", "rest", "floaters", "stone", "stone_big"])
ap.add_argument("--seconds", type=float, default=10.0)
ap.add_argument("--tag", default=None)
ap.add_argument("--cell", type=float, default=1.0 / 3.0, help="the pond's cell size in voxels (1/3 shipped; 1/9 finer)")
ap.add_argument("--solids", choices=["on", "off"], default="on", help="debris displaces water (WaterCore.md 20); off = the A/B control")
args = ap.parse_args()
tag = args.tag or f"motion_{args.scenario}"
EV.mkdir(parents=True, exist_ok=True)
api = Api(args.url); api.wait_status(900)
ffmpeg = shutil.which("ffmpeg")
if not ffmpeg:
    raise SystemExit("ffmpeg not on PATH")
POSE = {"x": 109.5, "y": 20.5, "z": 13.5, "yaw": 180, "pitch": -38}
REST = 16.5

def make_pond():
    for v in api.debug("water_av_list").get("volumes", []):
        api.debug("water_av_destroy", {"id": v["id"]})
    cr = api.debug("water_av_create", {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16, "cellSize": args.cell,
                                       "transport": "eulerian", "backend": "auto", "auto_sleep": False})
    if "error" in cr: raise SystemExit(f"create: {cr}")
    api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
    api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_coupling", {"enabled": True, "solids": args.solids == "on"})

def stimulus():
    if args.scenario == "blast":
        api.post("/api/damage/apply", {"x": 106, "y": 17, "z": 13.5, "radius": 4.0, "energy": 62.0})
    elif args.scenario == "floaters":   # push only the water
        api.debug("water_av_impulse", {"x": 103, "y": 16, "z": 13.5, "radius": 1.5, "strength": 3, "dx": -1, "dy": 0.3})
    elif args.scenario in ("stone", "stone_big"):   # ONE stone dropped 3 m into the pond centre (item 1: impacts, WaterCore.md 19.9)
        api.debug("spawn_gpu_particle", {"x": 101.5, "y": 19.5, "z": 13.5, "material": "Stone",
                                         "scale": (1.0 if args.scenario == "stone_big" else 1.0 / 3.0), "lifetime": 30.0})
    elif args.scenario == "debris":
        for i in range(10):   # one call per piece: 20 calls in a burst (~20-40 ms), then nothing
            x, z = 100.35 + (i % 2) * 0.9, 12.4 + (i // 2) * 0.7
            api.debug("spawn_gpu_particle", {"x": x, "y": 19.5, "z": z, "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 30.0})
            api.debug("spawn_gpu_particle", {"x": x + 2.0, "y": 19.5, "z": z, "material": "Wood", "scale": 1.0 / 3.0, "lifetime": 30.0})

camera_set(api, POSE)
make_pond()
time.sleep(8)
if args.scenario == "floaters":   # the wood settles (asleep) before the recording starts
    for i in range(10):
        api.debug("spawn_gpu_particle", {"x": 100.35 + (i % 4) * 0.9, "y": 19.5, "z": 12.4 + (i // 4) * 1.1, "material": "Wood", "scale": 1.0 / 3.0, "lifetime": 120.0})
    time.sleep(14)
mp4 = EV / f"{tag}.mp4"
rec = subprocess.Popen([ffmpeg, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.seconds),
                        "-i", f"title={args.window}", "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
time.sleep(1.5)
t_stim = time.time()
stimulus()
rec.wait()
pacing = api.get(f"/api/debug/frame_pacing?frames={int(args.seconds * 400)}")
# the frames of the action, 10 per second for 3 s from the stimulus
fdir = EV / f"{tag}_frames"; fdir.mkdir(exist_ok=True)
for f in fdir.glob("*.png"): f.unlink()
subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-ss", "1.3", "-t", "3.0", "-i", str(mp4), "-vf", "fps=10", str(fdir / "f_%02d.png")])
# flicker: a second stimulus, then the surface at a fixed point ~30x a second for 3 s (stalls are fine here)
time.sleep(4)
stimulus()
ys = []
ring = {d: [] for d in (0.0, 0.7, 1.4)}   # the surface at the drop point and 0.7 / 1.4 m from it (east)
t0 = time.time()
while time.time() - t0 < 3.0:
    ys.append(api.debug("water_av_probe", {"x": 101.5, "y": 16.0, "z": 13.5}).get("surface_y"))
    for d in ring:
        y = api.debug("water_av_probe", {"x": 101.5 + d, "y": 16.0, "z": 13.5}).get("surface_y")
        if y is not None: ring[d].append(y)
    time.sleep(0.02)
vals = [y for y in ys if y is not None]
jumps = [abs(b - a) for a, b in zip(vals, vals[1:])]
# frame pacing summary (whatever shape the route returns)
summary = {k: v for k, v in pacing.items() if k not in ("series", "frames", "raw")} if isinstance(pacing, dict) else pacing
out = {"scenario": args.scenario, "pose": POSE, "video": str(mp4), "frames_dir": str(fdir),
       "frame_pacing": summary,
       "flicker": {"samples": len(vals), "largest_jump_m": round(max(jumps), 4) if jumps else None,
                   "jumps_over_5cm": sum(1 for j in jumps if j > 0.05), "surface_range": [round(min(vals), 4), round(max(vals), 4)] if vals else None},
       "cell": args.cell, "solids": args.solids, "moving_solids": api.debug("water_coupling", {}).get("moving_solids"),
       "ring_m_from_drop": {str(d): {"min": round(min(v) - REST, 4), "max": round(max(v) - REST, 4)} for d, v in ring.items() if v}}
(EV / f"{tag}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
print(json.dumps(out, indent=1)[:3000])
