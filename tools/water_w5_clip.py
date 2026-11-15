"""WaterCore.md §54 (W5.1) - the owner's clip: wood debris dropped in a GENERATED river is carried downstream by the record's current.

WaterBench_RiverCarve (port 8113), the engine's defaults (water.enabled off, the record's current on). The 51.14 reach near
(-1729, -1040); the camera above its south bank looking across it (pose looked at before recording). At 3 s five 1/3 m WOOD GPU
debris blocks are dropped on the river's deepest wet columns (water_w5_river_current.pick); 30 s recorded with gdigrab, captioned
with the phase, the route's current at the drop and the frame rate; afterwards each block's drift from the particle log.

    python tools/water_w5_clip.py [--seconds 30] [--tag w5_clip]
Writes docs/evidence/water_core_m4/<tag>.mp4 / .srt / .json.
"""
import argparse, json, math, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_w5_river_current import pick, tracks_xz

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
POSE = {"x": -1712.0, "y": 34.0, "z": -1060.0, "yaw": 128.7, "pitch": -29.0}


def srt_time(t):
    ms = int(round(t * 1000)); return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--window", default="WaterBench_RiverCarve")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--skip", type=int, default=12)
    ap.add_argument("--tag", default="w5_clip")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    knobs = api.debug("water_av_river", {"on": True})
    camera_set(api, POSE); time.sleep(10.0)
    wet, _ = pick(api, "river")
    drops = wet[a.skip:a.skip + 5]
    if len(drops) < 5: raise SystemExit(f"only {len(drops)} wet columns after skip {a.skip}")
    speed = sum(d["river"]["speed_mps"] for d in drops) / len(drops)
    log = (EV / f"{a.tag}_debris_tmp.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    raw = EV / f"{a.tag}_raw.mp4"
    rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", str(raw)])
    t0 = time.time(); dropped = False; rows = []
    while rec.poll() is None:
        t = time.time() - t0
        if not dropped and t >= 3.0:
            for d in drops:
                api.debug("spawn_gpu_particle", {"x": d["p"][0], "y": d["surface_y"] + 1.2, "z": d["p"][2], "material": "Wood",
                                                 "scale": 1.0 / 3.0, "lifetime": 120.0, "spin": 0.0})
            dropped = True
        try: fps = api.get("/api/debug/engine_timing").get("fps")
        except Exception: fps = None
        rows.append({"t": round(t, 2), "fps": fps})
        time.sleep(0.25)
    api.debug("particle_log", {"action": "stop"})
    tr = tracks_xz(log); log.unlink(missing_ok=True)
    debris = []
    for slot, ps in tr.items():
        if len(ps) < 10: continue
        k0 = next((i for i, p in enumerate(ps) if p[2] < drops[0]["surface_y"] + 0.6), None)
        if k0 is None: continue
        p0, p1 = ps[k0], ps[-1]
        debris.append({"slot": slot, "t_in_water_s": round(p1[0] - p0[0], 2), "drift_m": round(math.hypot(p1[1] - p0[1], p1[3] - p0[3]), 2),
                       "start": [round(p0[1], 2), round(p0[3], 2)], "end": [round(p1[1], 2), round(p1[3], 2)], "y_last": round(p1[2], 3)})
    lines = []
    for i, r in enumerate(rows):
        t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
        ph = ("a generated river (the record: still spans, no simulation)" if r["t"] < 3.0 else
              f"5 wood blocks dropped in it - the record's current carries them (route {speed:.2f} m/s here)")
        lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{ph}\nt {r['t']:.1f} s | {(r['fps'] or 0):.0f} fps\n")
    (EV / f"{a.tag}.srt").write_text("\n".join(lines), encoding="utf-8")
    out = {"bench": "WaterBench_RiverCarve", "pose": POSE, "knobs": knobs, "route_speed_mps": speed,
           "drops": [{"p": d["p"], "surface_y": d["surface_y"], "river": d["river"]} for d in drops], "debris": debris, "rows": rows}
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", raw.name, "-vf", f"crop={a.crop},scale=1280:-2,subtitles={a.tag}.srt:force_style='{style}'",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}.mp4"], check=True, cwd=str(EV))
    raw.unlink(missing_ok=True)
    print("debris:", debris)
    print("wrote", EV / f"{a.tag}.mp4")


if __name__ == "__main__":
    main()
