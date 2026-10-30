"""WaterCore A12 v2 (45.3) PROTOTYPE - the wind's gusts drive the pond's own rings: do the waves look like a breeze on a pond, meet
the banks, and can the owner SEE them?

The Small bench (port 8111), Release. L5's 4 x 4 m pond in its stone pit (water x 100-104, z 12-16, level 16.5), kept AWAKE
(auto-sleep off: the prototype's rings would hold a pond awake). The camera CLOSE and low: 1.5 m south of the pond's south wall, 1.1 m
over the water, looking north - the owner could not see the 45.2 clips from 4 m. One variable: `water_ripples {wind: {pa}}`.

  --calibrate   pa 0 / 1 / 3 / 10 / 30: the rings' largest height and their facet-slope histogram after 8 s each (no clip)
  default       the clip: LEFT no wind, RIGHT wind at --pa, each with the same 1/9 m stone dropped at 6 s (the ring the owner
                knows, as the scale reference), captioned with the rings' largest height and the fps
Prediction (written 2026-10-29): with wind the waves run mostly downwind (deg 30 = toward +x, a little +z), steepest at the lee
(east / north) walls where they pile and reflect; the windward (west) wall stays calmer (fetch).

    python tools/water_wind_ripple_look.py --calibrate
    python tools/water_wind_ripple_look.py --pa 3 [--tag wind_look]
Writes docs/evidence/water_core_m4/<tag>.json (+ the clip, <tag>_side_by_side.mp4).
"""
import argparse, json, pathlib, shutil, statistics, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, pose_ok
import water_float_rest as w
import water_l5_outcome as l5

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
POSE = {"x": 102.0, "y": 17.6, "z": 17.5, "yaw": -90.0, "pitch": -24.0}
CROP = "954:600:243:50"


def srt_time(t):
    ms = int(round(t * 1000)); h, ms = divmod(ms, 3600000); m, ms = divmod(ms, 60000); s, ms = divmod(ms, 1000)
    return f"{h:02}:{m:02}:{s:02},{ms:03}"


def setup(api):
    api.debug("water_ripples", {"wind": False, "shape": False})
    api.debug("water_locality", {"test_viewer": [102.0, 17.6, 17.5]})
    w.AUTO_SLEEP = False; w.PRESSURE = True; w.pond(api)
    got = camera_set(api, POSE)
    if not pose_ok(POSE, got): raise SystemExit(f"camera pose not taken: {got}")
    return got


def shape(api):
    r = api.debug("water_ripples", {})
    return {"max_abs_mm": round(1000 * (r.get("max_abs_m") or 0.0), 3), "max_slope": r.get("max_slope"), "slope_hist": r.get("slope_hist"),
            "slope_edges": r.get("slope_edges"), "awake": r.get("awake"), "ms": r.get("ms")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--window", default="WaterBench_Small")
    ap.add_argument("--calibrate", action="store_true")
    ap.add_argument("--pa", type=float, default=3.0)
    ap.add_argument("--deg", type=float, default=30.0)
    ap.add_argument("--mps", type=float, default=0.5)
    ap.add_argument("--length-m", type=float, default=0.4)
    ap.add_argument("--seconds", type=float, default=14.0)
    ap.add_argument("--tag", default="wind_look")
    a = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(a.url); st = api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    if "wind" not in api.debug("water_ripples", {"shape": False}): raise SystemExit("this build has no wind prototype")
    out = {"build": st.get("build_config"), "args": vars(a), "rows": [], "clip": {}}
    wind = lambda pa: {"pa": pa, "deg": a.deg, "mps": a.mps, "length_m": a.length_m}
    try:
        out["pose"] = setup(api)
        time.sleep(4.0)
        if a.calibrate:
            for pa in (0.0, 1.0, 3.0, 10.0, 30.0):
                api.debug("water_ripples", {"wind": wind(pa) if pa > 0 else False, "shape": False})
                time.sleep(8.0)
                r = shape(api); r["pa"] = pa
                out["rows"].append(r); print(r, flush=True)
            return
        for name, pa in (("calm", 0.0), ("wind", a.pa)):
            api.debug("clear_dynamics")
            api.debug("water_ripples", {"wind": wind(pa) if pa > 0 else False, "shape": False})
            time.sleep(6.0)   # the waves build
            mp4 = EV / f"{a.tag}_{name}.mp4"
            rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(a.seconds), "-i", f"title={a.window}",
                                    "-c:v", "libx264", "-pix_fmt", "yuv420p", str(mp4)])
            t0 = time.time(); rows = []; dropped = False
            while rec.poll() is None:
                if not dropped and time.time() - t0 > 6.0:   # the reference: a 1/9 m stone, the ring the owner knows
                    api.debug("spawn_gpu_particle", {"x": 101.5, "y": 17.0, "z": 14.5, "material": "Stone", "scale": 1 / 9, "lifetime": 30, "spin": 0})
                    dropped = True
                try: fps = api.get("/api/debug/engine_timing").get("fps")
                except Exception: fps = None
                s = shape(api)
                rows.append({"t": round(time.time() - t0, 2), "fps": fps, "max_abs_mm": s["max_abs_mm"], "max_slope": s["max_slope"]}); time.sleep(0.4)
            label = "CALM: the pond's rings, no wind" if pa == 0 else f"WIND: gusts drive the rings ({pa:g} Pa)"
            lines = []
            for i, r in enumerate(rows):
                t1 = rows[i + 1]["t"] if i + 1 < len(rows) else r["t"] + 0.4
                extra = " | stone drop at 6 s" if 5.5 < r["t"] < 8.0 else ""
                lines.append(f"{i + 1}\n{srt_time(r['t'])} --> {srt_time(t1)}\n{label}\nrings up to {r['max_abs_mm']:.1f} mm | {(r['fps'] or 0):.0f} fps{extra}\n")
            (EV / f"{a.tag}_{name}.srt").write_text("\n".join(lines), encoding="utf-8")
            out["clip"][name] = {"pa": pa, "rows": rows}
        style = "FontName=Arial,FontSize=12,PrimaryColour=&H00FFFFFF,OutlineColour=&H00000000,BorderStyle=3,Outline=1,Alignment=2,MarginV=6"
        fc = (f"[0:v]crop={CROP},scale=960:-2,subtitles={a.tag}_calm.srt:force_style='{style}'[l];"
              f"[1:v]crop={CROP},scale=960:-2,subtitles={a.tag}_wind.srt:force_style='{style}'[r];[l][r]hstack=inputs=2[v]")
        subprocess.run([ff, "-y", "-loglevel", "error", "-i", f"{a.tag}_calm.mp4", "-i", f"{a.tag}_wind.mp4", "-filter_complex", fc, "-map", "[v]",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", f"{a.tag}_side_by_side.mp4"], check=True, cwd=str(EV))
        print("wrote", EV / f"{a.tag}_side_by_side.mp4", flush=True)
    finally:
        (EV / f"{a.tag}{'_calibrate' if a.calibrate else ''}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
        api.debug("water_ripples", {"wind": False, "shape": False})
        api.debug("water_locality", {"clear_test_viewer": True})
        api.debug("clear_dynamics")


if __name__ == "__main__":
    main()
