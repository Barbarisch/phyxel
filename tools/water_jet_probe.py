"""WaterCore 21.7 S1, the measurement before building the droplet crown: what does a stone entry throw ABOVE the
pond, live? The Small bench pond (1/3 m cells, the 20.14 rig), a 1/3 m stone dropped from 3 m, every frame for
2 s: every DETACHED run of water (engine/include/core/water/WaterDroplets.h) via /api/debug/water_jet_scan.
The 21.2 birth rule assumes the floating slab is made of ISOLATED runs holding less than one cell of water
(sum f < 1); this reports, over all frames, the runs above the rest level by isolation and size.

Usage: python tools/water_jet_probe.py [--url http://127.0.0.1:8111] [--runs 3] [--tag s1_jet]"""
import argparse, json, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api

ap = argparse.ArgumentParser()
ap.add_argument("--url", default="http://127.0.0.1:8111")
ap.add_argument("--runs", type=int, default=3)
ap.add_argument("--tag", default="s1_jet")
args = ap.parse_args()
api = Api(args.url); api.wait_status(900)
REST = 16.5
X, Z = 101.5, 13.5


def pond():
    for v in api.debug("water_av_list").get("volumes", []):
        api.debug("water_av_destroy", {"id": v["id"]})
    api.debug("water_av_create", {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16, "cellSize": 1.0 / 3.0,
                                  "transport": "eulerian", "backend": "auto", "auto_sleep": False})
    api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
    api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
    api.debug("water_av_realtime", {"on": True})


def summarize(rows, frames):
    above = [r for r in rows if r[2] > REST + 1.0 / 6.0]   # bottom cell centre above the rest level's cell
    def bucket(rs):
        return {"run_frames": len(rs), "sum_f_lt_1": sum(1 for r in rs if r[5] < 1.0), "sum_f_ge_1": sum(1 for r in rs if r[5] >= 1.0),
                "max_sum_f": round(max((r[5] for r in rs), default=0.0), 3), "max_cells": int(max((r[6] for r in rs), default=0)),
                "highest_bottom_above_rest_m": round(max((r[2] - REST for r in rs), default=0.0), 2),
                "frames_with_any": len({r[0] for r in rs})}
    return {"frames": frames, "all_detached_above_rest": bucket(above),
            "isolated": bucket([r for r in above if r[7] > 0.5]), "not_isolated": bucket([r for r in above if r[7] <= 0.5])}


api.debug("water_coupling", {"enabled": True, "solids": True})
out = {"runs": []}
for k in range(args.runs):
    pond(); time.sleep(6)
    api.debug("water_jet_scan", {"on": True})
    api.debug("spawn_gpu_particle", {"x": X, "y": 19.5, "z": Z, "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 4.0})
    time.sleep(2.0)
    res = api.debug("water_jet_scan", {"on": False}, timeout=120)
    s = summarize(res["rows"], res["frames"])
    out["runs"].append({"summary": s, "rows": res["rows"]})
    print(json.dumps(s))
    time.sleep(3)
(Path(__file__).resolve().parents[1] / "docs" / "evidence" / "water_core_e" / f"{args.tag}.json").write_text(json.dumps(out), encoding="utf-8")
