"""WaterCore A10 (44.3 item 8) - the owner's look at the size floor: does a 1/9 m block's shove show? Displacement ON both halves.

The Small bench (port 8111), Release, the S9 pit (8 x 8 m, 4 m of water - water_a2g_safe's), auto-sleep off. Into it, from 1 m
above the water: 400 1/9 m Wood blocks in four heaps (the debris of a break), then three 1/3 m Wood blocks and a 1/3 m Stone.
LEFT: no floor (`displace_min_half_m` 0 - every size shoves, the moonshot). RIGHT: the floor 1/6 m (the default - the 1/9 m blocks
only ripple, the 1/3 m blocks shove). Recorded from the window (gdigrab: /api/screenshot omits pond water), captioned with the
setting, the bodies shoving and the fps. Nothing rises: every block falls in from rest.

    python tools/water_displace_floor_video.py [--seconds 14] [--tag displace_floor]
Writes docs/evidence/water_core_m4/<tag>_{floor0,floor16}.mp4 (+ .srt), <tag>_side_by_side.mp4 and <tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_a2g_safe as g

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def pit(api):
    api.debug("clear_dynamics")
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})
    box = {**g.PIT_BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto", "auto_sleep": False}
    cr = api.debug("water_av_create", box)
    if "error" in cr and cr.get("recorded_m3"):
        cr = api.debug("water_av_create", {**box, "seed": "spans"}); api.debug("water_av_destroy", {"id": cr["volume"]["id"], "discard": True})
        cr = api.debug("water_av_create", box)
    if "error" in cr: raise SystemExit(f"pit: {cr}")
    api.debug("place_water_box", {**g.PIT_WATER, "mass": 1.0, "target": "core"})
    api.debug("water_av_realtime", {"on": True})
    time.sleep(4.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--window", default="WaterBench_Small")
    ap.add_argument("--seconds", type=float, default=14.0)
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--tag", default="displace_floor")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); st = api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    c0 = api.debug("water_coupling", {})
    gate0 = c0.get("displace_gate", {})
    out = {"build": st.get("build_config"), "takes": {}}
    try:
        for name, floor in (("floor0", 0.0), ("floor16", 1.0 / 6.0)):
            api.debug("water_coupling", {"enabled": True, "solids": True, "pressure_force": True, "displace": True, "displace_min_half_m": floor})
            pit(api)
            api.post("/api/camera", {"mode": "free", "position": {"x": 153.5, "y": 21.5, "z": 4.0}, "yaw": 90, "pitch": -38})
            time.sleep(1.0)
            mp4 = EV / f"{a.tag}_{name}.mp4"
            rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                                    "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
            t0 = time.time(); rows = []
            def sample_until(t_end):   # the captions' rows, from the start of the take (the small blocks' phase included)
                while rec.poll() is None and time.time() - t0 < t_end:
                    c = api.debug("water_coupling", {})
                    try: fps = api.get("/api/debug/engine_timing").get("fps")
                    except Exception: fps = None
                    last = (c.get("displace_gate") or {}).get("last") or {}
                    rows.append({"t": round(time.time() - t0, 2), "fps": fps, "shoving": (last.get("kept", 0) + last.get("admitted", 0)),
                                 "rasterized": (c.get("moving_solids") or {}).get("bodies")})
                    time.sleep(0.25)
            sample_until(1.0)
            for (x, z) in [(151.5, 11.5), (155.5, 11.5), (151.5, 15.5), (155.5, 15.5)]:
                api.debug("spawn_gpu_particle", {"x": x, "y": g.PIT_LEVEL + 1.0, "z": z, "material": "Wood", "scale": 1 / 9, "count": 100, "lifetime": 60, "spin": 0})
            t_small = time.time() - t0
            sample_until(t_small + 4.0)
            for (x, z, mat) in [(152.5, 13.0, "Wood"), (154.5, 13.0, "Wood"), (153.5, 15.0, "Wood"), (153.5, 11.5, "Stone")]:
                api.debug("spawn_gpu_particle", {"x": x, "y": g.PIT_LEVEL + 1.0 + 1 / 6, "z": z, "material": mat, "scale": 1 / 3, "lifetime": 60, "spin": 0})
            t_big = time.time() - t0
            sample_until(1e9)
            out["takes"][name] = {"floor_m": floor, "small_at_s": round(t_small, 2), "big_at_s": round(t_big, 2), "rows": rows,
                                  "gate_total": (api.debug("water_coupling", {}).get("displace_gate") or {}).get("total")}
            label = "NO SIZE FLOOR: every block shoves (1/9 m too)" if floor == 0.0 else "FLOOR 1/6 m: 1/9 m blocks ripple, 1/3 m blocks shove"
            lines = []
            for i, r in enumerate(rows):
                t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
                lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{label}\n{r['rasterized'] or 0} bodies shoving | {(r['fps'] or 0):.0f} fps\n")
            (EV / f"{a.tag}_{name}.srt").write_text("\n".join(lines), encoding="utf-8")
            print(name, "rows", len(rows), "max rasterized", max((r["rasterized"] or 0) for r in rows), "fps min", min((r["fps"] or 0) for r in rows), flush=True)
            api.debug("clear_dynamics")
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
        style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
        fc = (f"[0:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_floor0.srt:force_style='{style}'[l];"
              f"[1:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_floor16.srt:force_style='{style}'[r];[l][r]hstack=inputs=2[v]")
        subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_floor0.mp4", "-i", f"{a.tag}_floor16.mp4", "-filter_complex", fc, "-map", "[v]",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}_side_by_side.mp4"], check=True, cwd=str(EV))
        print("wrote", EV / f"{a.tag}_side_by_side.mp4")
    finally:
        api.debug("clear_dynamics")
        api.debug("water_coupling", {"displace": c0.get("displace", False), "displace_min_half_m": gate0.get("min_half_m", 1.0 / 6.0)})


if __name__ == "__main__":
    main()
