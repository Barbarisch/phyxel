"""26.21 clip for the owner: the q 0.5 creek at a fixed low pose over the centreline, the volume correction OFF (air built back
for 150 s), switched ON 15 s into a 40 s recording. Labels burned in. Per second during the clip: the drawn surface (mean of the
81 sub-columns) at three voxel columns ahead of the camera - no mass read-back, so the recording carries no extra stalls."""
import json, shutil, subprocess, sys, time, pathlib
REPO = pathlib.Path(r"G:/Github/phyxel")
sys.path.insert(0, str(REPO / "tools"))
from water_bench import Api, camera_set
EV = REPO / "docs/evidence/water_core_h1"
api = Api("http://127.0.0.1:8114")
POSE = {"x": 51.5, "y": 16.45, "z": 32.0, "yaw": 0.0, "pitch": -14.0}
COLS = [[53.5, 31.5], [53.5, 32.5], [54.5, 31.5]]
api.debug("water_volume_correction", {"time_s": 0.0})
camera_set(api, POSE)
time.sleep(150.0)
camera_set(api, POSE)
time.sleep(1.0)
T, SWITCH = 40, 15
vf = ("crop=960:630:240:42,"
      f"drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='air fix OFF':x=380:y=560:fontsize=28:fontcolor=white:box=1:boxcolor=black@0.6:enable='lt(t,{SWITCH})',"
      f"drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='air fix ON (switched at {SWITCH} s)':x=300:y=560:fontsize=28:fontcolor=white:box=1:boxcolor=black@0.6:enable='gte(t,{SWITCH})'")
rec = subprocess.Popen([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(T),
                        "-i", "title=WaterBench_Creek", "-vf", vf, "-c:v", "libx264", "-crf", "24", "-pix_fmt", "yuv420p",
                        str(EV / "creekbench_pond_g9_q0.5_airfix.mp4")])
t0 = time.time(); rows = []; switched = False
while time.time() - t0 < T:
    t = time.time() - t0
    if not switched and t >= SWITCH:
        api.debug("water_volume_correction", {"time_s": 0.5}); switched = True
    cols = api.debug("water_av_column", {"points": COLS}).get("columns", [])
    rows.append({"t": round(t, 2), "on": switched, "surface_mean": [c.get("surface_mean") for c in cols]})
    time.sleep(1.0)
rec.wait()
(EV / "creekbench_pond_g9_q0.5_airfix.json").write_text(json.dumps({"pose": POSE, "columns": COLS, "switch_s": SWITCH, "rows": rows}, indent=1))
for r in rows: print(r["t"], r["on"], [round(s, 4) if s else None for s in r["surface_mean"]])
