"""WaterCore §58.5 - the river reach's look A/B (a LOOK switch, the owner's call): water_render_core top_normal_smooth 0 (today's,
the quad's own normal) / 3 / 9 cells, stills at three poses of the W5.2b flight (RiverCarve, port 8113), the same frame each.
    python tools/water_reach_look_ab.py [--out docs/evidence/water_core_m4]
Writes reach_look_p<i>_s<k>.png and reach_look_sheet.png (rows = poses, columns = 0 / 3 / 9).
"""
import argparse, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_w52b_follow as F
import water_w5_river_current as w51

ap = argparse.ArgumentParser()
ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"))
a = ap.parse_args()
out = pathlib.Path(a.out)
ff = shutil.which("ffmpeg")
api = Api("http://127.0.0.1:8113"); api.wait_status(600)
try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
except Exception as e: print("editor_panels:", e)
camera_set(api, w51.SITES["river"]["cam"]); time.sleep(12.0)
path = F.trace(api)
wps = F.waypoints(path)
files = []
for i, wi in enumerate((1, 3, 5)):
    camera_set(api, wps[wi]); time.sleep(12.0)
    row = []
    for k in (0, 3, 9):
        print("pose", i, "smooth", api.debug("water_render_core", {"top_normal_smooth": k}).get("top_normal_smooth"), flush=True)
        time.sleep(1.0)
        f = out / f"reach_look_p{i}_s{k}.png"
        subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-draw_mouse", "0", "-i", "title=WaterBench_RiverCarve",
                        "-frames:v", "1", "-vf", "crop=954:560:243:60", str(f)], check=True)
        row.append(f)
    files.append(row)
api.debug("water_render_core", {"top_normal_smooth": 0})
inputs = []
for row in files: inputs += row
args = [ff, "-y", "-loglevel", "error"]
for f in inputs: args += ["-i", str(f)]
lay = "|".join(f"{c}*477_{r}*280" .replace("*", "") for r in range(3) for c in range(3))
lay = "|".join(f"{c * 477}_{r * 280}" for r in range(3) for c in range(3))
filt = "".join(f"[{j}]scale=477:280[s{j}];" for j in range(9)) + "".join(f"[s{j}]" for j in range(9)) + f"xstack=inputs=9:layout={lay}"
subprocess.run(args + ["-filter_complex", filt, str(out / "reach_look_sheet.png")], check=True)
print("wrote", out / "reach_look_sheet.png")
