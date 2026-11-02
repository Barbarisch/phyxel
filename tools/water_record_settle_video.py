"""WaterCore.md 50.6d - the owner's look at recorded tiles after the pressure fix: the same 60 x 60 m lake, 2 m deep, the same push at
its centre, twice - tile recording OFF (the shipped default) and ON (still lake parts stored as the record; only the water round the
disturbance simulated) - recorded from the window (gdigrab: /api/screenshot omits pond water), side by side, each half captioned
live (~4 a second) with the frame rate, the awake columns, the recorded tiles and the solver's time.

The rig: WaterBench_Creek (port 8114, Release), the lake tool's box (x 4-63, z 2-61, y 16-18) with a stone kerb two high round it
(world edits, re-laid each run); the engine's defaults (fixed 1/60 s tick, partial wake, freeze x10, tall band, 20 sweeps) except:
auto-sleep off; the record drawn 120 m round the view (48 m by default: from this camera the far half of the lake was not drawn).
The ON take waits until the still lake is all in the record (64 tiles) before the push.

    python tools/water_record_settle_video.py --still      # framing check: one frame
    python tools/water_record_settle_video.py [--seconds 45] [--tag rec_settle]
Writes docs/evidence/water_core_h1/<tag>_off.mp4, <tag>_on.mp4 (+ .srt), <tag>_side_by_side.mp4 and <tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
H, TOP, X0, Z0, L, W = 1.0 / 9.0, 16, 4, 2, 60, 60
TILES = (L // 8 + (L % 8 > 0)) * (W // 8 + (W % 8 > 0))


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def vol(api, vid):
    return next((v for v in api.debug("water_av_list")["volumes"] if v["id"] == vid), {})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--seconds", type=float, default=45.0)
    ap.add_argument("--settle", type=float, default=8.0)
    ap.add_argument("--strength", type=float, default=4.0)
    ap.add_argument("--radius", type=float, default=0.5, help="the push's radius (m)")
    ap.add_argument("--still-after", type=float, default=0.0, help="with --still: push, wait this long (s), then the frame")
    ap.add_argument("--pose", default=None, help="x,y,z,yaw,pitch")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--still", action="store_true")
    ap.add_argument("--tag", default="rec_settle")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(300)
    pose = {"x": X0 + L / 2, "y": TOP + 0.95 * L, "z": Z0 + W + 0.55 * L, "yaw": -90.0, "pitch": -52.0}
    if a.pose:
        p = [float(v) for v in a.pose.split(",")]; pose = {"x": p[0], "y": p[1], "z": p[2], "yaw": p[3], "pitch": p[4]}
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    # a sand bed under the lake (grass blades showed through the water) - world edits, re-laid every run
    world_job(api, "/api/world/clear", {"x1": X0, "y1": TOP - 1, "z1": Z0, "x2": X0 + L - 1, "y2": TOP - 1, "z2": Z0 + W - 1})
    world_job(api, "/api/world/fill", {"x1": X0, "y1": TOP - 1, "z1": Z0, "x2": X0 + L - 1, "y2": TOP - 1, "z2": Z0 + W - 1}, material="Sand")
    for b in ((X0 - 1, Z0 - 1, X0 - 1, Z0 + W), (X0 + L, Z0 - 1, X0 + L, Z0 + W), (X0 - 1, Z0 - 1, X0 + L, Z0 - 1), (X0 - 1, Z0 + W, X0 + L, Z0 + W)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": TOP, "z1": b[1], "x2": b[2], "y2": TOP + 1, "z2": b[3]}, material="StoneBricks")
    api.debug("water_av_realtime", {"on": True})
    rec0 = api.debug("water_av_record_tiles", {}).get("default", False)
    draw0 = api.debug("water_av_record_draw", {}).get("radius_m", 48.0)
    api.debug("water_av_record_draw", {"radius": 120.0})   # the rig: the record is drawn 48 m round the view by default - the far lake vanished
    out = {"pose": pose, "box": [X0, Z0, L, W], "strength": a.strength, "radius": a.radius, "takes": {}}

    def lake(record):
        destroy_all(api)
        box = {"x1": X0, "y1": TOP, "z1": Z0, "x2": X0 + L - 1, "y2": TOP + 2, "z2": Z0 + W - 1}
        api.debug("water_av_record_tiles", {"default": record})
        r = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False})
        api.debug("water_av_record_tiles", {"default": rec0})
        if "error" in r: raise SystemExit("create refused: " + r["error"])
        vid = r["volume"]["id"]
        for k in range(2): api.debug("place_water_box", {"target": "core", **box, "y1": TOP + k, "y2": TOP + k, "mass": 1.0})
        camera_set(api, pose)
        t0 = time.time()
        while time.time() - t0 < 60:
            v = vol(api, vid)
            full = v.get("mass", 0) + v.get("recorded_m3", 0) >= 0.99 * L * W * 2
            if full and (not record or v.get("recorded_tiles", 0) >= TILES): break
            time.sleep(0.5)
        time.sleep(a.settle)
        return vid

    try:
        if a.still:
            lake(True)
            if a.still_after > 0:
                api.debug("water_av_impulse", {"x": X0 + L / 2, "y": TOP + 1.9, "z": Z0 + W / 2, "radius": a.radius, "strength": a.strength, "dx": 0.0, "dy": -1.0, "dz": 0.0})
                time.sleep(a.still_after)
            subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={a.window}", "-frames:v", "1", "-vf", f"crop={a.crop}", str(EV / f"{a.tag}_still.png")], check=True)
            print("wrote", EV / f"{a.tag}_still.png"); return
        for name, record in (("off", False), ("on", True)):
            vid = lake(record)
            mp4 = EV / f"{a.tag}_{name}.mp4"
            rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                                    "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
            t0 = time.time()
            time.sleep(1.5)
            api.debug("water_av_impulse", {"x": X0 + L / 2, "y": TOP + 1.9, "z": Z0 + W / 2, "radius": a.radius, "strength": a.strength, "dx": 0.0, "dy": -1.0, "dz": 0.0})
            tp = time.time() - t0
            rows = []
            while rec.poll() is None:
                v = vol(api, vid)
                try: fps = api.get("/api/debug/engine_timing").get("fps")
                except Exception: fps = None
                rows.append({"t": round(time.time() - t0, 2), "fps": fps, "ms": v.get("gpu_device_ms"), "awake": v.get("awake_columns"),
                             "tiles": v.get("recorded_tiles"), "asleep": v.get("asleep"), "mass": v.get("mass", 0) + v.get("recorded_m3", 0)})
                time.sleep(0.25)
            out["takes"][name] = {"record": record, "push_at_s": round(tp, 2), "rows": rows}
            label = "TILE RECORDING ON" if record else "TILE RECORDING OFF (shipped)"
            lines = []
            for i, r in enumerate(rows):
                t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
                ms = 0.0 if r["asleep"] else (r["ms"] or 0.0)
                since = max(0.0, r["t"] - tp)
                lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{label}   {since:4.1f} s after the push\n"
                             f"{(r['fps'] or 0):.0f} fps | {r['awake'] or 0} columns awake | {r['tiles'] or 0}/{TILES} tiles recorded | solver {ms:.2f} ms\n")
            (EV / f"{a.tag}_{name}.srt").write_text("\n".join(lines), encoding="utf-8")
            last = rows[-1]
            print(f"{name}: last row {last}", flush=True)
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
        style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
        fc = (f"[0:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_off.srt:force_style='{style}'[l];"
              f"[1:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_on.srt:force_style='{style}'[r];[l][r]hstack=inputs=2[v]")
        subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_off.mp4", "-i", f"{a.tag}_on.mp4", "-filter_complex", fc, "-map", "[v]",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}_side_by_side.mp4"], check=True, cwd=str(EV))
        print("wrote", EV / f"{a.tag}_side_by_side.mp4")
    finally:
        destroy_all(api)
        api.debug("water_av_record_draw", {"radius": draw0})
        api.debug("water_av_record_tiles", {"default": rec0})


if __name__ == "__main__":
    main()
