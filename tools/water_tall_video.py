"""WaterCore.md 27.22 TC5 - the owner's look at tall cells: the same push into the same 2 m pond four times - all fine, and tall cells
under a band of 0.44 / 0.67 / 0.89 m - recorded from the window (gdigrab: /api/screenshot omits pond water) and laid out 2 x 2, each
panel captioned with its setting, the solver's time and the tall columns (polled live).

The rig: WaterBench_Creek, a 24 x 24 m pond at x 4-27, z 2-25 (inside one chunk), 2 m deep, a sand bed and a stone kerb (world edits,
re-laid each run), four floating wood pieces near the push; the shipped settings (partial wake on, 20 sweeps); a repeatable push
(water_av_impulse, strength --strength) 6 m in from the near corner, the camera low beside it.

    python tools/water_tall_video.py --still          # framing check: one frame
    python tools/water_tall_video.py [--seconds 14] [--tag tall_look]
Writes docs/evidence/water_core_h1/<tag>_<take>.mp4 / .srt, <tag>_grid.mp4 and <tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
X0, Z0, L, DEPTH, H = 4, 2, 24, 2, 1.0 / 9.0
TAKES = [("fine", 0.0), ("tall_044", 0.44), ("tall_067", 0.67), ("tall_089", 0.89)]


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--window", default="WaterBench_Creek")
    ap.add_argument("--seconds", type=float, default=14.0)
    ap.add_argument("--strength", type=float, default=4.0)
    ap.add_argument("--pose", default="15,18.9,29.5,-118,-15")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--still", action="store_true")
    ap.add_argument("--tag", default="tall_look")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(300)
    p = [float(v) for v in a.pose.split(",")]; pose = {"x": p[0], "y": p[1], "z": p[2], "yaw": p[3], "pitch": p[4]}
    sx, sz = X0 + 6.0, Z0 + L - 6.0
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    world_job(api, "/api/world/clear", {"x1": X0, "y1": 15, "z1": Z0, "x2": X0 + L - 1, "y2": 15, "z2": Z0 + L - 1})
    world_job(api, "/api/world/fill", {"x1": X0, "y1": 15, "z1": Z0, "x2": X0 + L - 1, "y2": 15, "z2": Z0 + L - 1}, material="Sand")
    for b in ((X0 - 1, Z0 - 1, X0 - 1, Z0 + L), (X0 + L, Z0 - 1, X0 + L, Z0 + L), (X0 - 1, Z0 - 1, X0 + L, Z0 - 1), (X0 - 1, Z0 + L, X0 + L, Z0 + L)):
        world_job(api, "/api/world/fill", {"x1": b[0], "y1": 16, "z1": b[1], "x2": b[2], "y2": 15 + DEPTH, "z2": b[3]}, material="StoneBricks")
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_coupling", {"enabled": True, "solids": True, "displace": True})
    out = {"pose": pose, "push_at": [sx, sz], "strength": a.strength, "takes": {}}

    def pond(band):
        for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})
        box = {"x1": X0, "y1": 16, "z1": Z0, "x2": X0 + L - 1, "y2": 16 + DEPTH, "z2": Z0 + L - 1}
        vid = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False, "tall_band_m": band})["volume"]["id"]
        for k in range(DEPTH): api.debug("place_water_box", {"target": "core", **box, "y1": 16 + k, "y2": 16 + k, "mass": 1.0})
        camera_set(api, pose)
        t0 = time.time()
        while band > 0 and time.time() - t0 < 40 and not next(x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid).get("tall_active"): time.sleep(0.5)
        time.sleep(3.0)
        for k in range(4):   # floating wood near the push (the debris coupling floats it)
            api.debug("spawn_gpu_particle", {"x": sx + 1.2 + 0.9 * k, "y": 16 + DEPTH + 0.3, "z": sz - 1.0 + 0.6 * (k % 2), "material": "Wood", "scale": 1.0 / 3.0, "lifetime": a.seconds + 4.0})
        time.sleep(2.0)
        return vid

    if a.still:
        pond(0.44)
        subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={a.window}", "-frames:v", "1", "-vf", f"crop={a.crop}", str(EV / f"{a.tag}_still.png")], check=True)
        print("wrote", EV / f"{a.tag}_still.png"); return

    for name, band in TAKES:
        vid = pond(band)
        mp4 = EV / f"{a.tag}_{name}.mp4"
        rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                                "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
        t0 = time.time()
        time.sleep(1.5)
        api.debug("water_av_impulse", {"x": sx, "y": 16 + DEPTH - 0.1, "z": sz, "radius": 0.5, "strength": a.strength, "dy": -1.0})
        rows = []
        while rec.poll() is None:
            v = next((x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid), {})
            rows.append({"t": round(time.time() - t0, 2), "solver_ms": v.get("gpu_device_ms"), "asleep": v.get("asleep"), "tall": v.get("tall_active"),
                         "tall_columns": v.get("tall_columns"), "floor": v.get("tall_floor_y"), "reason": v.get("tall_reason"), "mass": v.get("mass")})
            time.sleep(0.25)
        out["takes"][name] = {"band_m": band, "rows": rows}
        lines = []
        for i, r in enumerate(rows):
            t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.25
            ms = 0.0 if r["asleep"] else (r["solver_ms"] or 0.0)
            label = "ALL FINE (today)" if band == 0 else f"TALL CELLS, band {band:.2f} m" + (f" (floor {r['floor'] - 16:.2f} m above the bed)" if r["tall"] and r["floor"] else " (not active)")
            lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{label}  |  solver {ms:.2f} ms\n")
        (EV / f"{a.tag}_{name}.srt").write_text("\n".join(lines), encoding="utf-8")
        awake = sorted(r["solver_ms"] or 0 for r in rows if not r["asleep"])
        print(name, "recorded; solver median", awake[len(awake) // 2] if awake else None, "tall", any(r["tall"] for r in rows), flush=True)
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    style = "FontName=Arial,FontSize=13,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
    ins = []
    fc = ""
    for k, (name, _) in enumerate(TAKES):
        ins += ["-i", f"{a.tag}_{name}.mp4"]
        fc += f"[{k}:v]crop={a.crop},scale=960:-2,subtitles={a.tag}_{name}.srt:force_style='{style}'[v{k}];"
    fc += "[v0][v1][v2][v3]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0[v]"
    subprocess.run([ff, "-y", "-loglevel", "error", *ins, "-filter_complex", fc, "-map", "[v]", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}_grid.mp4"],
                   check=True, cwd=str(EV))
    print("wrote", EV / f"{a.tag}_grid.mp4")


if __name__ == "__main__":
    main()
