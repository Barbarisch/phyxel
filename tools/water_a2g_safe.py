"""WaterCore 33 (plan A2g) - no block ever floats up: a body that pumps the water is pushed by the water's pressure, never by the
table law. Live on the Small bench (port 8111), Release. docs/WaterCore.md 33 has the design, the predictions and the gates.

The knob: water_coupling {test_refuse_windows} refuses every pressure window (as over budget) - the push's fallback, forced.
Scenes (each in the 4 x 4 m pond, 1/9 m cells, auto-sleep off, shipped settings otherwise):
  stone    - a 1/3 m Stone dropped from 1 m, knob ON. A stone cannot rise under any of these laws (buoyancy ratio 0.385).
             RED (before the fix): rasterized bodies >= 1 while every window is refused. GREEN: 0, and refused_no_window >= 1.
             This is the GATE: the wood scenes refuse to run unless it passes (a wrong fix would launch wood in front of the owner).
  control  - 1/3 m and 1 m Wood dropped from 1 m, knob OFF (the signed-off push). Its peak after entry is the bound.
  refused  - the same drops, knob ON. Peak after entry <= the control's + 2 cm; at rest (y moves < 1 cm over the last 2 s).
  stress   - 1,100 1/9 m Wood blocks into the S9 pit (8 x 8 m), knob OFF: at every sample every rasterized body holds a window
             (rasterized <= windows; the windows cap at 1,024 while 1,100 are wet), and no block rises above the height it was
             spawned at (a launch would; a pile cannot). Whole frame reported.
Body poses come from the engine's per-frame particle log (every body, every frame).

    python tools/water_a2g_safe.py --only stone [--tag red]       # the red / the gate
    python tools/water_a2g_safe.py [--only stone,control,refused,stress] [--tag green]
Writes docs/evidence/water_core_m4/a2g_<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, particle_tracks
import water_float_rest as w

PIT_BOX = {"x1": 149, "y1": 11, "z1": 9, "x2": 158, "y2": 19, "z2": 18}
PIT_WATER = {"x1": 150, "y1": 12, "z1": 10, "x2": 157, "y2": 15, "z2": 17}   # 8 x 4 x 8 m, floor top 11 -> level 16.0
PIT_LEVEL = 16.0


def read_log(path):
    """-> {track: [(t, y, vy, sleeping)]} from the particle log, on the solver's clock (water_bench.particle_tracks, WaterCore A2j:
    the frame dt summed here before undercounted time - this tool's times and vy were wrong, its heights were not)."""
    return particle_tracks(path)


def from_spawn(rows, y0):
    """The rows from the body's spawn pose on: a slot the last run's body held can open the log (readback is a frame late)."""
    k = next((i for i, r in enumerate(rows) if abs(r[1] - y0) < 0.05), None)
    return rows[k:] if k is not None else []


def body_stats(rows, a, level):
    entry = next((i for i, r in enumerate(rows) if r[1] - a < level), None)
    after = rows[entry:] if entry is not None else []
    tail = [r for r in rows if r[0] > rows[-1][0] - 2.0] if rows else []
    return {"spawn_y": rows[0][1] if rows else None, "entered": entry is not None,
            "peak_bottom_after_entry": max((r[1] - a for r in after), default=None),
            "peak_y_after_entry": max((r[1] for r in after), default=None),
            # at rest = how far y moves over the last 2 s (the log's prev pose is one solver TICK back, its dt one FRAME: a speed
            # from them read 3 m/s for a block lying still)
            "end_y": rows[-1][1] if rows else None, "y_range_last2s": (max(r[1] for r in tail) - min(r[1] for r in tail)) if tail else None}


def sample(api):
    c = api.debug("water_coupling", {})
    p, m = c.get("pressure_force", {}), c.get("moving_solids", {})
    return {"rasterized": m.get("bodies", 0), "refused_no_window": m.get("refused_no_window"), "wet": m.get("wet_bodies", 0),
            "asked": p.get("asked", 0), "windows": p.get("windows", 0), "over_budget": p.get("fallback_over_budget", 0),
            "knob": p.get("test_refuse_windows")}


def trace(api, seconds, log):
    rows, t0 = [], time.time()
    while time.time() - t0 < seconds:
        s = sample(api); s["t"] = round(time.time() - t0, 2)
        et = api.get("/api/debug/engine_timing"); s["fps"] = et.get("fps"); s["cpu_ms"] = et.get("cpuFrameTime")
        rows.append(s); time.sleep(0.1)
    api.debug("particle_log", {"action": "stop"})
    bodies = read_log(log); log.unlink(missing_ok=True)
    return rows, bodies


