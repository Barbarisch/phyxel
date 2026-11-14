"""WaterCore 52 W4.1 live (L4): do the readers of water see a GENERATED lake on the new water? WaterBench_RiverCarve (port 8113:
Mountains, streaming, the old CA water OFF - shipped default), the 51.13 lake (body 1178, level ~90) west of the drain513 pose.

Finds a wet column (the new water: water_av_column), then reads, at a lake point and at a dry-land control:
  - the camera's underwater test: the camera put 1 m under the surface -> water_stats camera_submergence (the fog's input)
  - a CPU rigid body's water: water_query (W4.1's route; absent before W4.1 -> reported "no route") - a 1 m box half under the surface
Prediction (written 2026-11-14, before W4.1): today camera_submergence 0 and the box 0 (both read the old CA water, off);
after W4.1: 1 and 0.5 (+-0.05); dry land 0 both times.

    python tools/water_w4_readers_live.py [--tag w4_readers_before]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
POSE = {"x": -2169.1, "y": 98.0, "z": -1350.0, "yaw": -176.1, "pitch": -14.0}   # drain513_lake: the lake to the west


def find_lake(api, min_depth=3.0):
    """the nearest column west of the pose with >= min_depth of water over the ground (the new water's view: core or record; the
    ground from terrain_height's spawn_y, the first free y) - a shore column put the camera inside the bank (the first run)"""
    for dx in range(4, 200, 2):
        x, z = POSE["x"] - dx, POSE["z"]
        c = api.debug("water_av_column", {"points": [[x, z]]})["columns"][0]
        if c.get("surface_y") is None: continue
        g = api.get(f"/api/world/terrain_height?x={int(x // 1)}&z={int(z // 1)}").get("spawn_y")
        if g is not None and c["surface_y"] - g >= min_depth:
            c["ground_y"] = g
            return c
    return None


def probe(api, x, z, surface, still=None):
    out = {}
    cam = {"x": x, "y": surface - 1.0, "z": z, "yaw": POSE["yaw"], "pitch": 0.0}
    camera_set(api, cam); time.sleep(1.5)
    ws = api.debug("water_stats", {})
    out["camera"] = {"pose": cam, "camera_submergence": ws.get("camera_submergence"), "camera_depth_below": ws.get("camera_depth_below"),
                     "plane_enabled": ws.get("plane_enabled")}
    box = [x - 0.5, surface - 0.5, z - 0.5, x + 0.5, surface + 0.5, z + 0.5]
    try:
        out["box"] = api.debug("water_query", {"boxes": [box], "points": [[x, surface - 1.0, z]]})
    except Exception as e:
        out["box"] = {"error": f"no route ({type(e).__name__}: {e})"}
    if still:   # the frame the player sees there (gdigrab of the bench window; the fog is drawn or not)
        subprocess.run([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-draw_mouse", "0", "-i", "title=WaterBench_RiverCarve",
                        "-frames:v", "1", "-vf", "crop=954:560:243:60", str(EV / still)], check=True)
        out["still"] = still
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--tag", default="w4_readers_before")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    camera_set(api, POSE); time.sleep(30.0)   # streaming settles round the lake
    c = find_lake(api)
    if not c: raise SystemExit("no wet column found west of the pose")
    surf = c.get("surface_mean") or c["surface_y"]
    print("lake column:", c, flush=True)
    out = {"pose": POSE, "lake_column": c}
    out["lake"] = probe(api, c["x"] + 0.5, c["z"] + 0.5, surf, still=f"{a.tag}_underwater.png")
    print("lake:", json.dumps(out["lake"]), flush=True)
    # control: in the air at the pose (the bench camera's own spot, 8 m above the lake's level, over the bank): the camera 1 m below
    # the pose's height, the box straddling it - nothing there is water
    out["dry"] = probe(api, POSE["x"], POSE["z"], POSE["y"])
    print("dry:", json.dumps(out["dry"]), flush=True)
    camera_set(api, POSE)
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
