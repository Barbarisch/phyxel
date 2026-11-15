"""Why part of the lake is not drawn in w4_clip.mp4's opening shot (owner's question, 2026-11-14). Measures, at the clip's pose:
  - along the view line (-x from the camera), per 4 m: the terrain top and the water there (water_av_column: the record);
  - the record's draw radius (water_av_record_draw) and the far skin's state (water_render_core);
  - two stills at the same pose: as shipped, and with the record drawn 200 m round the view (the control).
Prediction: the pale band beyond the near grass strip is lake water further than the record's 48 m draw radius - drawn at 200 m.
    python tools/water_w4_clip_band.py     (WaterBench_RiverCarve up, port 8113)
Writes docs/evidence/water_core_m4/w4_clip_band.json, w4_clip_band_shipped.png, w4_clip_band_200m.png.
"""
import json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
POSE = {"x": -2224.0, "y": 93.0, "z": -1349.5, "yaw": 180.0, "pitch": -12.0}
CROP = "954:560:243:60"


def still(ff, name):
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-draw_mouse", "0", "-i", "title=WaterBench_RiverCarve", "-frames:v", "1",
                    "-vf", f"crop={CROP}", str(EV / name)], check=True)


def main():
    ff = shutil.which("ffmpeg")
    api = Api("http://127.0.0.1:8113"); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    camera_set(api, POSE); time.sleep(12.0)
    out = {"pose": POSE, "record_draw": api.debug("water_av_record_draw", {})}
    try: out["render_core"] = api.debug("water_render_core", {})
    except Exception as e: out["render_core"] = str(e)
    pts = [[POSE["x"] - d, POSE["z"]] for d in range(0, 200, 4)]
    cols = api.debug("water_av_column", {"points": pts}).get("columns", [])
    line = []
    for (px, pz), c in zip(pts, cols):
        th = api.get(f"/api/world/terrain_height?x={int(px)}&z={int(pz)}")
        line.append({"dist_m": round(POSE["x"] - px, 1), "x": px, "terrain_top": th.get("surface_y", th.get("height")), "water_surface": c.get("surface_y")})
    out["view_line"] = line
    still(ff, "w4_clip_band_shipped.png")
    draw0 = out["record_draw"].get("radius_m", 48.0)
    api.debug("water_av_record_draw", {"radius": 200.0}); time.sleep(8.0)
    still(ff, "w4_clip_band_200m.png")
    api.debug("water_av_record_draw", {"radius": draw0})
    (EV / "w4_clip_band.json").write_text(json.dumps(out, indent=1))
    print(json.dumps({"record_draw": out["record_draw"]}, indent=0))
    for r in line: print(r)


if __name__ == "__main__":
    main()
