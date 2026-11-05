"""MatSim P1b/P1c live rig (docs/MaterialSimPlan.md 6a "Lab rig", 6c, 6d) - L4 on the running engine.

The rig is HAND-PLACED test geometry (/api/world/fill), not generator output: an 8-thick Stone slab under chunk (0,0,0)
(top solid y = 15) and, inside the 16 x 8 x 16 m lab box (x 8-23, y 16-23, z 8-23):
  - a separation wall at z = 16 (full height) splitting the box into the DAM half (z 8-15) and the U-TUBE half (z 17-23);
  - the dam: a wall at x = 11 (y 16-19), a 3 x 3 x 8 m reservoir behind it, removed live for the dam break;
  - a ledge: x 21-23, y 16-17, z 8-10 (the dam-break flood meets it);
  - the U-tube: a divider at x = 16 (y 17-23), so the arms (x 8-15 and 17-23) connect through a 1 m tall gap at y 16.
WaterCore and the old water are off (the MatSimLab project's game.json).

    python tools/matsim_lab.py --build        # author + save the rig once (engine running on the project, port 8120)
    python tools/matsim_lab.py --run [--pressure off]   # every measurement; writes docs/evidence/matsim_p1c_live[_nopressure].json
    python tools/matsim_lab.py --view [--cpm 3]   # the raw-cell view of a flowing box + a screenshot, left running

P1c predictions (6d, written before this run): with pressure on, the U-tube arms end within max(1/4 cell, 3 cm) of
each other and of the volume level (2.6 m above the floor) at every cell size, and the box comes to rest; the ledger is
exact at every sample; live parity (cells, marks, head field) is identical; with --pressure off the far arm stops at the
gap's height (the P1 result).
"""
import argparse, json, math, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

ROOT = pathlib.Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / "docs" / "evidence" / "matsim_p1c_live.json"
PRESSURE = True   # set from --pressure
BOX_MIN, BOX_SIZE = {"x": 8, "y": 16, "z": 8}, {"x": 16, "y": 8, "z": 16}
DAM = {"x1": 11, "y1": 16, "z1": 8, "x2": 11, "y2": 19, "z2": 15}
H0 = 3.0                      # reservoir depth (m)
RITTER = 2.0 * math.sqrt(9.81 * H0)
TICK = 1.0 / 60.0


def fill(api, box, mat="Stone"):
    return world_job(api, "/api/world/fill", box, material=mat)


def build(api):
    world_job(api, "/api/world/clear", {"x1": 0, "y1": 8, "z1": 0, "x2": 31, "y2": 31, "z2": 31})
    fill(api, {"x1": 0, "y1": 8, "z1": 0, "x2": 31, "y2": 15, "z2": 31})                 # slab, top solid y 15
    fill(api, {"x1": 8, "y1": 16, "z1": 16, "x2": 23, "y2": 23, "z2": 16})               # separation wall
    fill(api, DAM)                                                                       # the dam
    fill(api, {"x1": 21, "y1": 16, "z1": 8, "x2": 23, "y2": 17, "z2": 10})               # the ledge
    fill(api, {"x1": 16, "y1": 17, "z1": 17, "x2": 16, "y2": 23, "z2": 23})              # the U-tube divider
    print("rig built (hand-placed); save:", str(api.post("/api/world/save", {}))[:120])


def ms(api, action, body=None, timeout=600):
    r = api.post(f"/api/matsim/{action}", body or {}, timeout=timeout)
    return r


def must(r, what):
    if "error" in r:
        raise SystemExit(f"{what}: {r['error']}")
    return r


def ledger_ok(r):
    return bool(r.get("ledger") and r["ledger"].get("exact"))


def slices(api, axis, mn=None, size=None):
    body = {"axis": axis}
    if mn: body["min"], body["size"] = mn, size
    r = must(ms(api, "slices", body), "slices")
    total = None
    for mat, arr in r["m3"].items():
        total = arr if total is None else [a + b for a, b in zip(total, arr)]
    n = int(round((size or BOX_SIZE)[axis] / r["slice_m"]))
    return r["first_slice_m"], r["slice_m"], total or [0.0] * n, r


def wait_occupancy():
    time.sleep(2.0)   # the occupancy pool repacks on the main thread at the next frames


