"""WaterCore A12 (45.1) - the rest motion: does a SLEPT pond move, is the tap honest, what does it cost, and the owner's clip.

The Small bench (port 8111), Release. L5's 4 x 4 m pond (water x 100-104, z 12-16, level 16.5), auto-sleep ON: it sleeps into the
record and is drawn from it (27.11) - the water at rest the owner asked about. The camera ~4 m south of the pond, looking north and
down. One variable each step: `water_render_core {rest_amp_mm}` (0 = the control, exactly the build before A12).

Predictions (written before the first run, 2026-10-29):
  tap   debug 10 (the rest height, +-2 mm = black / white; tone map curve 0, exposure 1): amp 0 - the water's grey range <= 1 level and
        two frames 2 s apart equal; amp 1 mm - range > 10 levels and the frames differ (it moves)
  facet debug 11 (the facet slope): constant over each 1/9 m square - seen in the stills (`<tag>_tap_slope.png`), not measured here
  cost  amp 1 mm vs 0 on this pose: the whole frame within the run-to-run spread (< 0.1 ms GPU); ABAB order, 6 s each
  clip  `<tag>_side_by_side.mp4`: LEFT amp 0 (today), RIGHT amp 1 mm, normal shading, captioned with the fps

    python tools/water_rest_look.py [--amp-mm 1.0] [--seconds 12] [--tag rest_look]
Writes docs/evidence/water_core_m4/<tag>.json, <tag>_tap_*.png, <tag>_{off,on}.mp4 (+ .srt) and <tag>_side_by_side.mp4.
"""
import argparse, json, pathlib, shutil, statistics, subprocess, sys, tempfile, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, pose_ok
import water_float_rest as w
import water_l5_outcome as l5

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
POSE = {"x": 102.0, "y": 19.0, "z": 20.0, "yaw": -90.0, "pitch": -32.0}
CROP = "954:600:243:50"


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def grab(ff, window, path):
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={window}", "-frames:v", "1", "-vf", f"crop={CROP}", str(path)], check=True)


