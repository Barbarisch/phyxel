"""The owner asked why part of the lake is not drawn at the start of w4_clip.mp4 (2026-11-14). The clip was recorded ~1 min after
launch; ~2 min after launch the same pose draws the whole lake (w4_clip_band_shipped.png). This measures the time course from a
FRESH launch at the clip's pose: every 10 s for 150 s, the record tiles (water_av_record_draw: tiles, wet, pending), the far skin
(water_av_skin) and a still. Prediction: the far lake appears only once the pending record tiles / the skin are built.
    python tools/water_w4_launch_fill.py   (start WaterBench_RiverCarve just before; port 8113)
Writes docs/evidence/water_core_m4/w4_launch_fill.json and w4_launch_fill_<t>s.png.
"""
import json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
POSE = {"x": -2224.0, "y": 93.0, "z": -1349.5, "yaw": 180.0, "pitch": -12.0}


def main():
    ff = shutil.which("ffmpeg")
    api = Api("http://127.0.0.1:8113"); api.wait_status(600)
    t0 = time.time()
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    camera_set(api, POSE)
    rows = []
    for k in range(16):
        t = round(time.time() - t0)
        rd = api.debug("water_av_record_draw", {})
        try: sk = api.debug("water_av_skin", {})
        except Exception as e: sk = {"error": str(e)}
        try: fps = api.get("/api/debug/engine_timing").get("fps")
        except Exception: fps = None
        png = f"w4_launch_fill_{k * 10:03d}s.png"
        subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-draw_mouse", "0", "-i", "title=WaterBench_RiverCarve", "-frames:v", "1",
                        "-vf", "crop=954:560:243:60", str(EV / png)], check=True)
        row = {"t_since_api_s": t, "tiles": rd.get("tiles"), "wet_tiles": rd.get("wet_tiles"), "pending": rd.get("pending"),
               "skin": {kk: v for kk, v in sk.items() if not isinstance(v, (list, dict))}, "fps": fps, "png": png}
        rows.append(row); print(json.dumps(row), flush=True)
        time.sleep(max(0.0, (k + 1) * 10.0 - (time.time() - t0)))
    (EV / "w4_launch_fill.json").write_text(json.dumps({"pose": POSE, "rows": rows}, indent=1))


if __name__ == "__main__":
    main()
