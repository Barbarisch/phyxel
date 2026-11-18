"""WaterCore.md §55.4 (W6.2a) - the owner's clip: the SHORE BAND - a simulated box where the sea meets the beach, following the
camera along the shore, its sea edge driven by the swell (water_waves), the swash running up the sand.

WaterBench_Coast (port 8109), the engine's defaults (the follow ON, swell 0.45 m / 14 m, wind 0.6 rad). The camera is driven by the
engine (camera_path, constant 5 m/s): along the beach at eye height (x 130 -> 190, z 690, y 18.5 - the sea from z ~686-698), then up
to 30 m over the band. Captioned with the phase, the band (makes), the frame rate. Before the clip: two stills at the same eye-height
pose - the swell ON, then amplitude 0 (the band remade: its columns copy the swell when made) - <tag>_still_swell.png / _calm.png.
§55.5 (W6.2b): the sea beyond the band is drawn with the band's own swell (water_sea_draw, default ON); one more still with that draw
OFF (today's look before W6.2b: the band a slab over a flat sea) - <tag>_still_seaoff.png.

    python tools/water_w62_clip.py [--tag w62_clip]
Writes docs/evidence/water_core_m4/<tag>.mp4 / .srt / .json and the stills.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_w5_clip import srt_time

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
EYE = {"x": 160.0, "y": 18.5, "z": 684.0, "yaw": 90.0, "pitch": -8.0}
PATH = [{"x": 130.0, "y": 18.5, "z": 684.0, "yaw": 90.0, "pitch": -8.0},
        {"x": 190.0, "y": 18.5, "z": 684.0, "yaw": 90.0, "pitch": -8.0},
        {"x": 190.0, "y": 48.0, "z": 668.0, "yaw": 90.0, "pitch": -40.0}]


def follow(api, body=None):
    return api.post("/api/debug/water_av_shore_follow", body or {})


def wait_band(api, s=20.0):
    t0 = time.time()
    while time.time() - t0 < s:
        st = follow(api)
        if st.get("band_id"): return st
        time.sleep(0.5)
    return follow(api)


def still(ff, window, crop, path):
    raw = path.with_suffix(".raw.png")
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={window}", "-frames:v", "1", str(raw)], check=True)
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", str(raw), "-vf", f"crop={crop}", str(path)], check=True)
    raw.unlink(missing_ok=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8109")
    ap.add_argument("--window", default="WaterBench_Coast")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--tag", default="w62_clip")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    print("realtime:", api.debug("water_av_realtime", {"on": True}))
    api.debug("water_locality", {"clear_test_viewer": True})
    waves = api.debug("water_waves", {})
    amp = waves.get("amplitude", 0.45)
    # the stills: the swell ON, then calm (the band remade so its columns take the new amplitude)
    camera_set(api, EYE); follow(api, {"on": True}); st = wait_band(api); time.sleep(8.0)
    still(ff, a.window, a.crop, EV / f"{a.tag}_still_swell.png")
    api.post("/api/debug/water_sea_draw", {"on": False}); time.sleep(1.0)
    still(ff, a.window, a.crop, EV / f"{a.tag}_still_seaoff.png")
    api.post("/api/debug/water_sea_draw", {"on": True}); time.sleep(1.0)
    api.debug("water_waves", {"amplitude": 0.0}); follow(api, {"on": False}); time.sleep(1.0); follow(api, {"on": True}); wait_band(api); time.sleep(8.0)
    still(ff, a.window, a.crop, EV / f"{a.tag}_still_calm.png")
    api.debug("water_waves", {"amplitude": amp}); follow(api, {"on": False}); time.sleep(1.0)
    # the clip
    camera_set(api, PATH[0]); follow(api, {"on": True}); wait_band(api); time.sleep(4.0)
    s0 = follow(api)
    seconds = 24.0   # the path ends at ~22 s (then the free camera returns to its own pose)
    raw = EV / f"{a.tag}_raw.mp4"
    rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(seconds), "-i", f"title={a.window}",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", str(raw)])
    t0 = time.time(); started = None; rows = []
    while rec.poll() is None:
        t = time.time() - t0
        if started is None and t >= 3.0:
            api.post("/api/camera/path", {"waypoints": PATH, "speed_u_per_s": 5.0, "loop": False, "stream_follow": False}); started = round(t, 2)
        try:
            st = follow(api); fps = api.get("/api/debug/engine_timing").get("fps")
            cam = api.get("/api/camera/path").get("camera") or api.get("/api/camera").get("position", {})   # /api/camera does not see the path's pose
        except Exception: st, fps, cam = {}, None, {}
        rows.append({"t": round(t, 2), "fps": fps, "band": st.get("band_id"), "makes": (st.get("makes") or 0) - s0["makes"],
                     "make_ms": st.get("last_make_ms"), "release_ms": (st.get("release_ms") or {}).get("max"), "cam": [cam.get("x"), cam.get("y"), cam.get("z")]})
        time.sleep(1.0)   # one caption poll a second: requests slow the frames on this bench (the recorder pass: no polls)
    lines = []
    for i, r in enumerate(rows):
        t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 1.0
        y = r["cam"][1] or 0.0
        if started is None or r["t"] < started: ph = "the shore band (simulated) - the sea beyond drawn with the same swell"
        elif y < 20.0: ph = "walking the beach at 5 m/s - the band follows (each swap stalls a frame: shown)"
        else: ph = "up to 30 m over the band"
        lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{ph}\nt {r['t']:.1f} s | {(r['fps'] or 0):.0f} fps | band {r['band']} | swaps {r['makes']}\n")
    (EV / f"{a.tag}.srt").write_text("\n".join(lines), encoding="utf-8")
    out = {"bench": "WaterBench_Coast", "path": PATH, "eye": EYE, "swell": waves, "sea_draw": api.post("/api/debug/water_sea_draw", {}), "follow_at_start": s0, "follow_at_end": follow(api), "rows": rows}
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", raw.name, "-vf", f"crop={a.crop},scale=1280:-2,subtitles={a.tag}.srt:force_style='{style}'",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}.mp4"], check=True, cwd=str(EV))
    raw.unlink(missing_ok=True)
    print("wrote", EV / f"{a.tag}.mp4")


if __name__ == "__main__":
    main()
