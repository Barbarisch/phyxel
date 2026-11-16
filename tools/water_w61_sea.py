"""WaterCore §55.1 (W6.1) live (L4): the sea drawn as far as the land.

WaterBench_Coast (port 8109), the shipped game.json (far terrain 2048), the restored world. Step 0 (55.0, `w6s0_off_high.png`) at
the high pose (136 m up, pitch -12, looking out to sea): the skin's water ended at ~1 km (far_m 1024), the seabed showed past it.
  --phase fill : on a session start - the skin's state every second at the high pose until nothing is pending (or --max-s): the
                 sampled and generator summaries held, the time the sampled reach filled and the time the generator's did. Then a
                 still (`<tag>_high.png`) and, with the skin off, the same pose (`<tag>_high_noskin.png`) - what the skin draws.
  --phase cost : whole-frame CPU / GPU / fps at the high pose, far_m 2048 (the far terrain's - shipped) vs 1024 (step 0's), pinned
                 through water_av_skin, ABBA (each state --hold s after a --settle s settle), then far_m_follow restored.
Prediction (written before the run): the band of the high still between the old skin end and the far terrain's end (rows 200-228 of
the 954 x 560 crop, step 0) - pale seabed before (>= 95 % sand-coloured), water after (<= 5 % sand-coloured); the sampled reach fills
in seconds (0.8 ms a chunk column, unit test), the generator's within gen_m 1024 as before (minutes cold, seconds from the cache);
the cost of far_m 2048 vs 1024 inside the run-to-run spread (one more ring level: <= 4,096 coarse cells).

    python tools/water_w61_sea.py --phase fill [--tag w61_fill_cold] [--max-s 400]
    python tools/water_w61_sea.py --phase cost [--tag w61_cost]
Writes docs/evidence/water_core_m4/<tag>.json (+ stills).
"""
import argparse, json, pathlib, shutil, statistics, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
HIGH = {"x": 165.0, "y": 136.0, "z": 600.0, "yaw": 90.0, "pitch": -12.0}
BAND = (201, 228)   # rows of the crop: step 0's seabed band (the old skin end ~row 230, the far terrain's end ~row 200)


def grab(window, crop, path):
    subprocess.run([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-i", f"title={window}",
                    "-frames:v", "1", "-vf", f"crop={crop}", str(path)], check=True)


def sand_fraction(path):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(path).convert("RGB")).astype(int)[BAND[0]:BAND[1] + 1, 20:-20]
    sand = (a[:, :, 0] > 190) & (a[:, :, 1] > 190) & (a[:, :, 2] > 180)
    return float(sand.mean())


