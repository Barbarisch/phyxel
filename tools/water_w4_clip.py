"""WaterCore 52 - the owner's clip at the end of W4: the world on the new water, the old water deleted.
WaterBench_RiverCarve (port 8113, Release; the engine's defaults - water.enabled off as shipped), the 51.13 lake (a GENERATED body:
water 90, bed 87, held in the record - no volume, no old water). One take, recorded from the window (gdigrab: /api/screenshot omits
pond water), captioned ~4 a second with the phase, the camera's height and depth under the surface (water_stats, the camera's own
reader), the debris heights, and the frame rate:
  0-3 s   the lake from above;
  3 s     three 1/3 m WOOD debris blocks dropped onto it (GPU debris; they read the record's water through their tiles) - they float;
  9-17 s  the camera sinks from 3 m above the surface to 1.5 m under it (the underwater look: the camera's reader, 52.1);
  17-21 s it holds under; 21-27 s it rises back out.
    python tools/water_w4_clip.py [--tag w4_clip]
Writes docs/evidence/water_core_m4/<tag>.mp4 (captioned), <tag>_raw.mp4, <tag>.srt and <tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, particle_tracks

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
LEVEL = 90.0
X, Z = -2224.0, -1349.5                       # the camera's column over the lake
DROPS = [(-2229.5, -1349.5), (-2230.0, -1348.3), (-2230.6, -1350.6)]   # ahead of the camera (yaw 180 looks -x)
YAW, PITCH = 180.0, -12.0


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def cam_y(t):
    if t < 9.0: return LEVEL + 3.0
    if t < 17.0: return LEVEL + 3.0 - 4.5 * (t - 9.0) / 8.0
    if t < 21.0: return LEVEL - 1.5
    if t < 27.0: return LEVEL - 1.5 + 4.5 * (t - 21.0) / 6.0
    return LEVEL + 3.0


def phase(t):
    if t < 3.0: return "a generated lake - the record, no volume, no old water"
    if t < 9.0: return "wood debris dropped on it - it floats"
    if t < 17.0: return "the camera sinks under the surface"
    if t < 21.0: return "under the lake"
    return "back up"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--window", default="WaterBench_RiverCarve")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--tag", default="w4_clip")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    camera_set(api, {"x": X, "y": cam_y(0), "z": Z, "yaw": YAW, "pitch": PITCH}); time.sleep(8.0)
    log = (EV / f"{a.tag}_debris_tmp.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    raw = EV / f"{a.tag}_raw.mp4"
    rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", str(raw)])
    t0 = time.time(); dropped = False; rows = []
    while rec.poll() is None:
        t = time.time() - t0
        if not dropped and t >= 3.0:
            for (dx, dz) in DROPS:
                api.debug("spawn_gpu_particle", {"x": dx, "y": LEVEL + 1.2, "z": dz, "material": "Wood", "scale": 1.0 / 3.0, "lifetime": 120.0, "spin": 0.0})
            dropped = True
        camera_set(api, {"x": X, "y": cam_y(t), "z": Z, "yaw": YAW, "pitch": PITCH})
        ws = api.debug("water_stats", {})
        try: fps = api.get("/api/debug/engine_timing").get("fps")
        except Exception: fps = None
        rows.append({"t": round(t, 2), "cam_y": round(cam_y(t), 2), "submergence": ws.get("camera_submergence"),
                     "depth": ws.get("camera_depth_below"), "fps": fps})
        time.sleep(0.25)
    api.debug("particle_log", {"action": "stop"})
    tracks = particle_tracks(log); log.unlink(missing_ok=True)
    rests = []
    for tr in tracks.values():
        tail = [p[1] for p in tr if p[0] > tr[-1][0] - 3.0]
        if tail: rests.append({"samples": len(tr), "min_y": round(min(p[1] for p in tr), 3), "rest_y_last3s": round(sum(tail) / len(tail), 3)})
    lines = []
    for i, r in enumerate(rows):
        t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
        under = f"{r['depth']:.2f} m under the surface" if (r["depth"] or 0) > 0 else "above the surface"
        lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{phase(r['t'])}\n"
                     f"camera y {r['cam_y']:.1f} (water 90.0): {under} | {(r['fps'] or 0):.0f} fps\n")
    (EV / f"{a.tag}.srt").write_text("\n".join(lines), encoding="utf-8")
    out = {"bench": "WaterBench_RiverCarve", "level": LEVEL, "drops": DROPS, "debris": rests, "rows": rows}
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", raw.name, "-vf", f"crop={a.crop},scale=1280:-2,subtitles={a.tag}.srt:force_style='{style}'",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}.mp4"], check=True, cwd=str(EV))
    print("debris:", rests)
    print("wrote", EV / f"{a.tag}.mp4")


if __name__ == "__main__":
    main()
