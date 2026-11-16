"""WaterCore W6 step 0 (live, L4): what draws the sea today, measured before W6 is designed.

WaterBench_Coast (port 8109). The sea is in the record (generation: below sea level = ocean, 52.2) and the old flat sea SHEET
(`water.frag`, game.json water.enabled) is still drawn at sea level (kept by §9 until W6). Two engine runs, one per sheet state
(the sheet has no runtime switch - the rig flips game.json water.enabled and restores it). At three poses - the shipped spawn
(the beach, looking out to sea), the shore close (2 m over the waterline) and high (120 m up, looking out) - writes a still
(gdigrab) and the record draw / skin stats.
Prediction (written before the run): sheet OFF - the sea still drawn: the record tiles to draw_m (48 m), the skin beyond to far_m;
the horizon line where the skin ends (far_m) shows seabed / far terrain past it. Sheet ON - the sheet and the skin overlap at the
same level (one of them wins per pixel, possibly z-fighting).

    python tools/water_w6_step0.py --state on|off [--tag w6s0]
Writes docs/evidence/water_core_m4/<tag>_<state>_<pose>.png and <tag>_<state>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
POSES = {
    "spawn": {"x": 165.0, "y": 50.0, "z": 600.0, "yaw": 90.0, "pitch": -28.0},
    "shore": {"x": 165.0, "y": 18.0, "z": 668.0, "yaw": 90.0, "pitch": -8.0},
    "high": {"x": 165.0, "y": 136.0, "z": 600.0, "yaw": 90.0, "pitch": -12.0},
}


def grab(window, crop, path):
    subprocess.run([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-i", f"title={window}",
                    "-frames:v", "1", "-vf", f"crop={crop}", str(path)], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8109")
    ap.add_argument("--state", required=True, choices=["on", "off"])
    ap.add_argument("--tag", default="w6s0")
    ap.add_argument("--window", default="WaterBench_Coast")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--settle", type=float, default=20.0)
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    out = {"bench": "WaterBench_Coast", "sheet_state_asked": a.state, "water_stats": api.debug("water_stats", {}), "poses": {}}
    for name, pose in POSES.items():
        camera_set(api, pose); time.sleep(a.settle)
        rec = api.debug("water_av_record_draw", {})
        sk = api.debug("water_av_skin", {})
        p = EV / f"{a.tag}_{a.state}_{name}.png"; grab(a.window, a.crop, p)
        out["poses"][name] = {"pose": pose, "record_draw": rec, "skin": sk, "still": p.name}
        print(name, "record tiles", rec.get("wet_tiles"), "pending", rec.get("pending"), "| skin levels", sk.get("levels"),
              "fields", sk.get("fields"), "wet", sk.get("wet_cells"), "far_m", sk.get("far_m"), "pending", sk.get("pending"), flush=True)
    (EV / f"{a.tag}_{a.state}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{a.tag}_{a.state}.json")


if __name__ == "__main__":
    main()
