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
import argparse, atexit, pathlib, shutil, subprocess, sys, time
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
    # the "push" A/B (the old floating rule vs the push) is REMOVED: the old rule launches blocks, and the owner ruled it is never
    # run (2026-10-25). Its recording stays in docs/evidence/water_core_m4/m4_look_side_by_side.mp4.
    ap.add_argument("--ab", default="wake", choices=["wake", "refuse", "displace", "delay", "handover"],
                    help="wake: a pond SLEPT into the record, waking off vs on (32); refuse: the shipped push vs every window refused (33, A2g); "
                         "displace: debris displacement on (left) vs the shipped default, off (right) - auto-sleep on, as shipped (34, A2h); "
                         "delay: a slept pond woken at once (left, shipped) vs --delay s after the first touch (right) - the step-2 look preview (39.4); "
                         "handover: everything as shipped (slept pond, displacement on, paced wake, kept memory), the rings' hand-over off (left) vs on (right, shipped since 45.5)")
    ap.add_argument("--delay", type=float, default=0.3, help="--ab delay: the right half's wake delay, s")
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
    c0 = api.debug("water_coupling", {})   # 46: the settings as found (displacement has been ON by default since 44.6 - not forced off)
    ho0 = (api.debug("water_ripples", {"shape": False}).get("handover") or {}).get("mm")
    def restore():   # 36: the A/B controls go back to the shipped defaults even when a run dies part way (one process-wide flag)
        try:
            api.debug("water_coupling", {"wake_on_touch": True, "test_refuse_windows": False, "displace": bool(c0.get("displace", False)), "test_wake_delay_s": 0})
            if ho0 is not None: api.debug("water_ripples", {"handover_mm": ho0, "shape": False})
        except Exception as e: print("restoring the water_coupling defaults failed:", e)
    atexit.register(restore)
    for name, on in (("off", False), ("on", True)):
        if a.ab == "wake":   # 32: the shipped auto-sleep - the pond is in the record when the blocks arrive; waking off is the control
            w.PRESSURE = True; w.AUTO_SLEEP = True
            api.debug("water_coupling", {"wake_on_touch": on})
        elif a.ab == "refuse":   # 33: both sides are physically sane (measured, a2g_green_wood.json) - the push vs the safe fallback
            w.PRESSURE = True; w.AUTO_SLEEP = False
        elif a.ab == "delay":   # 39.4: ONE variable - the wake at once (left) vs delayed (right); both as shipped otherwise
            w.PRESSURE = True; w.AUTO_SLEEP = True; w.DISPLACE = False
            api.debug("water_coupling", {"test_wake_delay_s": a.delay if on else 0})
        elif a.ab == "displace":   # 34: ONE variable - left displacement on, right off (the new default); both as shipped otherwise
            w.PRESSURE = True; w.AUTO_SLEEP = True; w.DISPLACE = not on
        elif a.ab == "handover":   # 46: ONE variable - the rings' hand-over 0 (left) vs the shipped 1.5 mm (right); the camera is the viewer (near)
            w.PRESSURE = True; w.AUTO_SLEEP = True; w.DISPLACE = bool(c0.get("displace", False))
            api.debug("water_ripples", {"handover_mm": (ho0 or 1.5) if on else 0.0, "shape": False})
        else:
            raise SystemExit("only --ab wake (the old floating rule is never run)")
        w.pond(api)
        if a.ab == "refuse": api.debug("water_coupling", {"test_refuse_windows": on})
        camera_set(api, pose)
        time.sleep(4.0)
        if a.ab in ("displace", "delay", "handover"):   # as shipped: the still pond sleeps into the record before anything lands (both halves alike)
            t_s = time.time()
            while api.debug("water_av_list").get("volumes") and time.time() - t_s < 40.0: time.sleep(0.5)
            slept = not api.debug("water_av_list").get("volumes")
            print(f"  pond {'recorded' if slept else 'STILL AWAKE'} after {time.time() - t_s:.1f} s", flush=True)
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
            pond_awake = bool(api.debug("water_av_list").get("volumes")) if a.ab in ("displace", "delay", "handover") else None
            rows.append({"t": round(time.time() - t0, 2), "fps": fps, "awake": pond_awake})
            time.sleep(0.25)
        if a.ab == "wake":
            label = "A SLEEPING POND WAKES WHEN TOUCHED (32)" if on else "A SLEEPING POND, NOT WOKEN (before)"
        elif a.ab == "refuse":
            label = "EVERY PUSH WINDOW REFUSED: THE SAFE FALLBACK (33)" if on else "THE WATER'S PUSH (shipped)"
        elif a.ab == "displace":
            label = "DISPLACEMENT OFF (the new default, 34)" if on else "DISPLACEMENT ON (opt-in)"
        elif a.ab == "handover":
            label = "RINGS HAND OVER, 1.5 mm (shipped, 45.5)" if on else "NO HAND-OVER (before 45)"
        elif a.ab == "delay":
            label = f"WAKE {a.delay:.2f} s AFTER THE TOUCH (preview, 39.4)" if on else "WAKE AT ONCE (shipped)"
        else:
            label = "THE WATER'S PUSH (M4)" if on else "THE OLD FLOATING RULE (today's default)"
        lines = []
        for i, r in enumerate(rows):
            t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
            since = r["t"] - td
            when = f"{since:4.1f} s after the drop" if since >= 0 else "before the drop"
            pond = "" if r.get("awake") is None else ("   pond awake" if r["awake"] else "   pond asleep (record)")
            lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{label}   {when}\n{(r['fps'] or 0):.0f} fps{pond}\n")
        (w.EV / f"{a.tag}_{name}.srt").write_text("\n".join(lines), encoding="utf-8")
        api.debug("clear_dynamics")
        print(name, "recorded", flush=True)
    restore()
    style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
    fc = (f"[0:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_off.srt:force_style='{style}'[l];"
          f"[1:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_on.srt:force_style='{style}'[r];[l][r]hstack=inputs=2[v]")
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_off.mp4", "-i", f"{a.tag}_on.mp4", "-filter_complex", fc, "-map", "[v]",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}_side_by_side.mp4"], check=True, cwd=str(w.EV))
    print("wrote", w.EV / f"{a.tag}_side_by_side.mp4")


if __name__ == "__main__":
    main()
