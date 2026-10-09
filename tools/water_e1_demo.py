"""WaterCore E1 (docs/WaterCore.md 19) live on the Small bench pond rig: a blast beside the pond and
the water's response, captured as a frame sequence, with the surface probed each frame. Then the far
control blast. Default backend (auto = GPU), realtime stepping - what a player sees.

Rig: pond 4x4 (x 100-103, z 12-15), floor at y 15, filled to 16.5 (1.5 m deep). Blast at the S8
blast_near (106, 17, 13.5), radius 4, energy 62 (Stone toughness 110: nothing breaks - the pond wall
stays whole; the push law gives 3.0 m/s at the centre, reach 6 m). Control: blast_far (124, 17, 13.5).
Usage: python tools/water_e1_demo.py [--url http://127.0.0.1:8111] [--backend auto|cpu|gpu]"""
import argparse, json, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set, camera_get
from water_feel import capture

EV = Path(__file__).resolve().parents[1] / "docs" / "evidence" / "water_core_e"
EV.mkdir(parents=True, exist_ok=True)
ap = argparse.ArgumentParser()
ap.add_argument("--url", default="http://127.0.0.1:8111")
ap.add_argument("--backend", default="auto")
ap.add_argument("--tag", default="e1")
args = ap.parse_args()
api = Api(args.url); api.wait_status(900)
POSE = {"x": 109.5, "y": 20.5, "z": 13.5, "yaw": 180, "pitch": -38}   # 7 m east of the pond, looking west across the blast point
camera_set(api, POSE); time.sleep(4)
for v in api.debug("water_av_list").get("volumes", []):
    api.debug("water_av_destroy", {"id": v["id"]})
cr = api.debug("water_av_create", {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16, "cellSize": 1.0 / 3.0,
                                   "transport": "eulerian", "backend": args.backend, "auto_sleep": False})
if "error" in cr:
    raise SystemExit(f"create: {cr}")
vid = cr["volume"]["id"]
api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
api.debug("water_av_realtime", {"on": True})
api.debug("water_coupling", {"enabled": True})
time.sleep(6)

def probe():
    near = [api.debug("water_av_probe", {"x": 103.5, "y": 16.0, "z": z + 0.5}).get("surface_y") for z in range(12, 16)]
    far = [api.debug("water_av_probe", {"x": 100.5, "y": 16.0, "z": z + 0.5}).get("surface_y") for z in range(12, 16)]
    m = lambda xs: round(sum(x for x in xs if x is not None) / max(1, sum(1 for x in xs if x is not None)), 4)
    return {"near": m(near), "far": m(far)}

log = {"pose": POSE, "backend": args.backend, "volume": cr["volume"], "before": probe()}
log["before_png"] = str(capture(api, EV / f"{args.tag}_before.png"))
t0 = time.time()
dmg = api.post("/api/damage/apply", {"x": 106, "y": 17, "z": 13.5, "radius": 4.0, "energy": 62.0})
log["blast"] = {k: dmg.get(k) for k in ("success", "broken", "debris")}
log["coupling"] = api.debug("water_coupling", {})
frames = []
for t in (0.15, 0.4, 0.8, 1.5, 3.0, 6.0, 10.0):
    while time.time() - t0 < t:
        time.sleep(0.02)
    png = capture(api, EV / f"{args.tag}_t{t:05.2f}.png")
    frames.append({"t": t, "png": str(png), **probe(), "volume": {k: v for k, v in api.debug("water_av_list")["volumes"][0].items() if k in ("asleep", "mass", "kinetic_energy")}})
    print(json.dumps(frames[-1]), flush=True)
log["frames"] = frames
c0 = probe()
far = api.post("/api/damage/apply", {"x": 124, "y": 17, "z": 13.5, "radius": 4.0, "energy": 62.0})
time.sleep(1.0)
log["control"] = {"blast": {k: far.get(k) for k in ("success", "broken")}, "coupling": api.debug("water_coupling", {}), "before": c0, "after": probe()}
log["after_control_png"] = str(capture(api, EV / f"{args.tag}_control.png"))
(EV / f"{args.tag}_demo.json").write_text(json.dumps(log, indent=1), encoding="utf-8")
print(json.dumps({"blast": log["blast"], "coupling": log["coupling"]["last_blast"], "control": log["control"]}, indent=1))
