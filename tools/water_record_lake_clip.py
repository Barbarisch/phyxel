"""WaterCore.md 51.3 - the owner's clip of a lake that exists only as the record: the 150 x 150 m lake written straight into the record
(water_av_record_lake: spans + one slept box, no volume - 1,350 x 27 x 1,350 cells, over the 16 M-cell volume cap), the same visible
splash as the live measure (water_av_impulse at the centre, 2 m / strength 8) waking a PATCH (28 x 28 m), the patch living, sleeping,
writing back. Recorded from the window (gdigrab: /api/screenshot omits pond water), each take captioned live (~4 a second) with the
phase, the frame rate, the live volumes, the awake columns and the patch's box.

Two takes, each its own splash: WIDE (most of the lake in frame - the patch is a small part of it) and CLOSE (the patch fills the frame).
They are joined one after the other into <tag>.mp4.

The rig: WaterBench_Creek (port 8114, Release); a sand bed + a stone kerb two high round the lake (world edits, re-laid each run); the
engine's defaults except: the record drawn 200 m round the view (48 m by default - from the wide pose the far lake would not be drawn).

    python tools/water_record_lake_clip.py --still wide|close     # framing check: one frame
    python tools/water_record_lake_clip.py [--seconds 40] [--tag lake150_clip]
Writes docs/evidence/water_core_m4/<tag>_wide.mp4, <tag>_close.mp4 (+ .srt), <tag>.mp4 and <tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
X0, Z0, TOP, W = 4, 2, 16, 150
CX, CZ = X0 + W / 2, Z0 + W / 2
POSES = {"wide": {"x": CX, "y": TOP + 46.0, "z": Z0 + W + 20.0, "yaw": -90.0, "pitch": -32.0},
         "close": {"x": CX, "y": TOP + 16.0, "z": CZ + 26.0, "yaw": -90.0, "pitch": -34.0}}


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def lay_lake(api):
    destroy_all(api)
    world_job(api, "/api/world/clear", {"x1": X0, "y1": TOP - 1, "z1": Z0, "x2": X0 + W - 1, "y2": TOP + 3, "z2": Z0 + W - 1})
    world_job(api, "/api/world/fill", {"x1": X0, "y1": TOP - 1, "z1": Z0, "x2": X0 + W - 1, "y2": TOP - 1, "z2": Z0 + W - 1}, material="Sand")
    for b in ((X0 - 1, Z0 - 1, X0 - 1, Z0 + W), (X0 + W, Z0 - 1, X0 + W, Z0 + W), (X0 - 1, Z0 - 1, X0 + W, Z0 - 1), (X0 - 1, Z0 + W, X0 + W, Z0 + W)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": TOP, "z1": b[1], "x2": b[2], "y2": TOP + 1, "z2": b[3]}, material="StoneBricks")
    r = api.debug("water_av_record_lake", {"x1": X0, "z1": Z0, "x2": X0 + W - 1, "z2": Z0 + W - 1, "bottom": TOP, "top": TOP + 2})
    if not r.get("success"): raise SystemExit(f"record lake refused: {r}")
    return r


def splash(api):
    return api.debug("water_av_impulse", {"x": CX, "y": TOP + 1.9, "z": CZ, "radius": 2.0, "strength": 8.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--seconds", type=float, default=40.0)
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--still", default=None, help="wide|close: one framing frame (with --still-after: splash, wait, then the frame)")
    ap.add_argument("--still-after", type=float, default=0.0)
    ap.add_argument("--tag", default="lake150_clip")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    api.debug("water_av_realtime", {"on": True})
    draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
    api.debug("water_av_record_draw", {"radius": 200.0})
    out = {"box": [X0, Z0, W, W], "splash": {"radius": 2.0, "strength": 8.0}, "takes": {}}
    try:
        if a.still:
            out["record"] = lay_lake(api)
            camera_set(api, POSES[a.still]); time.sleep(4.0)
            if a.still_after > 0: splash(api); time.sleep(a.still_after)
            png = EV / f"{a.tag}_{a.still}_still.png"
            subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={a.window}", "-frames:v", "1", "-vf", f"crop={a.crop}", str(png)], check=True)
            print("wrote", png); return
        for name in ("wide", "close"):
            rec_lake = lay_lake(api)
            camera_set(api, POSES[name]); time.sleep(5.0)
            mp4 = EV / f"{a.tag}_{name}.mp4"
            rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                                    "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
            t0 = time.time()
            time.sleep(3.0)
            k = splash(api)
            tp = time.time() - t0
            rows, back = [], None
            while rec.poll() is None:
                vols = api.debug("water_av_list").get("volumes", [])
                try: fps = api.get("/api/debug/engine_timing").get("fps")
                except Exception: fps = None
                awake = sum(v.get("awake_columns", 0) for v in vols if not v.get("asleep"))
                t = round(time.time() - t0, 2)
                if t > tp and not vols and back is None: back = round(t - tp, 1)
                rows.append({"t": t, "fps": fps, "volumes": len(vols), "awake": awake, "box": [v.get("box") for v in vols]})
                time.sleep(0.25)
            out["takes"][name] = {"pose": POSES[name], "record": rec_lake, "splash": k, "splash_at_s": round(tp, 2), "back_in_record_s": back, "rows": rows}
            lines = []
            for i, r in enumerate(rows):
                t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
                if r["t"] < tp: phase = "150 m lake - all of it the record (no volume)"
                elif r["volumes"]: phase = f"{r['t'] - tp:4.1f} s after the splash - a patch simulates"
                else: phase = f"{r['t'] - tp:4.1f} s after the splash - back in the record"
                bx = r["box"][0] if r["box"] else None
                patch = f"patch {bx['max'][0] - bx['min'][0] + 1} x {bx['max'][2] - bx['min'][2] + 1} m" if bx else "no patch"
                lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{name.upper()}   {phase}\n"
                             f"{(r['fps'] or 0):.0f} fps | {r['volumes']} live volume(s) | {r['awake']} columns awake | {patch}\n")
            (EV / f"{a.tag}_{name}.srt").write_text("\n".join(lines), encoding="utf-8")
            print(f"{name}: splash {k}, back in the record {back} s after it, last row {rows[-1]}", flush=True)
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
        style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
        fc = (f"[0:v]crop={a.crop},scale=1280:-2,subtitles={a.tag}_wide.srt:force_style='{style}'[a];"
              f"[1:v]crop={a.crop},scale=1280:-2,subtitles={a.tag}_close.srt:force_style='{style}'[b];[a][b]concat=n=2:v=1:a=0[v]")
        subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_wide.mp4", "-i", f"{a.tag}_close.mp4", "-filter_complex", fc, "-map", "[v]",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}.mp4"], check=True, cwd=str(EV))
        print("wrote", EV / f"{a.tag}.mp4")
    finally:
        destroy_all(api)
        api.debug("water_av_record_draw", {"radius": draw0})


if __name__ == "__main__":
    main()
