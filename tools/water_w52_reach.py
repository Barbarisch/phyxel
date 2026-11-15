"""WaterCore.md §54.2 (W5.2) - a near REACH of a generated river simulated, fed and drained at its edges from the record (L4).

WaterBench_RiverCarve (port 8113), the 51.14 reach near (-1729, -1040) (stage ~20.06). `water_av_river_reach {x, z}` makes a reach
(a 40 x 40 m box of 8 m record tiles at the default half_m 20, 1/9 m, seeded from the record WITH its current, Manning n 0.035,
edges: inflow columns at the record's level and current, level-only open columns elsewhere); after --settle seconds:
  - the CURRENT: on the 13 x 13 m grid round the site (W5.1's), the simulated surface velocity (water_query's flow, source volume)
    against the record's (its river.speed_mps / dir) - mean ratio and angle;
  - the SEAM: for every 4th ring column with water, the surface 1.5 m inside the box (the simulation) and 1.5 m outside (the
    record) - |difference| max / mean;
  - the BOOKS: water now - seeded - what the edges exchanged + residue (water_av_list);
  - the WOOD: W5.1's take (three 1/3 m wood blocks on the 3 deepest columns, their drift over --seconds) inside the reach.
Predictions (written before the run, §54.2): the simulated speed within 15 % of the record's on the river's columns; the seam <= 3 cm;
the books within 1e-3 m^3 (a 40 x 40 m reach for ~40 s; the integration rig: 2e-5 over 30 s); the wood drifts along the river at
>= 50 % of the record's speed x time (as W5.1 on the record). RED (--red: the same reach with no edges, `edges:false`): the wood's
drift < 2 m and the simulated speed falls far below the record's (the seeded current sloshes off the box's walls).

    python tools/water_w52_reach.py [--tag w52_reach] [--red] [--settle 20] [--seconds 20] [--half 20]
    python tools/water_w52_reach.py --cost [--reps 4]      # whole frame with the reach made vs none, ABBA
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, math, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_w5_river_current as w51

EV = w51.EV
SITE = w51.SITES["river"]


def make(api, half, edges=True):
    c = SITE["c"]
    r = api.debug("water_av_river_reach", {"x": c[0], "z": c[1], "half_m": half, "y": SITE["level"], "edges": edges}, timeout=300)
    print("reach:", {k: r.get(k) for k in ("success", "id", "error", "min", "max", "ring_columns", "inflow_columns", "level_only_columns",
                                           "record_q_in_m3s", "record_q_out_m3s", "seeded_m3", "make_ms", "edges")}, flush=True)
    return r


def release_all(api):
    for v in api.debug("water_av_list").get("volumes", []):
        if v.get("is_reach"): print("release:", api.debug("water_av_river_reach", {"release": v["id"]}))


def av(api, rid):
    return next((v for v in api.debug("water_av_list").get("volumes", []) if v.get("id") == rid), None)


def current(api):
    c = SITE["c"]; y = SITE["level"] - 0.15
    pts = [[c[0] + dx + 0.5, y, c[1] + dz + 0.5] for dz in range(-6, 7, 2) for dx in range(-6, 7, 2)]
    rows = api.debug("water_query", {"points": pts})["points"]
    ratios, angles, n = [], [], 0
    for r in rows:
        rv = r.get("river") or {}
        if not r.get("in_water") or not rv.get("on_river") or rv.get("speed_mps", 0.0) < 0.05: continue
        n += 1
        f = r.get("flow") or [0.0, 0.0, 0.0]
        fx, fz = (f[0], f[2]) if len(f) == 3 else (f[0], f[1])
        sp = math.hypot(fx, fz)
        ratios.append(sp / rv["speed_mps"])
        d = rv.get("dir") or [0.0, 0.0]
        if sp > 1e-3: angles.append(math.degrees(math.acos(max(-1.0, min(1.0, (fx * d[0] + fz * d[1]) / sp)))))
    srcs = sorted({r.get("source") for r in rows if r.get("in_water")} - {None})
    return {"river_columns": n, "speed_ratio_mean": round(statistics.mean(ratios), 3) if ratios else None,
            "speed_ratio_min": round(min(ratios), 3) if ratios else None, "speed_ratio_max": round(max(ratios), 3) if ratios else None,
            "angle_mean_deg": round(statistics.mean(angles), 1) if angles else None, "angle_max_deg": round(max(angles), 1) if angles else None,
            "sources": srcs}


def seam(api, lo, hi):
    y = SITE["level"] - 0.15
    pairs = []
    for x in range(lo[0], hi[0] + 1, 4):
        pairs.append(((x + 0.5, lo[2] + 1.5), (x + 0.5, lo[2] - 1.5))); pairs.append(((x + 0.5, hi[2] - 0.5), (x + 0.5, hi[2] + 2.5)))
    for z in range(lo[2], hi[2] + 1, 4):
        pairs.append(((lo[0] + 1.5, z + 0.5), (lo[0] - 1.5, z + 0.5))); pairs.append(((hi[0] - 0.5, z + 0.5), (hi[0] + 2.5, z + 0.5)))
    pts = [[p[0], y, p[1]] for a, b in pairs for p in (a, b)]
    rows = []
    for i in range(0, len(pts), 64): rows += api.debug("water_query", {"points": pts[i:i + 64]})["points"]
    d = []
    for k in range(len(pairs)):
        a, b = rows[2 * k], rows[2 * k + 1]
        if a.get("in_water") and b.get("in_water"):
            d.append({"inside": pairs[k][0], "in_src": a.get("source"), "in_y": a.get("surface_y"), "out_src": b.get("source"),
                      "out_y": b.get("surface_y"), "diff_m": round(a["surface_y"] - b["surface_y"], 4)})
    ad = [abs(e["diff_m"]) for e in d]
    return {"pairs": len(d), "max_abs_m": round(max(ad), 4) if ad else None, "mean_abs_m": round(statistics.mean(ad), 4) if ad else None,
            "worst": max(d, key=lambda e: abs(e["diff_m"])) if d else None, "rows": d}


def measure(api, a):
    camera_set(api, SITE["cam"]); time.sleep(12.0)
    release_all(api)
    r = make(api, a.half, edges=not a.red)
    out = {"bench": "WaterBench_RiverCarve", "red": a.red, "reach": r}
    if not r.get("success"): return out
    rid = r["id"]
    t0 = time.time()
    time.sleep(a.settle)
    out["current"] = current(api); print("current:", out["current"], flush=True)
    out["seam"] = seam(api, r["min"], r["max"]); print("seam:", {k: out["seam"][k] for k in ("pairs", "max_abs_m", "mean_abs_m", "worst")}, flush=True)
    w51.SKIP = a.skip
    out["wood"] = w51.take(api, "river", a.seconds, a.tag)
    v = av(api, rid)
    if v:
        books = v["mass"] - v.get("seeded_mass", 0.0) - v.get("boundary_exchange_m3", 0.0) + v.get("residue_dropped_m3", 0.0)
        out["books"] = {"mass_m3": v["mass"], "seeded_m3": v.get("seeded_mass"), "exchange_m3": v.get("boundary_exchange_m3"),
                        "residue_m3": v.get("residue_dropped_m3"), "books_m3": books, "after_s": round(time.time() - t0, 1),
                        "boundary_columns": v.get("boundary_columns"), "boundary_inflow": v.get("boundary_inflow"),
                        "gpu_device_ms": v.get("gpu_device_ms"), "asleep": v.get("asleep")}
        print("books:", out["books"], flush=True)
    out["release"] = api.debug("water_av_river_reach", {"release": rid})
    return out


def timing(api, seconds):
    xs = []
    t_end = time.time() + seconds
    while time.time() < t_end:
        try: xs.append(api.get("/api/debug/engine_timing"))
        except Exception: pass
        time.sleep(0.25)
    def m(k): v = [x[k] for x in xs if isinstance(x.get(k), (int, float))]; return round(statistics.mean(v), 3) if v else None
    return {"fps": m("fps"), "cpu_ms": m("cpuFrameTime"), "gpu_ms": m("gpu_frame_ms"), "n": len(xs)}


def cost(api, a):
    camera_set(api, SITE["cam"]); time.sleep(15.0)
    release_all(api); time.sleep(5.0)
    rows = []
    for rep in range(a.reps):
        for state in (("off", "on") if rep % 2 == 0 else ("on", "off")):
            rid = None; mk = None
            if state == "on":
                mk = make(api, a.half); rid = mk.get("id"); time.sleep(5.0)   # past the make's stall
            t = timing(api, 10.0); t.update({"rep": rep, "state": state, "make_ms": mk.get("make_ms") if mk else None})
            if rid:
                v = av(api, rid); t["gpu_device_ms"] = v.get("gpu_device_ms") if v else None
                api.debug("water_av_river_reach", {"release": rid}); time.sleep(5.0)
            print(t, flush=True); rows.append(t)
    def mean(state, k): v = [r[k] for r in rows if r["state"] == state and r[k] is not None]; return round(statistics.mean(v), 3) if v else None
    return {"rows": rows, "summary": {s: {k: mean(s, k) for k in ("fps", "cpu_ms", "gpu_ms")} for s in ("off", "on")}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--tag", default="w52_reach")
    ap.add_argument("--red", action="store_true")
    ap.add_argument("--cost", action="store_true")
    ap.add_argument("--reps", type=int, default=4)
    ap.add_argument("--settle", type=float, default=20.0)
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--half", type=float, default=20.0)
    ap.add_argument("--skip", type=int, default=0)
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    # the water core steps only with realtime ON - no game session turns it on (only this route; every live water tool since 51 does
    # it, WaterCore 54.2 found it: the first w52 run measured an unstepped reach - its "current" was the seed's)
    print("realtime:", api.debug("water_av_realtime", {"on": True}))
    out = cost(api, a) if a.cost else measure(api, a)
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{a.tag}.json")


if __name__ == "__main__":
    main()