def run_size(api, cpm, ev):
    out = {"cells_per_m": cpm}
    fill(api, DAM)          # the dam back up (a previous run removed it)
    wait_occupancy()
    r = must(ms(api, "create", {"min": BOX_MIN, "size": BOX_SIZE, "cells_per_m": cpm, "backend": "gpu", "pressure": PRESSURE}), f"create cpm {cpm}")
    out["backend"], out["cells"] = r["backend"], r["box"]["cell_count"]
    st = ms(api, "stats")
    out["unknown_cells_at_create"] = st["unknown_cells"]
    must(ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 8}, "size": {"x": 3, "y": 3, "z": 8}, "fraction": 1.0}), "reservoir")
    r = must(ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 17}, "size": {"x": 8, "y": 5, "z": 7}, "fraction": 1.0}), "U-tube arm")
    m3_0 = sum(r["ledger"]["units"].values()) / r["ledger"]["full_units"] / cpm ** 3
    out["water_m3"] = m3_0
    # The wall holds: 30 ticks with the dam in place, nothing beyond x = 11.
    must(ms(api, "step", {"n": 30}), "step")
    x0, h, tot, _ = slices(api, "x", {"x": 8, "y": 16, "z": 8}, {"x": 16, "y": 8, "z": 8})
    leaked = sum(v for i, v in enumerate(tot) if x0 + i * h >= 12.0)
    out["dam_holds_m3_past_wall"] = leaked
    ledgers_exact = True
    # Dam break: remove the wall in the WORLD, then resolidify the box from the occupancy pool.
    world_job(api, "/api/world/clear", DAM)
    wait_occupancy()
    r = must(ms(api, "resolidify"), "resolidify")
    out["after_break"] = {"unknown_cells": r["unknown_cells"], "trapped_units": r["trapped_units"]}
    ledgers_exact &= ledger_ok(r)
    front = []   # (t s, front x m)
    t = 0
    step = max(1, cpm)   # sample every 1/3/9 ticks
    while t < 240:
        r = must(ms(api, "step", {"n": step}), "step")
        ledgers_exact &= ledger_ok(r)
        t += step
        x0, h, tot, _ = slices(api, "x", {"x": 8, "y": 16, "z": 8}, {"x": 16, "y": 8, "z": 8})
        thr = 0.01 * 8 * h   # 1 % of a full one-cell-thick slice of the 8 m wide channel
        fx = max((x0 + (i + 1) * h for i, v in enumerate(tot) if v > thr), default=x0)
        front.append((t * TICK, fx))
        if fx >= 20.0: break
    # Front speed: the front's travel from x 12 to its last sample before the ledge, by time.
    a = next((p for p in front if p[1] >= 12.5), None)
    b = next((p for p in reversed(front) if p[1] <= 20.0), None)
    out["dam_break_front"] = front
    out["front_speed_mps"] = (b[1] - a[1]) / (b[0] - a[0]) if a and b and b[0] > a[0] else None
    out["ritter_mps"] = RITTER
    out["front_cap_mps"] = 60.0 / cpm
    # U-tube: arm water heights above the floor (y 16), sampled each second; the volume level is
    # (arm water - the gap cell under the divider) / (both arm areas).
    def arms():
        x0, h, tot, _ = slices(api, "x", {"x": 8, "y": 16, "z": 17}, {"x": 16, "y": 8, "z": 7})
        left = sum(v for i, v in enumerate(tot) if x0 + i * h < 16.0) / (8 * 7)
        gap = sum(v for i, v in enumerate(tot) if 16.0 <= x0 + i * h < 17.0)
        right = sum(v for i, v in enumerate(tot) if x0 + i * h >= 17.0) / (7 * 7)
        return left, right, gap
    lv = []
    level_at = None
    tol = max(0.25 / cpm, 0.03)
    # Rest: quiet (nothing moved, no head changed) on two consecutive ticks (6d), up to 300 s total.
    asleep_at, prev = None, False
    while t < 300 * 60 and asleep_at is None:
        left, right, gap = arms()
        lv.append((t * TICK, left, right))
        if level_at is None and abs(left - right) <= tol: level_at = t * TICK
        r = must(ms(api, "step", {"n": 58}), "step"); t += 58
        ledgers_exact &= ledger_ok(r)
        r1 = must(ms(api, "step", {"n": 1}), "step")
        r2 = must(ms(api, "step", {"n": 1}), "step"); t += 2
        ledgers_exact &= ledger_ok(r2)
        if r1.get("quiet") and r2.get("quiet"): asleep_at = t * TICK
    left, right, gap = arms()
    lv.append((t * TICK, left, right))
    out["u_tube_levels"] = lv[::max(1, len(lv) // 40)]
    out["u_tube_level_within_tol_s"] = level_at
    out["u_tube_tolerance_m"] = tol
    out["u_tube_final_m"] = {"left": left, "right": right, "gap_m3": gap,
                             "volume_level": (left * 56 + right * 49) / 105.0}
    out["asleep_at_s"] = asleep_at
    r = must(ms(api, "step", {"n": 60}), "step")
    out["asleep_gpu_ms_per_tick"], out["asleep_evaluated_blocks"] = r["gpu_ms_per_tick"], r["evaluated_blocks"]
    ledgers_exact &= ledger_ok(r)
    out["ledger_exact_at_every_sample"] = ledgers_exact
    out["final_ledger"] = r["ledger"]
    # Live parity on a fresh awake box (the dam is open now): CPU reference vs device, cell by cell.
    must(ms(api, "create", {"min": BOX_MIN, "size": BOX_SIZE, "cells_per_m": cpm, "backend": "gpu", "pressure": PRESSURE}), "create (parity)")
    must(ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 8}, "size": {"x": 3, "y": 3, "z": 8}, "fraction": 1.0}), "fill")
    must(ms(api, "fill_box", {"material": "Sand", "min": {"x": 21, "y": 18, "z": 8}, "size": {"x": 1, "y": 3, "z": 1}, "fraction": 1.0}), "fill")
    must(ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 17}, "size": {"x": 8, "y": 5, "z": 7}, "fraction": 0.7}), "fill")
    # The CPU reference steps on the engine's main thread (Debug): 1.49 M cells x 300 ticks outlasts the API's wait, so 1/9 m
    # checks 30 ticks live (the integration test covers 300 ticks of the same box size).
    # 6d: with the head pass the Debug CPU reference is slower still, so the same coverage runs as consecutive
    # shorter parity calls (each continues from the last; every one must be identical).
    n_par, chunk = (300, 50) if cpm < 9 else (30, 5)
    calls = []
    for _ in range(n_par // chunk):
        r = must(ms(api, "parity", {"n": chunk}), "parity")
        calls.append({k: r.get(k) for k in ("identical", "differing_cells", "first_difference", "marks_identical", "heads_identical",
                                             "eligible_cpu_gpu", "evaluated_cpu_gpu", "head_changes_cpu_gpu")})
    out["live_parity_ticks"] = n_par
    out["live_parity_chunk"] = chunk
    out["live_parity"] = {"identical": all(c["identical"] for c in calls), "calls": calls}
    r = must(ms(api, "step", {"n": 60}), "step")
    out["awake_gpu_ms_per_tick"], out["awake_wall_ms_per_tick"] = r["gpu_ms_per_tick"], r["wall_ms_per_tick"]
    out["awake_evaluated_blocks_per_tick"] = r["evaluated_blocks"] / 60
    out["u_tube_first_last"] = [lv[0], lv[-1]] if lv else None
    ev["sizes"].append(out)
    EVIDENCE.parent.mkdir(parents=True, exist_ok=True)
    EVIDENCE.write_text(json.dumps(ev, indent=1))   # after every size, so a later failure keeps what was measured
    print(json.dumps({k: v for k, v in out.items() if k not in ("dam_break_front", "u_tube_levels", "final_ledger")}, indent=1))


def refusals(api, ev):
    cases = {
        "unknown_space": ("create", {"min": {"x": 8, "y": 600, "z": 8}, "size": {"x": 4, "y": 4, "z": 4}, "cells_per_m": 3}),
        "bad_cell_size": ("create", {"min": BOX_MIN, "size": BOX_SIZE, "cells_per_m": 2}),
        "too_many_cells": ("create", {"min": {"x": 0, "y": 0, "z": 0}, "size": {"x": 300, "y": 300, "z": 300}, "cells_per_m": 1}),
        "off_lattice": ("create", {"min": {"x": 8.5, "y": 16, "z": 8}, "size": BOX_SIZE, "cells_per_m": 1}),
    }
    must(ms(api, "create", {"min": BOX_MIN, "size": BOX_SIZE, "cells_per_m": 3}), "create")
    before = ms(api, "stats")
    cases.update({
        "unknown_material": ("fill_box", {"material": "Lava", "min": BOX_MIN, "size": {"x": 1, "y": 1, "z": 1}, "fraction": 1}),
        "fraction_over_1": ("fill_box", {"material": "Water", "min": BOX_MIN, "size": {"x": 1, "y": 1, "z": 1}, "fraction": 2}),
        "step_too_many": ("step", {"n": 100001}),
        "step_negative": ("step", {"n": -1}),
        "bad_type": ("step", {"n": "ten"}),
        "unknown_action": ("explode", {}),
    })
    res = {}
    for name, (action, body) in cases.items():
        r = ms(api, action, body)
        res[name] = r.get("error")
        print(f"  {name}: {r.get('error')}")
    after = ms(api, "stats")
    res["box_unchanged_by_refusals"] = before["box"] == after["box"] and before["ledger"] == after["ledger"] and before["tick"] == after["tick"]
    res["all_refused"] = all(v for k, v in res.items() if k != "box_unchanged_by_refusals")
    ev["refusals"] = res


def view(api, cpm, tag):
    fill(api, DAM); wait_occupancy()
    must(ms(api, "create", {"min": BOX_MIN, "size": BOX_SIZE, "cells_per_m": cpm}), "create")
    must(ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 8}, "size": {"x": 3, "y": 3, "z": 8}, "fraction": 1.0}), "fill")
    must(ms(api, "fill_box", {"material": "Sand", "min": {"x": 21, "y": 18, "z": 8}, "size": {"x": 1, "y": 3, "z": 2}, "fraction": 1.0}), "fill")
    must(ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 17}, "size": {"x": 8, "y": 5, "z": 7}, "fraction": 1.0}), "fill")
    world_job(api, "/api/world/clear", DAM); wait_occupancy()
    must(ms(api, "resolidify"), "resolidify")
    pose = {"x": 16.0, "y": 34.0, "z": -6.0, "yaw": 90.0, "pitch": -45.0}
    got = camera_set(api, pose)
    must(ms(api, "view", {"on": True}), "view")
    must(ms(api, "realtime", {"on": True}), "realtime")
    time.sleep(1.5)
    shot = api.get("/api/screenshot", timeout=30)
    st = ms(api, "stats")
    print(f"[{tag}] cpm {cpm}: screenshot {shot.get('path')} tick {st['tick']} faces {st['view_faces']} cut {st['view_faces_cut']} "
          f"dropped {st['realtime_ticks_dropped']} camera {got}")
    return {"cpm": cpm, "screenshot": shot.get("path"), "pose_asked": pose, "pose_got": got, "stats": {k: st[k] for k in ("tick", "view_faces", "view_faces_cut", "realtime_ticks_dropped", "gpu_ms_per_tick")}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8120")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--view", action="store_true")
    ap.add_argument("--cpm", type=int, default=3)
    ap.add_argument("--sizes", default="3,1,9")
    ap.add_argument("--pressure", choices=("on", "off"), default="on")
    a = ap.parse_args()
    global PRESSURE, EVIDENCE
    PRESSURE = a.pressure == "on"
    if not PRESSURE: EVIDENCE = EVIDENCE.with_name("matsim_p1c_live_nopressure.json")
    api = Api(a.url)
    api.wait_status(120)
    if a.build: build(api)
    if a.run:
        ev = {"rig": "hand-placed (tools/matsim_lab.py --build), MatSimLab project, chunk (0,0,0)", "sizes": [],
              "pressure": PRESSURE,
              "predictions": {"ledger": "exact at every size",
                              "u_tube": ("arms within max(1/4 cell, 3 cm) of each other and of the volume level, at rest"
                                         if PRESSURE else "far arm stops at the gap height (P1)"),
                              "parity": "identical incl. the head field", "unknown": "refused"}}
        st0 = ms(api, "stats")
        ev["inert_before_create"] = {k: st0.get(k) for k in ("created", "tick", "view_faces")}
        for cpm in [int(s) for s in a.sizes.split(",")]:
            run_size(api, cpm, ev)
        refusals(api, ev)
        ms(api, "clear")
        fill(api, DAM)
        EVIDENCE.parent.mkdir(parents=True, exist_ok=True)
        EVIDENCE.write_text(json.dumps(ev, indent=1))
        print("evidence:", EVIDENCE)
    if a.view:
        r = view(api, a.cpm, "view")
        print(json.dumps(r, indent=1))


if __name__ == "__main__":
    main()
