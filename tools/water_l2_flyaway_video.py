"""WaterCore L2 (43.2) - the owner's look: ripple detail by distance, in motion, OFF (today) and ON side by side.

WaterBench_Creek (port 8114, Release), the 60 x 28 m lake of 27.24 (x 4-63, z 2-29, 2 m of water, GPU, auto-sleep off). 1/3 m
Stones fall 1 m onto the whole lake, one every 0.2 s at seeded points (the same points in both takes; they only fall). The camera
glides 3 m over the water along the lake's middle (z 16) looking east (+x), from x -2 to x 44 and back, 36 s - the camera is the
water's viewer (no test viewer). OFF: the ripples stop where the 384-cell window ends (17-25 m ahead, a line that jumps 4 m as the
window moves). ON (ripple_m 21, fade 8): full detail to 13 m, gone by 21 m, no line. Captioned with the setting and the camera x.

    python tools/water_l2_flyaway_video.py [--seconds 36] [--tag l2_flyaway]
Writes docs/evidence/water_core_m4/<tag>_off.mp4, <tag>_on.mp4 (+ .srt), <tag>_side_by_side.mp4 and <tag>.json.
"""
import argparse, json, math, pathlib, random, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, world_job
import water_l2_ripple_cost as lake

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8114)
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--seconds", type=float, default=36.0)
    ap.add_argument("--tag", default="l2_flyaway")
    ap.add_argument("--crop", default="1146:750:290:56", help="the viewport inside the editor window (1920 x 1080)")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    if not ff: raise SystemExit("ffmpeg not on PATH")
    api = Api(f"http://127.0.0.1:{a.port}"); st = api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    loc0 = api.debug("water_locality", {})
    api.debug("water_locality", {"clear_test_viewer": True})
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_ripples", {"enabled": True, "shape": False})
    # a sand bed under the lake (grass blades showed through the water) - world edits, re-laid every run (as 27.26's clip tool)
    bed = {"x1": lake.X0, "y1": lake.TOP - 1, "z1": lake.Z0, "x2": lake.X0 + lake.L - 1, "y2": lake.TOP - 1, "z2": lake.Z0 + lake.W - 1}
    world_job(api, "/api/world/clear", bed)
    world_job(api, "/api/world/fill", bed, material="Sand")
    y = lake.TOP + 2 + 3.0
    def cam_x(t):   # -2 -> 44 -> -2, eased at the turns
        u = (t % a.seconds) / a.seconds
        return -2.0 + 46.0 * 0.5 * (1.0 - math.cos(2.0 * math.pi * u))
    out = {"build": st.get("build_config"), "takes": {}}
    try:
        for name in ("off", "on"):
            api.debug("water_locality", {"ripple_reach": name == "on", "ripple_m": 21.0, "ripple_fade_m": 8.0})
            api.post("/api/camera", {"mode": "free", "position": {"x": cam_x(0), "y": y, "z": 16.0}, "yaw": 0.0, "pitch": -14.0})
            lake.make_lake(api, 4.0)
            rng = random.Random(11)
            mp4 = EV / f"{a.tag}_{name}.mp4"
            rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                                    "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
            t0, nxt, rows = time.time(), time.time(), []
            while rec.poll() is None and time.time() - t0 < a.seconds + 5:
                t = time.time() - t0
                cx = cam_x(t)
                api.post("/api/camera", {"mode": "free", "position": {"x": cx, "y": y, "z": 16.0}, "yaw": 0.0, "pitch": -14.0})
                if time.time() >= nxt:
                    api.debug("spawn_gpu_particle", {"x": lake.X0 + 1 + rng.random() * (lake.L - 2), "y": lake.TOP + 3.17,
                                                     "z": lake.Z0 + 1 + rng.random() * (lake.W - 2), "material": "Stone", "scale": 1 / 3,
                                                     "lifetime": 30, "spin": 0})
                    nxt += 0.2
                if not rows or t - rows[-1]["t"] >= 0.25:
                    rp = api.debug("water_ripples", {"shape": False})
                    try: fps = api.get("/api/debug/engine_timing").get("fps")
                    except Exception: fps = None
                    rows.append({"t": round(t, 2), "cam_x": round(cx, 1), "fps": fps, "window_side": rp.get("window_side"),
                                 "fade": rp.get("fade"), "awake": rp.get("awake")})
                time.sleep(1.0 / 30.0)
            rec.wait()
            out["takes"][name] = rows
            label = "RIPPLE REACH ON (full to 13 m, gone by 21 m)" if name == "on" else "RIPPLE REACH OFF (today: the window's edge)"
            lines = []
            for i, r in enumerate(rows):
                t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
                lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{label}\ncamera x {r['cam_x']:5.1f} | {(r['fps'] or 0):.0f} fps\n")
            (EV / f"{a.tag}_{name}.srt").write_text("\n".join(lines), encoding="utf-8")
            api.debug("clear_dynamics")
            print(f"{name}: {len(rows)} rows, last {rows[-1] if rows else None}", flush=True)
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
        style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
        fc = (f"[0:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_off.srt:force_style='{style}'[l];"
              f"[1:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_on.srt:force_style='{style}'[r];[l][r]hstack=inputs=2[v]")
        subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_off.mp4", "-i", f"{a.tag}_on.mp4", "-filter_complex", fc, "-map", "[v]",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}_side_by_side.mp4"], check=True, cwd=str(EV))
        print("wrote", EV / f"{a.tag}_side_by_side.mp4")
    finally:
        api.debug("clear_dynamics"); lake.destroy_all(api)
        api.debug("water_locality", {"ripple_reach": loc0.get("ripple_reach", False), "ripple_m": loc0.get("ripple_m", 21.0), "ripple_fade_m": loc0.get("ripple_fade_m", 8.0)})


if __name__ == "__main__":
    main()
