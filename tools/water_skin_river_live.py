"""WaterCore.md 51.4 S3 step 2 live (L4): the far skin on a GENERATED world - WaterBench_RiverCarve (port 8113: Mountains, streaming,
the old CA water off, so the generated rivers and lakes reach the new core only as the chunk record). From the bench's camera above
the valley looking north along the order-5 trunk river: skin off, then on - a still each (gdigrab), the whole frame at rest, the
skin's stats; the "on" still is taken once the generator's workers are idle (columns beyond residency drawn from the generator).

    python tools/water_skin_river_live.py [--tag skin_river] [--wait-s 240]
Writes docs/evidence/water_core_m4/<tag>_off.png, <tag>_on.png and <tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_a3_cost import window

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
POSE = {"x": -1728.0, "y": 115.0, "z": -1320.0, "yaw": 90.0, "pitch": -28.0}   # game.json's camera: the valley, looking north


def grab(ff, title, crop, png):
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={title}", "-frames:v", "1", "-vf", f"crop={crop}", str(png)], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--window", default="WaterBench_RiverCarve")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--tag", default="skin_river")
    ap.add_argument("--wait-s", type=float, default=240.0)
    ap.add_argument("--settle-s", type=float, default=60.0)
    ap.add_argument("--pose", default=None, help="x,y,z,yaw,pitch (default: the bench's camera)")
    a = ap.parse_args()
    pose = dict(POSE)
    if a.pose:
        v = [float(t) for t in a.pose.split(",")]; pose = {"x": v[0], "y": v[1], "z": v[2], "yaw": v[3], "pitch": v[4]}
    ff = shutil.which("ffmpeg")
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    skin0 = api.debug("water_av_skin", {})
    out = {"pose": pose, "skin_before": skin0}
    try:
        camera_set(api, pose); time.sleep(a.settle_s)   # streaming settles round the camera (20 s was not enough: 162 ms water feed frames)
        api.debug("water_av_skin", {"on": False}); time.sleep(3.0)
        grab(ff, a.window, a.crop, EV / f"{a.tag}_off.png")
        out["off"] = {"rest": window(api, 6.0, "skin off, at rest")}
        api.debug("water_av_skin", {"on": True})
        t0 = time.time(); trace = []
        while time.time() - t0 < a.wait_s:
            s = api.debug("water_av_skin", {})
            trace.append({"t": round(time.time() - t0, 1), **{k: s.get(k) for k in ("generator_source", "gen_pending", "gen_held", "gen_built", "pending", "summaries", "wet_cells", "fields", "ms_last_call")}})
            if time.time() - t0 > 5 and s.get("gen_pending", 1) == 0 and s.get("pending", 1) == 0: break
            time.sleep(2.0)
        out["fill"] = trace
        print("filled:", trace[-1], flush=True)
        time.sleep(2.0)
        grab(ff, a.window, a.crop, EV / f"{a.tag}_on.png")
        out["on"] = {"skin": api.debug("water_av_skin", {}), "rest": window(api, 6.0, "skin on, at rest")}
        print("on stats:", json.dumps(out["on"]["skin"]), flush=True)
        api.debug("water_av_skin", {"on": False}); time.sleep(3.0)
        out["off_again"] = {"rest": window(api, 6.0, "skin off again, at rest")}   # A/B/A: the off figure after the same settling
        for m in ("off", "on", "off_again"):
            r = out[m]["rest"]
            print(f"{m}: {r['fps_mean']} fps, cpu {r['cpu_frame_ms_mean']} ms, gpu {r['gpu_frame_ms_median']} ms, worst {r['frame_ms_max']} ms, water app {r['app_water_cpu_ms']}", flush=True)
    finally:
        api.debug("water_av_skin", {"on": bool(skin0.get("on", False))})
        (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
        print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