def skin(api, **kw):
    return api.debug("water_av_skin", kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8109")
    ap.add_argument("--phase", required=True, choices=["fill", "cost", "clip"])
    ap.add_argument("--tag", default=None)
    ap.add_argument("--window", default="WaterBench_Coast")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--max-s", type=float, default=400.0)
    ap.add_argument("--settle", type=float, default=8.0)
    ap.add_argument("--hold", type=float, default=12.0)
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    out = {"bench": "WaterBench_Coast", "phase": a.phase, "pose": HIGH}
    camera_set(api, HIGH)
    if a.phase == "fill":
        tag = a.tag or "w61_fill"
        t0 = time.time(); rows = []; tSampled = tGen = None
        while time.time() - t0 < a.max_s:
            s = skin(api)
            t = round(time.time() - t0, 1)
            rows.append({"t": t, **{k: s.get(k) for k in ("far_m", "far_m_source", "gen_m", "levels", "sampled_built", "sampled_held",
                                                           "gen_built", "gen_held", "gen_pending", "pending", "upgrades", "wet_cells", "ms_max")},
                         "gen_cache": s.get("gen_cache", {}).get("state")})
            # the sampled reach: nothing sampled left to ask (the sampled jobs are served first, so gen_pending then counts only the
            # generator's); filled when the sampled count stops rising and nothing is pending on the main thread
            if tSampled is None and len(rows) > 3 and rows[-1]["sampled_built"] == rows[-3]["sampled_built"] and rows[-1]["sampled_built"]:
                tSampled = rows[-3]["t"]
            if s.get("gen_pending") == 0 and s.get("pending") == 0 and t > 5:
                tGen = t; break
            if int(t) % 10 == 0: print(rows[-1], flush=True)
            time.sleep(1.0)
        out.update({"rows": rows, "sampled_filled_s": tSampled, "all_filled_s": tGen, "final": skin(api)})
        p = EV / f"{tag}_high.png"; grab(a.window, a.crop, p)
        skin(api, on=False); time.sleep(3.0)
        p2 = EV / f"{tag}_high_noskin.png"; grab(a.window, a.crop, p2)
        skin(api, on=True); time.sleep(3.0)
        out["band_sand_fraction"] = {"skin_on": sand_fraction(p), "skin_off": sand_fraction(p2),
                                     "step0_before": sand_fraction(EV / "w6s0_off_high.png")}
        out["stills"] = [p.name, p2.name]
        print("sampled filled", tSampled, "s; all", tGen, "s; band sand", out["band_sand_fraction"], flush=True)
    elif a.phase == "clip":
        # the owner's clip: from the beach (2 m over the sand) the camera climbs to 150 m looking out to sea (30 s), holds (6 s), then
        # turns 60 degrees left and right (12 s) - captioned with the height, fps and the skin's reach every second
        from water_w5_clip import srt_time
        tag = a.tag or "w61_clip"
        start = {"x": 165.0, "y": 18.0, "z": 668.0, "yaw": 90.0, "pitch": -4.0}
        top = {"x": 165.0, "y": 150.0, "z": 600.0, "yaw": 90.0, "pitch": -14.0}
        camera_set(api, start); time.sleep(6.0)
        keys = [(0.0, start), (3.0, start), (33.0, top), (39.0, top), (45.0, dict(top, yaw=30.0)), (57.0, dict(top, yaw=150.0)), (60.0, dict(top, yaw=150.0))]
        def pose_at(t):
            for (t0, p0), (t1, p1) in zip(keys, keys[1:]):
                if t <= t1:
                    f = 0.0 if t1 == t0 else (t - t0) / (t1 - t0); f = f * f * (3 - 2 * f)
                    return {k: p0[k] + (p1[k] - p0[k]) * f for k in p0}
            return keys[-1][1]
        dur = keys[-1][0] + 1.0
        raw = EV / f"{tag}_raw.mp4"
        rec = subprocess.Popen([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(dur), "-i",
                                f"title={a.window}", "-c:v", "libx264", "-pix_fmt", "yuv420p", str(raw)])
        t0 = time.time(); rows = []; nextRow = 0.0
        while (t := time.time() - t0) < dur - 0.5:
            camera_set(api, pose_at(t))
            if t >= nextRow:
                e = api.get("/api/debug/engine_timing"); s = skin(api)
                rows.append({"t": round(t, 1), "y": round(pose_at(t)["y"], 1), "fps": e.get("fps"), "cpu_ms": e.get("cpuFrameTime"),
                             "far_m": s.get("far_m"), "sampled_held": s.get("sampled_held"), "gen_pending": s.get("gen_pending")})
                nextRow += 1.0
            time.sleep(0.03)
        rec.wait()
        lines = []
        for i, r in enumerate(rows):
            t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 1.0
            lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\nthe sea drawn as far as the land (far terrain 2 km) - camera {r['y']:.0f} m up\n"
                         f"t {r['t']:.0f} s | {(r.get('fps') or 0):.0f} fps | the water's reach {r['far_m']:.0f} m\n")
        (EV / f"{tag}.srt").write_text("\n".join(lines), encoding="utf-8")
        style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
        subprocess.run([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-i", raw.name, "-vf", f"crop={a.crop},scale=1280:-2,subtitles={tag}.srt:force_style='{style}'",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{tag}.mp4"], check=True, cwd=str(EV))
        raw.unlink(missing_ok=True)
        out["rows"] = rows
        print("wrote", EV / f"{tag}.mp4", flush=True)
    else:
        tag = a.tag or "w61_cost"
        res = {}
        for rep, far in enumerate((2048.0, 1024.0, 1024.0, 2048.0)):
            st = skin(api, far_m=far)
            time.sleep(a.settle); api.get("/api/debug/engine_timing")
            samples = []
            t0 = time.time()
            while time.time() - t0 < a.hold:
                time.sleep(1.0)
                e = api.get("/api/debug/engine_timing")
                samples.append({"fps": e.get("fps"), "cpu_ms": e.get("cpuFrameTime"), "gpu_ms": e.get("gpu_frame_ms")})
            sk = skin(api)
            key = f"far{int(far)}_{rep}"
            res[key] = {"far_m": st.get("far_m"), "levels": sk.get("levels"), "fields": sk.get("fields"), "wet_cells": sk.get("wet_cells"),
                        "skin_ms_max": sk.get("ms_max"), "pending": sk.get("pending"), "gen_pending": sk.get("gen_pending"),
                        **{k: statistics.median([x[k] for x in samples if x[k] is not None]) for k in ("fps", "cpu_ms", "gpu_ms")}}
            print(key, res[key], flush=True)
        out["runs"] = res
        out["restored"] = skin(api, far_m_follow=True)
    (EV / f"{tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{tag}.json")


if __name__ == "__main__":
    main()
