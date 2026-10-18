"""The owner's look at the splash (2026-10-18: "the splashes seem way too round and numerous - really overdid it with the splash
particles"): the same 1 m stone into the same pond once per droplet variant, recorded from the window and laid out 2 x 2 with the
variant and the droplet count burned in.

Variants (water_droplets): size = droplet edge as a fraction of a 1/9 m cell (21.11 default 1/9 -> 1.2 cm drops, ~8,500 at a
stone's peak); lateral = the sideways share of each parcel's velocity spread (1 = a ball, the default; < 1 = thrown along its own
direction). The rig's look: a sand bed and a stone kerb TWO blocks high (the water sits inside the rim, not level with its top).

    python tools/water_splash_variants_video.py [--seconds 7] [--tag splash_look]
Writes docs/evidence/water_core_h1/<tag>_<i>.mp4 and <tag>_grid.mp4 (+ .json).
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
VARIANTS = [   # (label, size fraction, lateral)
    ("NOW: 1.2 cm drops, burst in a ball", 1 / 9, 1.0),
    ("3.7 cm drops, ball", 1 / 3, 1.0),
    ("11 cm drops (microcube), ball", 1.0, 1.0),
    ("3.7 cm drops, thrown along the splash", 1 / 3, 0.2),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--seconds", type=float, default=7.0)
    ap.add_argument("--pose", default="-1,21.5,32,-49,-22", help="x,y,z,yaw,pitch (close on the splash)")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--still", action="store_true")
    ap.add_argument("--tag", default="splash_look")
    args = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(args.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    l, w, x0, z0 = 46, 28, 2, 0
    world_job(api, "/api/world/clear", {"x1": x0, "y1": 15, "z1": z0, "x2": x0 + l - 1, "y2": 15, "z2": z0 + w - 1})
    world_job(api, "/api/world/fill", {"x1": x0, "y1": 15, "z1": z0, "x2": x0 + l - 1, "y2": 15, "z2": z0 + w - 1}, material="Sand")
    for b in ((x0 - 1, z0 - 1, x0 - 1, z0 + w), (x0 + l, z0 - 1, x0 + l, z0 + w), (x0 - 1, z0 - 1, x0 + l, z0 - 1), (x0 - 1, z0 + w, x0 + l, z0 + w)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": 16, "z1": b[1], "x2": b[2], "y2": 17, "z2": b[3]}, material="StoneBricks")
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_coupling", {"enabled": True, "solids": True})
    p = [float(v) for v in args.pose.split(",")]
    pose = {"x": p[0], "y": p[1], "z": p[2], "yaw": p[3], "pitch": p[4]}
    sx, sz = x0 + 4.0, z0 + w - 4.0

    def pond():
        for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
        box = {"x1": x0, "y1": 16, "z1": z0, "x2": x0 + l - 1, "y2": 17, "z2": z0 + w - 1}
        api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False, "partial_wake": True})
        api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
        camera_set(api, pose)
        time.sleep(4.0)

    if args.still:
        pond()
        subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={args.window}", "-frames:v", "1", "-vf", f"crop={args.crop}", str(EV / f"{args.tag}_still.png")], check=True)
        print("wrote", EV / f"{args.tag}_still.png"); return
    out = []
    for i, (label, size, lateral) in enumerate(VARIANTS):
        api.debug("water_droplets", {"enabled": True, "size": size, "lateral": lateral})
        pond()
        mp4 = EV / f"{args.tag}_{i}.mp4"
        rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.seconds),
                                "-i", f"title={args.window}", "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
        time.sleep(1.0)
        api.debug("spawn_gpu_particle", {"x": sx, "y": 20.0, "z": sz, "material": "Stone", "scale": 1.0, "lifetime": args.seconds})
        peak = 0
        while rec.poll() is None:
            peak = max(peak, api.debug("water_droplets", {}).get("alive", 0)); time.sleep(0.05)
        out.append({"label": label, "size": size, "lateral": lateral, "peak_droplets": peak})
        print(label, "peak droplets", peak, flush=True)
        (EV / f"{args.tag}_{i}.txt").write_text(f"{label}  |  {peak} droplets at the peak", encoding="utf-8")
        time.sleep(1.0)
    api.debug("water_droplets", {"enabled": True, "size": 1 / 9, "lateral": 1.0})   # back to the default
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    (EV / f"{args.tag}.json").write_text(json.dumps({"pose": pose, "variants": out}, indent=1))
    # 2 x 2, each captioned (drawtext from a file: no escaping of the label)
    font = "C\\:/Windows/Fonts/arial.ttf"
    parts = []
    for i in range(len(VARIANTS)):
        parts.append(f"[{i}:v]crop={args.crop},scale=960:-2,drawtext=fontfile='{font}':textfile={args.tag}_{i}.txt:fontsize=22:fontcolor=white:box=1:boxcolor=black@0.6:boxborderw=6:x=10:y=h-th-12[v{i}]")
    fc = ";".join(parts) + ";[v0][v1]hstack[top];[v2][v3]hstack[bot];[top][bot]vstack[v]"
    cmd = [ff, "-y", "-loglevel", "error"]
    for i in range(len(VARIANTS)): cmd += ["-i", f"{args.tag}_{i}.mp4"]
    cmd += ["-filter_complex", fc, "-map", "[v]", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", f"{args.tag}_grid.mp4"]
    subprocess.run(cmd, check=True, cwd=str(EV))
    print("wrote", EV / f"{args.tag}_grid.mp4")


if __name__ == "__main__":
    main()
