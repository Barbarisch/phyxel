"""WaterCore §40 / 40.1 items 5-7 (L5) - the outcome check: where the viewer is must never change where things end up.

The Small bench (port 8111), the 4 x 4 m pond 1.5 m deep (floor top y 15, level 16.5), the shipped settings (auto-sleep on, so
the pond is in the record when the bodies arrive; displacement off; the push on). One 1/3 m Stone (x 101) and one 1/3 m Wood
block (x 103), z 14, both dropped 1 m onto the slept pond together. Only the WATER's viewer moves (water_locality test_viewer),
never the camera: the camera also moves the occupancy box, streaming and the capture (40.1 item 5).

Runs (each one pond, re-made; --runs picks):
  near_a, near_b   the viewer 6 m from the pond, twice: the repeatability control (40.1 item 6)
  far              the viewer 200 m away (beyond every reach)
  far_dry          far, with test_far_record_dry on: L5's red (40.1 item 7)

Compared against near_a: each body's rest position (the last logged pose; the log prints 6 significant digits - 1 mm in x/z at
x ~100, 0.1 mm in y), its time to rest (the solver's clock, A2j: the first snapshot from which it stays asleep), the pond's mass
once it has slept again (the record, read by a create that is refused), and its level (water_av_probe at the far corner).

Predictions, written before the first run (2026-10-28):
  near_a vs near_b  equal to the log's resolution, or the measured spread is the tolerance (stated);
  far vs near       equal within that spread (nothing reads the viewer for an outcome today);
  far_dry vs near   the wood ends ~0.5 m lower (on the bed, centre ~15.17, not floating ~16.4), the stone is the same, the pond's
                    mass is the same (it never woke), and nothing rises: no body's centre is ever above its release height.

    python tools/water_l5_outcome.py [--url http://127.0.0.1:8111] [--runs near_a,near_b,far,far_dry] [--seconds 30] [--tag l5]
Writes docs/evidence/water_core_m4/l5_<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, SOLVER_TICK
import water_float_rest as w

LEVEL, A = 16.5, 1.0 / 6.0
BODIES = {"stone": {"x": 101.0, "z": 14.0, "material": "Stone"}, "wood": {"x": 103.0, "z": 14.0, "material": "Wood"}}
Y0 = LEVEL + A + 1.0 + 0.01
NEAR = [102.0, 18.0, 20.0]       # 4 m south of the pond's edge
FAR = [302.0, 18.0, 14.0]        # 200 m east: beyond draw_m (48), the ripple window (42.7) and everything L3/L4 will add
RUNS = {"near_a": (NEAR, False), "near_b": (NEAR, False), "far": (FAR, False), "far_dry": (FAR, True)}
POND_BOX = {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16}


def tracks_xyz(path):
    """The particle log -> {slot: [(t, x, y, z, sleeping)]} on the solver's clock (snap_ticks, A2j), as water_bench.particle_tracks
    but with the whole position. A reused slot starts a new track when the body's spawn age (flags bits 16..23) goes DOWN: the
    log's first snapshot can be a frame stale and still show the last run's body in the slot the new one takes."""
    tr, t, age_of, n_of = {}, None, {}, {}
    with open(path) as f:
        for line in f:
            if line.startswith("F,"):
                p = line.rstrip("\n").split(",")
                if len(p) < 14: raise SystemExit("the particle log has no snap_ticks column - a stale engine binary (A2j)")
                t = int(p[13]) * SOLVER_TICK
            elif line.startswith("P,") and t is not None:
                p = line.split(",")
                flags = int(p[9])
                if not flags & 1: continue
                slot, age = int(p[2]), (flags >> 16) & 0xFF
                if slot in age_of and age < age_of[slot]: n_of[slot] = n_of.get(slot, 0) + 1
                age_of[slot] = age
                rows = tr.setdefault((slot, n_of.get(slot, 0)), [])
                if not rows or rows[-1][0] != t: rows.append((t, float(p[3]), float(p[4]), float(p[5]), bool(flags & 2)))
    return tr


def recorded_m3(api):
    r = api.debug("water_av_create", {**POND_BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "seed": "none"})
    if "error" not in r:   # it made a volume: the record held nothing - undo it, never leave a rig volume behind
        api.debug("water_av_destroy", {"id": r["volume"]["id"], "discard": True})
        return 0.0
    return r.get("recorded_m3")


def wait_slept(api, limit):
    t0 = time.time()
    while api.debug("water_av_list").get("volumes") and time.time() - t0 < limit: time.sleep(0.5)
    return not api.debug("water_av_list").get("volumes")


def kind(name):
    """near_a / near_c / far_b ... -> its (viewer, far-dry): any suffix after near / far / far_dry is a repeat"""
    for k in ("far_dry", "far", "near"):
        if name == k or name.startswith(k + "_") and not (k == "far" and name.startswith("far_dry")): return RUNS.get(k + "_a", RUNS.get(k))
    raise SystemExit(f"unknown run {name}")


def run(api, name, seconds):
    viewer, dry = kind(name)
    api.debug("water_locality", {"clear_test_viewer": True, "test_far_record_dry": False})
    w.AUTO_SLEEP = True; w.PRESSURE = True; w.DISPLACE = False
    w.pond(api)
    if not wait_slept(api, 60.0): raise SystemExit(f"{name}: the pond did not sleep into the record within 60 s")
    loc = api.debug("water_locality", {"test_viewer": viewer, "test_far_record_dry": dry})
    time.sleep(0.5)   # a frame or two: the viewer is set once a frame
    loc = api.debug("water_locality", {})
    if loc.get("test_far_record_dry") != dry or not loc.get("test_viewer_on"): raise SystemExit(f"{name}: water_locality not taken: {loc}")
    m_before = recorded_m3(api)
    kept0 = (api.debug("water_coupling", {}).get("slept") or {}).get("kept") or {}   # L3 (41.4 item 1): which wake path ran
    log = (w.EV / f"l5_{name}.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    for b in BODIES.values():
        r = api.debug("spawn_gpu_particle", {"x": b["x"], "y": Y0, "z": b["z"], "material": b["material"], "scale": 1 / 3,
                                             "lifetime": seconds + 30.0, "spin": 0.0})
        if not r.get("success"): raise SystemExit(f"spawn: {r}")
    t0, trace, woke = time.time(), [], False
    while time.time() - t0 < seconds:
        v = api.debug("water_av_list").get("volumes", [])
        if v: woke = True
        trace.append({"t": round(time.time() - t0, 2), "volumes": len(v), "mass": v[0].get("mass") if v else None})
        time.sleep(0.5)
    kept1 = (api.debug("water_coupling", {}).get("slept") or {}).get("kept") or {}
    hits, misses = kept1.get("hits", 0) - kept0.get("hits", 0), kept1.get("misses", 0) - kept0.get("misses", 0)
    wake_path = ("no wake" if hits == 0 and misses == 0 else "kept" if misses == 0 else "fresh" if hits == 0
                 else f"mixed ({hits} kept, {misses} fresh)")
    slept_again = wait_slept(api, 30.0)
    api.debug("particle_log", {"action": "stop"})
    m_after = recorded_m3(api)
    col = api.debug("water_av_column", {"points": [list(w.FAR)]}).get("columns") or [{}]   # the record's level (not the far-dry read)
    lvl = col[0].get("surface_y") if isinstance(col, list) and col else None
    # the bodies go BEFORE the switch: a wood block sunk on the bed of a pond that reads dry would read wet again and rise
    api.debug("clear_dynamics"); time.sleep(0.3)
    api.debug("water_locality", {"clear_test_viewer": True, "test_far_record_dry": False})
    tr = tracks_xyz(log)
    log.unlink(missing_ok=True)
    bodies = {}
    for key, b in BODIES.items():   # each body's track: the one released at its x
        # released at its x, first seen still in the air (the log can start a few ticks after the spawn)
        cand = [rows for rows in tr.values() if abs(rows[0][1] - b["x"]) < 0.01 and abs(rows[0][3] - b["z"]) < 0.01 and rows[0][2] > LEVEL + A + 0.5]
        if not cand: raise SystemExit(f"{name}: no track for the {key}; track starts: " +
                                      str([(s, rows[0], len(rows)) for s, rows in tr.items()][:12]))
        rows = max(cand, key=len)
        rest = next((i for i in range(len(rows)) if all(r[4] for r in rows[i:])), None)
        bodies[key] = {"rest_pose": list(rows[-1][1:4]), "rest_t": round(rows[rest][0] - rows[0][0], 4) if rest is not None else None,
                       "y_max": max(r[2] for r in rows), "y_min": min(r[2] for r in rows), "rows": len(rows)}
    out = {"run": name, "viewer": viewer, "far_record_dry": dry, "locality": loc, "pond_woke": woke, "pond_slept_again": slept_again,
           "recorded_m3_before": m_before, "recorded_m3_after": m_after, "level": lvl, "bodies": bodies, "pond": trace,
           "wake_path": wake_path, "kept_before_drop": kept0}
    print(f"{name:7s} woke {woke} ({wake_path} memory) slept again {slept_again} m3 {m_before}->{m_after} level {lvl} | " +
          " | ".join(f"{k} rest {[round(c, 4) for c in v['rest_pose']]} at {v['rest_t']} s, y max {v['y_max']:.4f}" for k, v in bodies.items()),
          flush=True)
    return out


def compare(ref, o):
    d = {"run": o["run"], "vs": ref["run"]}
    for k in BODIES:
        a, b = ref["bodies"][k], o["bodies"][k]
        d[k] = {"pose_mm": round(1000.0 * max(abs(x - y) for x, y in zip(a["rest_pose"], b["rest_pose"])), 2),
                "horiz_mm": round(1000.0 * ((a["rest_pose"][0] - b["rest_pose"][0]) ** 2 + (a["rest_pose"][2] - b["rest_pose"][2]) ** 2) ** 0.5, 2),
                "dy_mm": round(1000.0 * (b["rest_pose"][1] - a["rest_pose"][1]), 2),
                "rest_ticks": None if a["rest_t"] is None or b["rest_t"] is None else round((b["rest_t"] - a["rest_t"]) / SOLVER_TICK),
                "rose_above_release": b["y_max"] > Y0 + 1e-4}
    d["m3"] = None if ref["recorded_m3_after"] is None or o["recorded_m3_after"] is None else round(o["recorded_m3_after"] - ref["recorded_m3_after"], 5)
    d["level_mm"] = None if ref["level"] is None or o["level"] is None else round(1000.0 * (o["level"] - ref["level"]), 2)
    print("  ", json.dumps(d), flush=True)
    return d


def verdict(runs, self_check=True):
    """Each measure's spread over the near runs (40.1 item 6) sets its fence: a run passes when every measure lies inside
    [near min - spread - res, near max + spread + res] (res = the log's resolution: 1 mm across, 0.1 mm up, one tick).

    Why the spread is added on both sides: a plain min..max envelope fails runs drawn from the SAME distribution - with n near runs
    a new one falls outside a measure's range with chance 2 / (n + 1), and there are ten measures (40.4: 17 near runs, and most
    far runs 'failed' by a millimetre of drift). The self-check judges each near run against the others with the same rule; it
    must pass them all, or the rule is wrong, not the far runs."""
    near = [r for r in runs if r["run"].startswith("near")]
    if len(near) < 3: return {"note": "fewer than three near runs: no spread, no verdict"}
    def measures(r):
        m = {"m3": r["recorded_m3_after"], "level": r["level"]}
        for k in BODIES:
            b = r["bodies"][k]
            m[f"{k}_x"], m[f"{k}_y"], m[f"{k}_z"], m[f"{k}_rest_t"] = *b["rest_pose"], b["rest_t"]
        return m
    res = {"near_runs": [r["run"] for r in near], "spread": {}, "runs": {}}
    if self_check:   # 41.4 item 1: with L3, near and far must take different wake paths, or near-vs-far compares one path with itself
        paths = {r["run"]: r.get("wake_path") for r in runs if "wake_path" in r}
        res["wake_paths"] = paths
        if paths: print(f"  wake paths: {paths}", flush=True)
    nm = [measures(r) for r in near]
    for key in nm[0]:
        vals = [m[key] for m in nm if m[key] is not None]
        # a hair over one tick for times: they are tick multiples, rounded in the JSON
        pad = 1.01 * SOLVER_TICK if key.endswith("rest_t") else (1e-4 if key.endswith("_y") or key == "level" else (1e-3 if key[-2:] in ("_x", "_z") else 1e-5))
        res["spread"][key] = {"min": min(vals), "max": max(vals), "tol": (max(vals) - min(vals)) + pad}
    if self_check and len(near) < 4:
        res["self_check"] = {"note": "fewer than four near runs: no leave-one-out check (pool sessions with --rejudge a,b,...)"}
        print("  self-check: skipped (fewer than four near runs) - the fence below is thin", flush=True)
    elif self_check:   # leave-one-out: each near run against the rest
        fails = []
        for i, r in enumerate(near):
            v = verdict([{**o, "run": "near" + str(j)} for j, o in enumerate(near) if j != i] + [{**r, "run": "self"}], self_check=False)
            if not v["runs"]["self"]["pass"]: fails.append((r["run"], v["runs"]["self"]["outside"]))
        res["self_check"] = {"near_runs": len(near), "failed": fails}
        print(f"  self-check: {len(fails)} of {len(near)} near runs fall outside the others' fence {fails}", flush=True)
    for r in runs:
        if r["run"].startswith("near"): continue
        out = {}
        for key, v in measures(r).items():
            s = res["spread"][key]
            out[key] = {"value": v, "inside": v is not None and s["min"] - s["tol"] <= v <= s["max"] + s["tol"]}
        rose = any(r["bodies"][k]["y_max"] > Y0 + 1e-4 for k in BODIES)
        res["runs"][r["run"]] = {"pass": all(o["inside"] for o in out.values()) and not rose, "rose": rose,
                                 "outside": [k for k, o in out.items() if not o["inside"]], "measures": out}
        if self_check: print(f"  verdict {r['run']}: {'PASS' if res['runs'][r['run']]['pass'] else 'FAIL'}; outside the near spread: "
              f"{res['runs'][r['run']]['outside']}; rose: {rose}", flush=True)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--runs", default="near_a,near_b,near_c,far_a,far_b,far_c,far_dry")
    ap.add_argument("--rejudge", default="", help="a saved l5_<tag>.json: print its verdict again, run nothing")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--tag", default="l5")
    a = ap.parse_args()
    if a.rejudge:   # one tag re-scores that file; several pool their runs (three near runs a session is a thin spread)
        tags = a.rejudge.split(",")
        runs = []
        for t in tags:
            for r in json.loads((w.EV / f"l5_{t}.json").read_text(encoding="utf-8"))["runs"]:
                runs.append({**r, "run": f"{r['run']}@{t}"})
        v = verdict(runs)
        (w.EV / f"l5_pooled_{'_'.join(tags)}.json" if len(tags) > 1 else w.EV / f"l5_{tags[0]}_verdict.json").write_text(json.dumps(v, indent=1), encoding="utf-8")
        return
    api = Api(a.url); st = api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    res = {"status": {k: st.get(k) for k in ("build_config", "version", "git") if k in st}, "y0": Y0, "runs": [], "compare": []}
    try:
        for name in a.runs.split(","):
            res["runs"].append(run(api, name, a.seconds))
        ref = res["runs"][0]
        for o in res["runs"][1:]: res["compare"].append(compare(ref, o))
        res["verdict"] = verdict(res["runs"])
    finally:
        api.debug("clear_dynamics"); time.sleep(0.3)   # the bodies first (see run)
        api.debug("water_locality", {"clear_test_viewer": True, "test_far_record_dry": False})
        (w.EV / f"l5_{a.tag}.json").write_text(json.dumps(res, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