def knob(api, on):
    r = api.debug("water_coupling", {"test_refuse_windows": on}).get("pressure_force", {})
    if r.get("test_refuse_windows") is not on: raise SystemExit(f"test_refuse_windows not taken (stale binary?): {r}")


def drop(api, size, mat, refuse, seconds, tag):
    w.AUTO_SLEEP = False; w.PRESSURE = True; w.pond(api); time.sleep(3.0)
    knob(api, refuse)
    log = (w.EV / f"a2g_{tag}.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    a = size / 2
    y0 = w.LEVEL + a + 1.0 + 0.01
    r = api.debug("spawn_gpu_particle", {"x": w.CX, "y": y0, "z": w.CZ, "material": mat, "scale": size,
                                         "lifetime": seconds + 30.0, "spin": 0})
    if not r.get("success"): raise SystemExit(f"spawn: {r}")
    rows, bodies = trace(api, seconds, log)
    knob(api, False); api.debug("clear_dynamics")
    mine = [t for t in (from_spawn(v, y0) for v in bodies.values()) if t]
    if len(mine) != 1: raise SystemExit(f"expected one body from the spawn pose, found {len(mine)}")
    b = body_stats(mine[0], a, w.LEVEL)
    # the bob, per 2 s of log time (is it dying away?)
    t0 = mine[0][0][0]
    b["y_range_by_2s"] = [round(max(r[1] for r in seg) - min(r[1] for r in seg), 4)
                          for k in range(int((mine[0][-1][0] - t0) // 2))
                          if (seg := [r for r in mine[0] if t0 + 2 * k <= r[0] < t0 + 2 * k + 2])]
    out = {"size": size, "material": mat, "refuse": refuse, "body": b,
           "rasterized_max": max(s["rasterized"] for s in rows), "windows_max": max(s["windows"] for s in rows),
           "over_budget_max": max(s["over_budget"] for s in rows),
           "refused_no_window_max": max((s["refused_no_window"] or 0) for s in rows), "samples": rows[::5]}
    print(f"  {mat} {size:.3f} m knob {'ON ' if refuse else 'off'}: rasterized max {out['rasterized_max']} windows max {out['windows_max']}"
          f" over_budget max {out['over_budget_max']} refused_no_window max {out['refused_no_window_max']} | spawn y {b.get('spawn_y')}"
          f" peak bottom after entry {b.get('peak_bottom_after_entry')} end y {b.get('end_y')} y range (last 2 s) {b.get('y_range_last2s')}"
          f"\n    bob per 2 s: {b.get('y_range_by_2s')}", flush=True)
    return out


def stress(api, seconds, n_per, spots):
    api.debug("clear_dynamics")
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})
    box = {**PIT_BOX, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto", "auto_sleep": False}
    cr = api.debug("water_av_create", box)
    if "error" in cr and cr.get("recorded_m3"):   # the pit's last water is in the record: take it in, discard, start fresh
        cr = api.debug("water_av_create", {**box, "seed": "spans"}); api.debug("water_av_destroy", {"id": cr["volume"]["id"], "discard": True})
        cr = api.debug("water_av_create", box)
    if "error" in cr: raise SystemExit(f"pit: {cr}")
    api.debug("place_water_box", {**PIT_WATER, "mass": 1.0, "target": "core"})
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_coupling", {"enabled": True, "solids": True, "pressure_force": True, "displace": w.DISPLACE})
    knob(api, False); time.sleep(3.0)
    log = (w.EV / "a2g_stress.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log), "max": 2000})
    for (x, z) in spots:
        api.debug("spawn_gpu_particle", {"x": x, "y": PIT_LEVEL + 1.0, "z": z, "material": "Wood", "scale": 1 / 9, "count": n_per,
                                         "lifetime": seconds + 30.0, "spin": 0})
    rows, bodies = trace(api, seconds, log)
    api.debug("clear_dynamics")
    a = 1 / 18
    # 34: from the spawn pose, as the single drops - a slot the stone scene held opened the log at the stone's rest (15.166) and read
    # as a 1.83 m "rise" (17.0 - 15.166, exactly); a slot with no row at the spawn height is counted, not measured
    trimmed = {s: from_spawn(r, PIT_LEVEL + 1.0) for s, r in bodies.items()}
    unmatched = sum(1 for r in trimmed.values() if not r)
    st = {s: body_stats(r, a, PIT_LEVEL) for s, r in trimmed.items() if r}
    rise = [b["peak_y_after_entry"] - b["spawn_y"] for b in st.values() if b["entered"]]
    over = [s for s in rows if s["asked"] > 1024]
    # every rasterized body holds a window, at every sample (the invariant); refused_no_window counts only DISPLACING bodies refused,
    # so it is <= asks - windows, not equal
    exact = all(s["rasterized"] <= s["windows"] and (s["refused_no_window"] or 0) <= s["asked"] - s["windows"] for s in rows)
    out = {"bodies": len(st), "unmatched_slots": unmatched, "displace": w.DISPLACE, "entered": len(rise), "max_rise_above_spawn_m": max(rise) if rise else None,
           "samples_over_cap": len(over), "refused_exact": exact,
           "asked_max": max(s["asked"] for s in rows), "windows_max": max(s["windows"] for s in rows),
           "refused_no_window_max": max((s["refused_no_window"] or 0) for s in rows),
           "fps_min": min(s["fps"] for s in rows), "fps_mean": sum(s["fps"] for s in rows) / len(rows),
           "cpu_ms_mean": sum(s["cpu_ms"] for s in rows) / len(rows), "samples": rows[::5]}
    print(f"  stress (displace {'on' if w.DISPLACE else 'off'}): {out['bodies']} bodies ({out['entered']} entered, {unmatched} slots unmatched) max rise above spawn {out['max_rise_above_spawn_m']}"
          f" | asked max {out['asked_max']} windows max {out['windows_max']} refused max {out['refused_no_window_max']}"
          f" samples over the cap {out['samples_over_cap']} rasterized <= windows at every sample: {exact} | fps mean {out['fps_mean']:.0f} min {out['fps_min']:.0f}"
          f" CPU {out['cpu_ms_mean']:.2f} ms", flush=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--only", default="stone,control,refused,stress")
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument("--tag", default="now")
    ap.add_argument("--displace", default="on", choices=["on", "off"], help="debris displacement (34: shipped off; this tool's numbers were on)")
    a = ap.parse_args()
    w.DISPLACE = a.displace == "on"
    api = Api(a.url); api.wait_status(300)
    api.post("/api/camera", {"mode": "free", "position": {"x": 102.0, "y": 20.0, "z": 8.0}, "yaw": 90, "pitch": -40})
    only = a.only.split(","); out = {}
    path = w.EV / f"a2g_{a.tag}.json"
    if "stone" in only:
        print("stone (the red / the gate)", flush=True)
        out["stone"] = drop(api, 1 / 3, "Stone", True, a.seconds, "stone")
        s = out["stone"]
        # with displacement off (34) nothing is displacing, so nothing is refused for want of a window - the gate is then nothing
        # rasterized and no window, the A2g filter itself untested (it is tested with --displace on)
        s["gate_pass"] = s["rasterized_max"] == 0 and s["windows_max"] == 0 and (s["refused_no_window_max"] >= 1 or not w.DISPLACE)
        print(f"  GATE {'PASS' if s['gate_pass'] else 'FAIL'}", flush=True)
        path.write_text(json.dumps(out, indent=1))
    wood = [x for x in ("control", "refused") if x in only]
    if wood:
        if not out.get("stone", {}).get("gate_pass"): raise SystemExit("the wood scenes need the stone gate to pass in the same run (--only stone,...)")
        for sc in wood:
            print(sc, flush=True)
            out[sc] = [drop(api, size, "Wood", sc == "refused", a.seconds, f"{sc}_{i}") for i, size in enumerate((1 / 3, 1.0))]
            path.write_text(json.dumps(out, indent=1))
        if "control" in out and "refused" in out:
            for c, r in zip(out["control"], out["refused"]):
                cb, rb = c["body"]["peak_bottom_after_entry"], r["body"]["peak_bottom_after_entry"]
                r["pass"] = rb <= cb + 0.02 and r["body"]["y_range_last2s"] < 0.01
                print(f"  {r['size']:.3f} m: refused peak bottom {rb:.3f} vs control {cb:.3f} (+0.02) y range last 2 s {r['body']['y_range_last2s']:.4f} m"
                      f" -> {'PASS' if r['pass'] else 'FAIL'}", flush=True)
    if "stress" in only:
        if not out.get("stone", {}).get("gate_pass"): raise SystemExit("the stress needs the stone gate to pass in the same run")
        print("stress", flush=True)
        spots = [(151.0 + 1.6 * i, 11.0 + 1.6 * j) for j in range(3) for i in range(4)][:11]
        out["stress"] = stress(api, a.seconds, 100, spots)
    path.write_text(json.dumps(out, indent=1))
    api.debug("clear_dynamics")
    print("wrote", path)


if __name__ == "__main__":
    main()
