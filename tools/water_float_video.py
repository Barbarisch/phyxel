"""WaterCore 31 (M4) - the owner's look: the same drops into the Small bench pond twice, the old floating law (the table law: the
column's top read as the waterline) and the water's push (water_coupling pressure_force), recorded from the window (gdigrab:
/api/screenshot omits pond water) and put side by side, each half captioned live with the time since the drop and the frame rate.

The drops (each take the same): a 1 m Wood block placed on the surface, three 1/3 m Wood blocks and one 1/3 m Stone from 1 m above
it, axis-aligned (spin 0) so the two takes start alike. The pond: tools/water_float_rest.py's (4 x 4 m, 1.5 m deep, 1/9 m cells,
auto-sleep off so the volume stays awake to be seen - the shipped default would record it, 31.9 b).

    python tools/water_float_video.py --still      # framing check: one frame
    python tools/water_float_video.py [--seconds 14] [--tag m4_look]
Writes docs/evidence/water_core_m4/<tag>_off.mp4, <tag>_on.mp4 (+ .srt), <tag>_side_by_side.mp4.
"""
import argparse, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_float_rest as w


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


DROPS = [((101.0, 13.0), 1.0, 0.0, "Wood"), ((103.2, 12.7), 1 / 3, 1.0, "Wood"), ((103.2, 13.9), 1 / 3, 1.0, "Wood"),
         ((103.2, 15.1), 1 / 3, 1.0, "Wood"), ((101.0, 15.0), 1 / 3, 1.0, "Stone")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--window", default="WaterBench_Small")
    ap.add_argument("--seconds", type=float, default=14.0)
    ap.add_argument("--pose", default="102.0,18.4,20.5,-90,-22", help="x,y,z,yaw,pitch")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--still", action="store_true")
    ap.add_argument("--tag", default="m4_look")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    if not ff: raise SystemExit("ffmpeg not on PATH")
    api = Api(a.url); api.wait_status(300)
    p = [float(v) for v in a.pose.split(",")]
    pose = {"x": p[0], "y": p[1], "z": p[2], "yaw": p[3], "pitch": p[4]}
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    w.AUTO_SLEEP = False; w.DISPLACE = True
    if a.still:
        w.PRESSURE = True; w.pond(api); camera_set(api, pose); time.sleep(3)
        subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={a.window}", "-frames:v", "1", "-vf", f"crop={a.crop}",
                        str(w.EV / f"{a.tag}_still.png")], check=True)
        print("wrote", w.EV / f"{a.tag}_still.png"); return
    for name, on in (("off", False), ("on", True)):
        w.PRESSURE = on
        w.pond(api)
        camera_set(api, pose)
        time.sleep(4.0)
        mp4 = w.EV / f"{a.tag}_{name}.mp4"
        rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                                "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
        t0 = time.time()
        time.sleep(1.0)
        for (x, z), size, drop, mat in DROPS:
            api.debug("spawn_gpu_particle", {"x": x, "y": w.LEVEL + size / 2 + drop + 0.01, "z": z, "material": mat, "scale": size, "lifetime": 60, "spin": 0})
        td = time.time() - t0
        rows = []
        while rec.poll() is None:
            try: fps = api.get("/api/debug/engine_timing").get("fps")
            except Exception: fps = None
            rows.append({"t": round(time.time() - t0, 2), "fps": fps})
            time.sleep(0.25)
        label = "THE WATER'S PUSH (M4)" if on else "THE OLD FLOATING RULE (today's default)"
        lines = []
        for i, r in enumerate(rows):
            t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
            since = r["t"] - td
            when = f"{since:4.1f} s after the drop" if since >= 0 else "before the drop"
            lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{label}   {when}\n{(r['fps'] or 0):.0f} fps\n")
        (w.EV / f"{a.tag}_{name}.srt").write_text("\n".join(lines), encoding="utf-8")
        api.debug("clear_dynamics")
        print(name, "recorded", flush=True)
    style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
    fc = (f"[0:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_off.srt:force_style='{style}'[l];"
          f"[1:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_on.srt:force_style='{style}'[r];[l][r]hstack=inputs=2[v]")
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_off.mp4", "-i", f"{a.tag}_on.mp4", "-filter_complex", fc, "-map", "[v]",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}_side_by_side.mp4"], check=True, cwd=str(w.EV))
    print("wrote", w.EV / f"{a.tag}_side_by_side.mp4")


if __name__ == "__main__":
    main()
