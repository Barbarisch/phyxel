"""Live check of WaterCore 20 (moving solids) on the Small bench: one 1/3 m stone dropped 3 m into the
centre of a fresh, rested pond; the DRAWN surface recorded by the engine every frame (water_av_watch - no
HTTP sampling jitter) at the drop point and 0.33 / 0.67 m from it (the T4 probes), for 1.5 s from the
spawn. `level` = the pond itself (the run that starts lowest), `top` = the highest run (a splash thrown
above it), reported separately. Repeated --runs times with displacement ON and OFF (the control); each
stone lives 4 s, so the next drop's pond holds none. Writes docs/evidence/water_core_e/<tag>.json.

Usage: python tools/water_solids_probe.py [--url http://127.0.0.1:8111] [--runs 3] [--tag solids_probe]
Predictions (WaterCore.md 20.8, derived): entry dip >= 8 cm, >= 3 cm at 0.33 m, >= 1.5 cm at 0.67 m within
0.5 s of entry; OFF -> the baseline (0.3 / - / 0.5 cm)."""
import argparse, json, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api

ap = argparse.ArgumentParser()
ap.add_argument("--url", default="http://127.0.0.1:8111")
ap.add_argument("--runs", type=int, default=3)
ap.add_argument("--tag", default="solids_probe")
args = ap.parse_args()
api = Api(args.url); api.wait_status(900)
REST = 16.5
X, Z = 101.5, 13.5          # a cell centre (1/3 m cells: 101.333..101.667)
NAMES = ["0.00", "0.33", "0.67"]
POINTS = [[X, Z], [X + 1.0 / 3.0, Z], [X + 2.0 / 3.0, Z]]

def pond():
    for v in api.debug("water_av_list").get("volumes", []):
        api.debug("water_av_destroy", {"id": v["id"]})
    api.debug("water_av_create", {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16, "cellSize": 1.0 / 3.0,
                                  "transport": "eulerian", "backend": "auto", "auto_sleep": False})
    api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
    api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
    api.debug("water_av_realtime", {"on": True})

def run(solids):
    api.debug("water_coupling", {"enabled": True, "solids": solids})
    pond(); time.sleep(6)
    api.debug("water_av_watch", {"points": POINTS})
    api.debug("spawn_gpu_particle", {"x": X, "y": 19.5, "z": Z, "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 4.0})
    time.sleep(1.5)
    feed = api.debug("water_coupling", {}).get("moving_solids")
    w = api.debug("water_av_watch", {"stop": True})
    rows = w.get("rows", [])
    out = {"solids": solids, "feed": feed, "frames": len(rows)}
    for k, name in enumerate(NAMES):
        lv = [r[1 + 2 * k] - REST for r in rows if r[1 + 2 * k] is not None]
        tp = [r[2 + 2 * k] - REST for r in rows if r[2 + 2 * k] is not None]
        out[name] = {"level_min": round(min(lv), 4) if lv else None, "level_max": round(max(lv), 4) if lv else None,
                     "top_max": round(max(tp), 4) if tp else None}
    out["rows"] = rows
    time.sleep(3)   # the stone (4 s) is gone before the next pond
    return out

res = {"on": [], "off": []}
for r in range(args.runs):
    for mode in ("on", "off"):
        o = run(mode == "on")
        res[mode].append(o)
        print("%-3s frames %4d | entry level min %+.3f | 0.33 m level max %+.3f (splash top %+.3f) | 0.67 m level max %+.3f (top %+.3f) | bodies %s" % (
            mode, o["frames"], o["0.00"]["level_min"], o["0.33"]["level_max"], o["0.33"]["top_max"], o["0.67"]["level_max"], o["0.67"]["top_max"],
            (o["feed"] or {}).get("bodies")), flush=True)
api.debug("water_coupling", {"solids": True})
(Path(__file__).resolve().parents[1] / "docs" / "evidence" / "water_core_e" / f"{args.tag}.json").write_text(json.dumps(res, indent=1), encoding="utf-8")