def frame_window(api, seconds):
    fps, cpu = [], []
    api.get("/api/debug/engine_timing")
    t0 = time.time()
    while time.time() - t0 < seconds:
        et = api.get("/api/debug/engine_timing"); fps.append(et.get("fps", 0)); cpu.append(et.get("cpuFrameTime", 0))
        time.sleep(0.05)
    gt = api.get("/api/debug/gpu_timing").get("gpu_frame_ms", {})
    return {"fps_mean": round(statistics.mean(fps), 1), "cpu_frame_ms_median": round(statistics.median(cpu), 3),
            "gpu_frame_ms_median": round(gt.get("median_ms", 0.0), 3), "gpu_frame_ms_max": round(gt.get("max_ms", 0.0), 3)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--window", default="WaterBench_Small")
    ap.add_argument("--amp-mm", type=float, default=1.0)
    ap.add_argument("--seconds", type=float, default=12.0)
    ap.add_argument("--tag", default="rest_look")
    ap.add_argument("--no-clip", action="store_true")
    a = ap.parse_args()
    import numpy as np
    from PIL import Image
    ff = shutil.which("ffmpeg")
    if not ff: raise SystemExit("ffmpeg not on PATH")
    api = Api(a.url); st = api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    rc0 = api.debug("water_render_core", {})
    rest0 = rc0.get("rest") or {}
    if "rest" not in rc0: raise SystemExit("this build has no rest motion (water_render_core echoes no 'rest')")
    tm0 = api.post("/api/debug/tonemap", {})
    tmp = pathlib.Path(tempfile.mkdtemp())
    out = {"build": st.get("build_config"), "amp_mm": a.amp_mm, "pose": None, "tap": {}, "cost": [], "clip": {}}
    try:
        api.debug("water_render_core", {"rest_amp_mm": 0.0, "debug": 0})
        w.AUTO_SLEEP = True; w.PRESSURE = True; w.pond(api)
        if not l5.wait_slept(api, 90): raise SystemExit("the pond did not sleep")
        got = camera_set(api, POSE)
        if not pose_ok(POSE, got): raise SystemExit(f"camera pose not taken: {got}")
        out["pose"] = got
        time.sleep(1.0)
        # --- the tap (debug 10, the passthrough tone map) ---
        api.post("/api/debug/tonemap", {"curve": 0, "exposure": 1.0})
        api.debug("water_render_core", {"debug": 10})

        def water_mask(img):
            g = img.astype(np.int16)
            chroma = np.abs(g[..., 0] - g[..., 1]) + np.abs(g[..., 1] - g[..., 2])
            return chroma <= 2   # the debug water is pure grey; terrain and sky are coloured

        mask = None
        for name, amp in (("off", 0.0), ("on", a.amp_mm)):
            api.debug("water_render_core", {"rest_amp_mm": amp})
            time.sleep(0.5)
            p1, p2 = tmp / f"{name}1.png", tmp / f"{name}2.png"
            grab(ff, a.window, p1); time.sleep(2.0); grab(ff, a.window, p2)
            i1 = np.asarray(Image.open(p1).convert("RGB")); i2 = np.asarray(Image.open(p2).convert("RGB"))
            if mask is None:   # the water = the off frame's flat grey (the most common bright grey: the stone ground is grey too, but dark)
                g = i1[..., 0].astype(int); cand = water_mask(i1) & (g > 120)
                flat = int(np.bincount(g[cand]).argmax())
                mask = cand & (np.abs(g - flat) <= 1)
                out["tap"]["flat_grey"] = flat
            m = mask
            g1, g2 = i1[..., 0][m].astype(int), i2[..., 0][m].astype(int)
            shutil.copy(p1, EV / f"{a.tag}_tap_height_{name}.png")
            out["tap"][name] = {"water_px": int(m.sum()), "grey_min": int(g1.min()) if g1.size else None, "grey_max": int(g1.max()) if g1.size else None,
                                "grey_p5_p95": [int(np.percentile(g1, 5)), int(np.percentile(g1, 95))] if g1.size else None,
                                "frames_differ_px": int((g1 != g2).sum()), "frames_differ_mean": round(float(np.abs(g1 - g2).mean()), 3) if g1.size else None}
            print(f"tap {name}: {out['tap'][name]}", flush=True)
        api.debug("water_render_core", {"debug": 11, "rest_amp_mm": a.amp_mm}); time.sleep(0.5)
        grab(ff, a.window, EV / f"{a.tag}_tap_slope.png")
        api.debug("water_render_core", {"debug": 0})
        api.post("/api/debug/tonemap", {"curve": tm0.get("curve", 1), "exposure": tm0.get("exposure", 8.0)})
        # --- the cost, ABAB ---
        for name in ("off", "on", "off", "on"):
            api.debug("water_render_core", {"rest_amp_mm": 0.0 if name == "off" else a.amp_mm})
            time.sleep(1.0)
            r = frame_window(api, 6.0); r["cond"] = name
            out["cost"].append(r); print("cost", r, flush=True)
        # --- the owner's clip ---
        if not a.no_clip:
            for name, amp in (("off", 0.0), ("on", a.amp_mm)):
                api.debug("water_render_core", {"rest_amp_mm": amp}); time.sleep(1.0)
                mp4 = EV / f"{a.tag}_{name}.mp4"
                rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                                        "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
                t0 = time.time(); rows = []
                while rec.poll() is None:
                    try: fps = api.get("/api/debug/engine_timing").get("fps")
                    except Exception: fps = None
                    rows.append({"t": round(time.time() - t0, 2), "fps": fps}); time.sleep(0.5)
                label = "TODAY: a slept pond (no rest motion)" if amp == 0 else f"REST MOTION {amp:g} mm (a breeze, faceted)"
                lines = []
                for i, r in enumerate(rows):
                    t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.5
                    lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{label}\n{(r['fps'] or 0):.0f} fps\n")
                (EV / f"{a.tag}_{name}.srt").write_text("\n".join(lines), encoding="utf-8")
                out["clip"][name] = {"fps_mean": round(statistics.mean([r["fps"] or 0 for r in rows]), 1)}
            style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
            fc = (f"[0:v]crop={CROP},scale=960:-2,subtitles={a.tag}_off.srt:force_style='{style}'[l];"
                  f"[1:v]crop={CROP},scale=960:-2,subtitles={a.tag}_on.srt:force_style='{style}'[r];[l][r]hstack=inputs=2[v]")
            subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_off.mp4", "-i", f"{a.tag}_on.mp4", "-filter_complex", fc, "-map", "[v]",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}_side_by_side.mp4"], check=True, cwd=str(EV))
            print("wrote", EV / f"{a.tag}_side_by_side.mp4", flush=True)
    finally:
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
        api.debug("water_render_core", {"debug": 0, "rest_amp_mm": rest0.get("amp_mm", 0.0)})
        api.post("/api/debug/tonemap", {"curve": tm0.get("curve", 1), "exposure": tm0.get("exposure", 8.0)})
        api.debug("clear_dynamics")


if __name__ == "__main__":
    main()
