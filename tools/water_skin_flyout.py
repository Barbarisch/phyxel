"""WaterCore.md 51.4 S3 - the owner's fly-out clip: the 150 x 150 m lake in the record (water_record_lake_clip.py's bed and kerb), the
camera starting low over the lake's near shore, pulling back and up to ~400 m from the lake's centre, holding, and coming back - at the
DEFAULT record radius (draw_m 48 m). Two takes, the skin OFF (today's drawing: the record tiles only) and ON, joined side by side.
Each take is captioned live with the camera's distance to the lake's centre, the frame rate and the skin's state. The camera is moved
over the API (~25 updates a second). Recorded from the window (gdigrab: /api/screenshot omits pond water).

    python tools/water_skin_flyout.py [--seconds 14] [--tag skin150_flyout]
Writes docs/evidence/water_core_m4/<tag>_off.mp4, <tag>_on.mp4 (+ .srt), <tag>.mp4 (side by side) and <tag>.json.
"""
import argparse, json, math, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_record_lake_clip import EV, CX, CZ, TOP, W, Z0, lay_lake, destroy_all, srt_time, splash


def pose(u):
    """u 0..1 along the path: from 12 m over the near (south) shore out to ~400 m back and ~120 m up, looking at the lake's centre."""
    e = 0.5 - 0.5 * math.cos(math.pi * u)   # ease in and out
    z = (Z0 + W + 6.0) + e * 380.0
    y = (TOP + 12.0) + e * 110.0
    dz, dy = z - CZ, y - TOP
    return {"x": CX, "y": y, "z": z, "yaw": -90.0, "pitch": -math.degrees(math.atan2(dy, dz)) * 0.9}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--seconds", type=float, default=14.0, help="each leg (out, back); a 3 s hold at each end")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--tag", default="skin150_flyout")
    ap.add_argument("--splash", action="store_true",
                    help="51.7 (S4's clip): start FAR, fly in, splash at the lake's centre (a patch wakes), hold, fly out with the patch alive")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    api.debug("water_av_realtime", {"on": True})
    draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
    skin0 = api.debug("water_av_skin", {})
    api.debug("water_av_record_draw", {"radius": 48.0})
    out = {"draw_m": 48.0, "takes": {}}
    total = 3.0 + a.seconds + 3.0 + a.seconds + 3.0
    try:
        out["record"] = lay_lake(api)
        for mode in ("off", "on"):
            api.debug("water_av_skin", {"on": mode == "on"})
            camera_set(api, pose(1.0 if a.splash else 0.0)); time.sleep(4.0)
            mp4 = EV / f"{a.tag}_{mode}.mp4"
            rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", f"{total:.1f}", "-i", f"title={a.window}",
                                    "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
            t0 = time.time(); rows = []; last_row = -1.0; splashed = False
            while rec.poll() is None:
                t = time.time() - t0
                if a.splash:   # far -> in -> splash + hold -> out -> far
                    if t < 3.0: u = 1.0
                    elif t < 3.0 + a.seconds: u = 1.0 - (t - 3.0) / a.seconds
                    elif t < 6.0 + a.seconds:
                        u = 0.0
                        if not splashed: splash(api); splashed = True
                    elif t < 6.0 + 2 * a.seconds: u = (t - 6.0 - a.seconds) / a.seconds
                    else: u = 1.0
                elif t < 3.0: u = 0.0
                elif t < 3.0 + a.seconds: u = (t - 3.0) / a.seconds
                elif t < 6.0 + a.seconds: u = 1.0
                elif t < 6.0 + 2 * a.seconds: u = 1.0 - (t - 6.0 - a.seconds) / a.seconds
                else: u = 0.0
                p = pose(u)
                api.post("/api/camera", {"mode": "free", "position": {"x": p["x"], "y": p["y"], "z": p["z"]}, "yaw": p["yaw"], "pitch": p["pitch"]})
                if t - last_row >= 0.25:
                    last_row = t
                    try: fps = api.get("/api/debug/engine_timing").get("fps")
                    except Exception: fps = None
                    sk = api.debug("water_av_skin", {})
                    try: vols = len(api.debug("water_av_list", {}).get("volumes", []))
                    except Exception: vols = None
                    rows.append({"t": round(t, 2), "dist_m": round(math.hypot(p["z"] - CZ, p["y"] - TOP), 1), "fps": fps,
                                 "skin_cells": sk.get("wet_cells"), "skin_levels": sk.get("levels"), "skin_ms": sk.get("ms_last_call"), "volumes": vols})
                time.sleep(0.04)
            out["takes"][mode] = rows
            if a.splash: time.sleep(30.0)   # the patch back in the record before the next take
            lines = []
            for i, r in enumerate(rows):
                t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
                state = f"skin ON: {r['skin_cells']} cells, {r['skin_levels']} levels" if mode == "on" else "skin OFF: the record tiles only (48 m round the camera)"
                if a.splash: state += " | patch live" if r.get("volumes") else " | no patch"
                lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{state}\n{r['dist_m']:.0f} m from the lake's centre | {(r['fps'] or 0):.0f} fps\n")
            (EV / f"{a.tag}_{mode}.srt").write_text("\n".join(lines), encoding="utf-8")
            fl = [r["fps"] for r in rows if r["fps"]]
            print(f"{mode}: {len(rows)} rows, fps mean {sum(fl) / max(1, len(fl)):.0f} min {min(fl) if fl else 0:.0f}", flush=True)
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
        style = "FontName=Arial,FontSize=11,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
        fc = (f"[0:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_off.srt:force_style='{style}'[a];"
              f"[1:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_on.srt:force_style='{style}'[b];[a][b]hstack=inputs=2[v]")
        subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_off.mp4", "-i", f"{a.tag}_on.mp4", "-filter_complex", fc, "-map", "[v]",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}.mp4"], check=True, cwd=str(EV))
        print("wrote", EV / f"{a.tag}.mp4")
    finally:
        destroy_all(api)
        api.debug("water_av_skin", {"on": bool(skin0.get("on", False))})
        api.debug("water_av_record_draw", {"radius": draw0})


if __name__ == "__main__":
    main()
