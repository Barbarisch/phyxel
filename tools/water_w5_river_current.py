"""WaterCore.md §54 (W5.1) - a generated river's record carries its current: wood debris dropped in a generated river drifts
downstream; the same drop in a generated lake (the control) stays put.

WaterBench_RiverCarve (port 8113; Mountains, streaming, water.enabled off as shipped). The river: the 51.14 reach near
(-1729, -1040) (stage ~20.06). The lake: the 51.13 lake near (-2229, -1350) (level 90). For each site: the camera parked nearby
(the chunks stream, the record is read); water_query on a 13 x 13 m grid round the site, probing just under the known level,
picks the 3 deepest wet columns (on the river: the route's current there); three 1/3 m WOOD GPU debris blocks dropped 1.2 m above
the surface; their x / z logged for --seconds (particle_log); the drift = the last logged position - the first one at rest height.

Prediction (written before the run, §54): before the build the river's drift ~0 m like the lake's (the record holds no current);
after it, in the river each block moves along the route's direction (within 30 deg) by >= 50 % of speed x time (or until it
grounds on a bank - reported), the lake's < 0.5 m.

    python tools/water_w5_river_current.py [--seconds 20] [--tag w5_current] [--sites river,lake]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, math, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
SITES = {
    "river": {"c": (-1729.0, -1040.0), "level": 20.06, "cam": {"x": -1700.0, "y": 40.0, "z": -1040.0, "yaw": 180.0, "pitch": -35.0}},
    "lake": {"c": (-2229.0, -1350.0), "level": 90.0, "cam": {"x": -2215.0, "y": 96.0, "z": -1350.0, "yaw": 180.0, "pitch": -30.0}},
}
SOLVER_TICK = 1.0 / 60.0
SKIP = 0


def tracks_xz(path):
    """particle_log -> {slot: [(t, x, y, z)]}, active bodies; t from the snapshot's ticks (water_bench.particle_tracks' rule)."""
    out, t = {}, None
    with open(path) as f:
        for line in f:
            if line.startswith("F,"):
                p = line.rstrip("\n").split(",")
                if len(p) < 14: raise SystemExit("stale binary: the particle log has no snap_ticks")
                t = int(p[13]) * SOLVER_TICK
            elif line.startswith("P,") and t is not None:
                p = line.split(",")
                if not int(p[9]) & 1: continue
                out.setdefault(int(p[2]), []).append((t, float(p[3]), float(p[4]), float(p[5])))
    return out


def pick(api, site):
    s = SITES[site]; c = s["c"]; y = s["level"] - 0.15
    pts = [[c[0] + dx + 0.5, y, c[1] + dz + 0.5] for dz in range(-6, 7, 2) for dx in range(-6, 7, 2)]
    rows = []
    for i in range(0, len(pts), 64):
        rows += api.debug("water_query", {"points": pts[i:i + 64]})["points"]
    wet = [r for r in rows if r.get("in_water")]
    wet.sort(key=lambda r: -(r.get("river") or {}).get("depth_m", 0.0))
    return wet, len(rows)


def take(api, site, seconds, tag):
    s = SITES[site]
    camera_set(api, s["cam"]); time.sleep(12.0)
    wet, probed = pick(api, site)
    res = {"site": site, "probed": probed, "wet": len(wet)}
    if len(wet) < 3:
        res["error"] = f"only {len(wet)} wet columns of {probed}"; print(res); return res
    drops = wet[SKIP:SKIP + 3]   # --skip: other columns, so a take never lands on an earlier take's blocks (120 s lifetime)
    res["drops"] = [{"p": d["p"], "surface_y": d["surface_y"], "flow": d["flow"], "source": d["source"], "river": d.get("river")} for d in drops]
    log = (EV / f"{tag}_{site}_tmp.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    time.sleep(0.5)
    for d in drops:
        api.debug("spawn_gpu_particle", {"x": d["p"][0], "y": d["surface_y"] + 1.2, "z": d["p"][2], "material": "Wood",
                                         "scale": 1.0 / 3.0, "lifetime": 120.0, "spin": 0.0})
    time.sleep(seconds)
    api.debug("particle_log", {"action": "stop"})
    tr = tracks_xz(log); log.unlink(missing_ok=True)
    out = []
    for slot, ps in tr.items():
        if len(ps) < 10: continue
        surf = min(drops, key=lambda d: (d["p"][0] - ps[0][1]) ** 2 + (d["p"][2] - ps[0][3]) ** 2)
        if (surf["p"][0] - ps[0][1]) ** 2 + (surf["p"][2] - ps[0][3]) ** 2 > 9.0 or ps[0][2] < surf["surface_y"] + 0.6: continue   # not this take's drop
        # in water: the first sample within 0.6 m of the surface after the fall
        k0 = next((i for i, p in enumerate(ps) if p[2] < surf["surface_y"] + 0.6), None)
        if k0 is None: out.append({"slot": slot, "error": "never reached the water"}); continue
        p0, p1 = ps[k0], ps[-1]
        dx, dz = p1[1] - p0[1], p1[3] - p0[3]
        dist, dt = math.hypot(dx, dz), p1[0] - p0[0]
        rv = surf.get("river") or {}
        d = rv.get("dir") or [0.0, 0.0]
        path = [ps[k0 + (len(ps) - 1 - k0) * j // 4] for j in range(5)]
        q = api.debug("water_query", {"points": [[p[1], p[2], p[3]] for p in path]})["points"]
        dirs = [(r.get("river") or {}).get("dir") or [0.0, 0.0] for r in q]
        md = [sum(v[0] for v in dirs) / 5.0, sum(v[1] for v in dirs) / 5.0]
        mdl = math.hypot(*md)
        ang_path = math.degrees(math.acos(max(-1.0, min(1.0, (dx * md[0] + dz * md[1]) / (dist * mdl))))) if dist > 1e-3 and mdl > 0.3 else None
        speed = rv.get("speed_mps", 0.0)
        ang = math.degrees(math.acos(max(-1.0, min(1.0, (dx * d[0] + dz * d[1]) / dist)))) if dist > 1e-3 and math.hypot(*d) > 0.5 else None
        # did it stall (the last 3 s moved < 0.2 m: aground on a bank or the current gone)?
        tail = [p for p in ps if p[0] > p1[0] - 3.0]
        stalled = math.hypot(tail[-1][1] - tail[0][1], tail[-1][3] - tail[0][3]) < 0.2 if len(tail) > 1 else None
        out.append({"slot": slot, "t_in_water_s": round(dt, 2), "drift_m": round(dist, 3), "drift_xz": [round(dx, 3), round(dz, 3)],
                    "angle_to_route_deg": None if ang is None else round(ang, 1),
                    "angle_to_route_along_path_deg": None if ang_path is None else round(ang_path, 1), "route_speed_mps": speed,
                    "speed_x_time_m": round(speed * dt, 2), "mean_speed_mps": round(dist / dt, 3) if dt > 0 else None,
                    "stalled_last_3s": stalled, "y_last": round(p1[2], 3), "end": [round(p1[1], 2), round(p1[3], 2)]})
    res["debris"] = out
    for o in out: print(f"  {site}: {o}", flush=True)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--tag", default="w5_current")
    ap.add_argument("--sites", default="river,lake")
    ap.add_argument("--off", action="store_true", help="the red control: the record's current off (water_av_river on:false) - reads as before 54")
    ap.add_argument("--skip", type=int, default=0)
    a = ap.parse_args()
    global SKIP; SKIP = a.skip
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    try: knobs = api.debug("water_av_river", {"on": not a.off})
    except Exception as e: knobs = {"error": str(e)}
    out = {"bench": "WaterBench_RiverCarve", "seconds": a.seconds, "river_knobs": knobs, "takes": []}
    for site in a.sites.split(","):
        out["takes"].append(take(api, site, a.seconds, a.tag))
    if a.off: api.debug("water_av_river", {"on": True})
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
