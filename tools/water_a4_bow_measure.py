"""WaterCore §58 item 3 - A4's bow wave MEASURED before the clip is remade (the owner on the first cut: "cant really tell a
difference"). WaterBench_Small (port 8111), the sweep clip's channel rig exactly (x 66-77, z 14-15, 0.8 m deep, the NPC patrolling
x 66.8 <-> 76.4 at 2 m/s), characters OFF then ON. Every 0.25 s for 12 s: the wader's position (/api/npcs) and the water surface
(water_av_column surface_mean) on a 0.5 m grid 3 x 2 m round it; per sample the largest |surface - rest| within 0.75 m ahead of the
wader (along its motion), and anywhere in the grid. Rest = the take's median surface before the wader spawns.
    python tools/water_a4_bow_measure.py [--out docs/evidence/water_core_m4/a4_bow_measure.json]
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_sweep_clip as W

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"


def wader(api):
    d = api.get("/api/npcs")
    for n in d.get("npcs", d if isinstance(d, list) else []):
        if n.get("name") == W.NPC or n.get("id") == W.NPC:
            p = n.get("position") or {}
            return float(p.get("x", 0)), float(p.get("z", 0))
    return None


def surface(api, pts):
    out = {}
    for i in range(0, len(pts), 64):
        for c, p in zip(api.debug("water_av_column", {"points": pts[i:i + 64]}).get("columns", []), pts[i:i + 64]):
            if c.get("surface_mean") is not None: out[(p[0], p[1])] = c["surface_mean"]
    return out


def take(api, mode):
    api.debug("water_coupling", {"characters": mode == "on"})
    api.post("/api/npc/remove", {"name": W.NPC})
    W.clear_volumes(api)
    r = api.debug("water_av_create", {**W.CH_BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto", "auto_sleep": False})
    if "error" in r: raise SystemExit(f"create: {r}")
    api.debug("place_water_box", {"x1": 66, "y1": 16, "z1": 14, "x2": 77, "y2": 16, "z2": 15, "mass": W.DEPTH, "target": "core"})
    api.debug("water_av_realtime", {"on": True})
    time.sleep(4.0)
    grid = [[66.25 + 0.5 * i, 14.25 + 0.5 * k] for i in range(22) for k in range(4)]
    rest = statistics.median(surface(api, grid).values())
    api.post("/api/npc/spawn", {"name": W.NPC, "position": {"x": 66.8, "y": 16.0, "z": W.LANE_Z}, "behavior": "patrol", "walkSpeed": 2.0,
                                "waitTime": 0.5, "waypoints": [{"x": 66.8, "y": 16.0, "z": W.LANE_Z}, {"x": 76.4, "y": 16.0, "z": W.LANE_Z}]})
    rows = []; prev = None; t0 = time.time()
    while time.time() - t0 < 12.0:
        p = wader(api)
        if p:
            vx = 0.0 if prev is None else p[0] - prev[0]
            pts = [[round(p[0] + dx, 2), round(z, 2)] for dx in (-1.5, -1.0, -0.5, 0.0, 0.5, 1.0, 1.5) for z in (14.25, 14.75)]
            s = surface(api, pts)
            dev = {k: abs(v - rest) for k, v in s.items()}
            ahead = [d for (x, z), d in dev.items() if vx != 0.0 and 0.0 < (x - p[0]) * (1 if vx > 0 else -1) <= 0.75]
            rows.append({"t": round(time.time() - t0, 2), "x": round(p[0], 2), "vx": round(vx, 3),
                         "ahead_mm": round(1000 * max(ahead), 2) if ahead else None, "grid_mm": round(1000 * max(dev.values()), 2) if dev else None})
            prev = p
        time.sleep(0.25)
    api.post("/api/npc/remove", {"name": W.NPC})
    ah = [r["ahead_mm"] for r in rows if r["ahead_mm"] is not None]; gr = [r["grid_mm"] for r in rows if r["grid_mm"] is not None]
    summ = {"mode": mode, "rest": rest, "samples": len(rows),
            "ahead_mm_median": statistics.median(ah) if ah else None, "ahead_mm_max": max(ah) if ah else None,
            "grid_mm_median": statistics.median(gr) if gr else None, "grid_mm_max": max(gr) if gr else None}
    print(json.dumps(summ), flush=True)
    return {"summary": summ, "rows": rows}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(EV / "a4_bow_measure.json"))
    a = ap.parse_args()
    api = Api("http://127.0.0.1:8111"); api.wait_status(600)
    out = []
    try:
        for mode in ("off", "on"): out.append(take(api, mode))
    finally:
        api.post("/api/npc/remove", {"name": W.NPC})
        api.debug("water_coupling", {"characters": True})
        W.clear_volumes(api)
    pathlib.Path(a.out).write_text(json.dumps(out, indent=1))
    print("wrote", a.out)


if __name__ == "__main__":
    main()
