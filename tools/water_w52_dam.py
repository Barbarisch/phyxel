"""WaterCore.md §54.2 (W5.2) - the dam in a simulated reach, measured live (the clip's caption "the water backs up, below it the current
dies" checked, not assumed). WaterBench_RiverCarve (8113): the reach at the 51.14 site (half_m 20), 20 s to settle; then
water_w52_clip's dam (stone across the river's water columns, 6 m downstream of the site, crest y 21); the surface and the current
read on the river's centre line 8 m upstream and 8 m downstream of the dam line, before it and 25 s after; the dam removed again.
Predictions (written before the run, from ASimulatedReachCarriesTheRecordsRiver's dam rig): upstream rises >= 0.1 m; downstream the
current falls under half of what it was.

    python tools/water_w52_dam.py [--tag w52_dam]
"""
import argparse, json, math, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job
import water_w52_reach as w
from water_w52_clip import dam_columns
from water_w5_clip import POSE

EV = w.EV


def read(api, pts, d=None):
    rows = api.debug("water_query", {"points": [[x, w.SITE["level"] - 0.15, z] for x, z in pts]})["points"]
    ys = [r["surface_y"] for r in rows if r.get("in_water")]
    sp = [math.hypot(*(r.get("flow") or [0, 0])[:2]) for r in rows if r.get("in_water")]
    out = {"wet": len(ys), "surface_mean": round(statistics.mean(ys), 3) if ys else None, "speed_mean": round(statistics.mean(sp), 3) if sp else None}
    if d:   # the current ALONG the river (+ downstream, - back upstream)
        al = [(r.get("flow") or [0, 0])[0] * d[0] + (r.get("flow") or [0, 0])[1] * d[1] for r in rows if r.get("in_water")]
        out["along_mean"] = round(statistics.mean(al), 3) if al else None
    return out


def ground(api, x, z):   # the top of the ground at a column (the highest existing voxel from y 24 down)
    for y in range(24, 12, -1):
        if api.get(f"/api/world/voxel?x={x}&y={y}&z={z}").get("exists"): return y + 1
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--tag", default="w52_dam")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(600)
    print("realtime:", api.debug("water_av_realtime", {"on": True}))
    camera_set(api, POSE); time.sleep(12.0)
    w.release_all(api)
    r = w.make(api, 20.0)
    time.sleep(20.0)
    d, dam = dam_columns(api)
    c = w.SITE["c"]; centre = (c[0] + 6.0 * d[0], c[1] + 6.0 * d[1]); perp = (-d[1], d[0])
    def line(off): return [(centre[0] + off * d[0] + t * perp[0], centre[1] + off * d[1] + t * perp[1]) for t in (-2, -1, 0, 1, 2)]
    up, down = line(-8.0), line(8.0)
    before = {"up": read(api, up, d), "down": read(api, down, d)}
    print("before:", before, flush=True)
    # the dam's ends: the ground beside its first and last column, along the line (a crest at y 21 leaks round an end lower than that)
    ends = []
    for (x, z, yb), sgn in ((dam[0], -1), (dam[-1], 1)):
        for k in (1, 2, 3):
            ex, ez = int(math.floor(x + 0.5 + sgn * k * perp[0])), int(math.floor(z + 0.5 + sgn * k * perp[1]))
            ends.append({"x": ex, "z": ez, "ground_top": ground(api, ex, ez)})
    print("dam ends' ground:", ends, flush=True)
    t0 = time.time()
    for x, z, yb in dam: world_job(api, "/api/world/fill", {"x1": x, "y1": yb, "z1": z, "x2": x + 1, "y2": 20, "z2": z + 1}, material="Stone")
    placed_s = round(time.time() - t0, 1)
    time.sleep(25.0)
    after = {"up": read(api, up, d), "down": read(api, down, d)}
    print("after:", after, f"(dam placed in {placed_s} s)", flush=True)
    v = w.av(api, r["id"])
    for x, z, yb in dam: world_job(api, "/api/world/clear", {"x1": x, "y1": yb, "z1": z, "x2": x + 1, "y2": 20, "z2": z + 1})
    rel = api.debug("water_av_river_reach", {"release": r["id"]})
    out = {"reach": r, "dam_columns": dam, "dam_ends_ground": ends, "record_dir": d, "dam_place_s": placed_s, "before": before, "after": after,
           "rise_up_m": None if not (before["up"]["surface_mean"] and after["up"]["surface_mean"]) else round(after["up"]["surface_mean"] - before["up"]["surface_mean"], 3),
           "down_speed_ratio": None if not (before["down"]["speed_mean"] and after["down"]["speed_mean"] is not None) else round(after["down"]["speed_mean"] / before["down"]["speed_mean"], 3),
           "exchange_m3": v.get("boundary_exchange_m3") if v else None, "release": rel}
    print({k: out[k] for k in ("rise_up_m", "down_speed_ratio", "exchange_m3")}, "down along before/after:", before["down"].get("along_mean"), after["down"].get("along_mean"))
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
