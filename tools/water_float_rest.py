"""WaterCore 31 step 0: how a floating block rests on today's code - its draft and whether the pond settles.

On the Small bench (port 8111), a fresh 4 x 4 m pond 1.5 m deep (floor top y 15, surface 16.5) in a 1/9 m volume. Per scenario
one wood block (buoyancy 1.429 -> the physical draft 1 / 1.429 = 0.70 of its height) is released and traced for --seconds:
its centre and speed (water_coupling wet_body_list), the pond's level away from it, whether the pond falls asleep
(water_av_list asleep) and how many columns stay awake. Control: the same pond with no block.

Draft = (level - (centre_y - a)) / (2 a), a = the block's half size (unrotated: spin 0 starts it axis-aligned and still).

Predictions written before the run (2026-10-24):
  - the control pond falls asleep;
  - the table law's own equilibrium is f b = 1 -> draft 0.70 for an unrotated block; the 2026-10-09 run (1/3 m cells, tumbling)
    measured wood higher than that (centre 16.45-16.57 vs 16.43);
  - a pond with a floating block does not fall asleep (WaterCore 0a: "a floating block's pond never sleeps").

    python tools/water_float_rest.py [--url http://127.0.0.1:8111] [--seconds 40] [--only NAME] [--tag now]
Writes docs/evidence/water_core_m4/float_rest_<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, particle_tracks

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
LEVEL = 16.5
SETTLE = 8.0
AUTO_SLEEP = False             # --auto-sleep: the shipped default (a still pond sleeps into the record; the volume goes)
DISPLACE = True                # --no-displace (TEST): bodies recorded but not rasterized (WaterCore 31: the pump's share)
PRESSURE = True                # the water's push, always (owner 2026-10-25: the old floating rule launched blocks; it is never run)
CX, CZ = 102.0, 14.0           # the pond's centre (water x 100..104, z 12..16)
FAR = (100.3, 12.3)            # the level probe, ~2.4 m from the block
SCEN = [
    {"name": "control", "size": 0.0},
    {"name": "wood_third_gentle", "size": 1 / 3, "drop": 0.0, "spin": 0.0},
    {"name": "wood_third_drop1m", "size": 1 / 3, "drop": 1.0, "spin": 0.0},
    {"name": "wood_third_tumble", "size": 1 / 3, "drop": 1.0, "spin": 3.0},
    {"name": "wood_ninth_gentle", "size": 1 / 9, "drop": 0.0, "spin": 0.0},
    {"name": "wood_one_gentle", "size": 1.0, "drop": 0.0, "spin": 0.0},
    # a sinker: must reach the floor (centre 15 + 1/6) and fall asleep there (31.8's "no sleep off balance" is for floaters only)
    {"name": "stone_third_drop1m", "size": 1 / 3, "drop": 1.0, "spin": 0.0, "material": "Stone"},
]


def pond(api):
    api.debug("clear_dynamics")
    for v in api.debug("water_av_list").get("volumes", []):
        api.debug("water_av_destroy", {"id": v["id"], "discard": True})
    box = {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto",
           # kept as a volume (the shipped default sleeps a still pond into the record and removes the volume): "asleep" is the
           # volume's own sleep test, read without acting on it
           "auto_sleep": AUTO_SLEEP}
    cr = api.debug("water_av_create", box)
    if "error" in cr and cr.get("recorded_m3") and AUTO_SLEEP:
        # the hand-over run needs a pond AWAKE when the block lands: take the record into a volume, discard it, fill afresh
        cr = api.debug("water_av_create", {**box, "seed": "spans", "auto_sleep": False})
        if "error" in cr: raise SystemExit(f"create (seeded, to purge): {cr}")
        api.debug("water_av_destroy", {"id": cr["volume"]["id"], "discard": True})
        cr = api.debug("water_av_create", box)
        if "error" in cr: raise SystemExit(f"create after the purge: {cr}")
        api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
        api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
    elif "error" in cr and cr.get("recorded_m3"):
        # the last run's pond slept into the record (auto_sleep, the shipped default): wake THAT water, add none
        cr = api.debug("water_av_create", {**box, "seed": "spans"})
        if "error" in cr: raise SystemExit(f"create (seeded): {cr}")
    elif "error" in cr: raise SystemExit(f"create: {cr}")
    else:
        api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
        api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
    time.sleep(1.0)
    m = api.debug("water_av_list").get("total_mass")
    if not AUTO_SLEEP and (m is None or abs(m - 24.0) > 0.05): raise SystemExit(f"the pond holds {m} m^3, not 24")
    api.debug("water_av_realtime", {"on": True})
    pc = api.debug("water_coupling", {"enabled": True, "solids": True, "pressure_force": PRESSURE, "displace": DISPLACE})
    if pc.get("pressure_force", {}).get("on") != PRESSURE: raise SystemExit(f"pressure_force not taken (stale binary?): {pc.get('pressure_force')}")
    return cr


def vol(api):
    v = api.debug("water_av_list").get("volumes", [])
    return v[0] if v else {}


def level(api):
    return api.debug("water_av_probe", {"x": FAR[0], "y": 16.0, "z": FAR[1]}).get("surface_y")


def read_log(path, a):
    """The engine's particle log -> [(t, y, speed, sleeping)] for the one block: the longest track (a slot the last run's body held
    can open the log with a row or two). t on the solver's clock from the first row (water_bench.particle_tracks, WaterCore A2j:
    before it, t was the frame dt summed - an undercount, so every time and speed this tool reported was wrong; heights were not)."""
    tracks = particle_tracks(path)
    if not tracks: return []
    tr = max(tracks.values(), key=len)
    t0 = tr[0][0]
    return [(round(t - t0, 4), y, abs(vy), sl) for t, y, vy, sl in tr]


def after_dive(body, a):
    """The highest centre after the dive: rows after entry (bottom below the level), from the lowest point of their first 2 s on."""
    e = next((i for i, b in enumerate(body) if b[1] - a < LEVEL), None)
    if e is None: return None
    win = [i for i in range(e, len(body)) if body[i][0] <= body[e][0] + 2.0]
    k = min(win, key=lambda i: body[i][1])
    return max(b[1] for b in body[k:])


def run(api, sc, seconds):
    pond(api)
    t_settle = time.time()                        # let the fill settle before the block goes in (the control's clock too)
    while time.time() - t_settle < SETTLE: time.sleep(0.5)
    a = sc["size"] / 2
    log = (EV / f"float_rest_{sc['name']}.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    if a > 0:
        y0 = LEVEL + a + sc["drop"] + 0.01
        r = api.debug("spawn_gpu_particle", {"x": CX, "y": y0, "z": CZ, "material": sc.get("material", "Wood"), "scale": sc["size"],
                                             "lifetime": seconds + 30.0, "spin": sc["spin"]})
        if not r.get("success"): raise SystemExit(f"spawn: {r}")
        if abs(r.get("spin", -1) - sc["spin"]) > 1e-6: raise SystemExit(f"spawn ignored spin (stale binary?): {r}")
    t0, pond_trace, woke, asleep_again = time.time(), [], False, None
    while time.time() - t0 < seconds:
        t = time.time() - t0
        v = vol(api)
        state = "recorded" if not v else ("asleep" if v.get("asleep") else "awake")
        pond_trace.append({"t": round(t, 2), "state": state, "awake_columns": v.get("awake_columns"), "level": level(api),
                           "mass": api.debug("water_av_list").get("total_mass")})
        if state == "awake": woke = True
        elif woke and asleep_again is None: asleep_again = round(t, 2)
        time.sleep(0.25)
    press = api.debug("water_coupling", {}).get("pressure_force")
    api.debug("particle_log", {"action": "stop"})
    body = read_log(log, a) if a > 0 else []
    log.unlink(missing_ok=True)                   # the per-frame log is large; the JSON keeps every 10th row
    lv = LEVEL                                    # the pond's level: 24 m^3 over 16 m^2 above the floor at 15 (mass checked)
    tail = [b for b in body if b[0] > body[-1][0] - 5.0] if body else []
    drafts = [(lv - (b[1] - a)) / (2 * a) for b in tail]
    slept = next((b[0] for b in body if b[3]), None)
    rec_t = next((p["t"] for p in pond_trace if p["state"] == "recorded"), None)
    handover = None
    if rec_t is not None and body:
        before = [b[1] for b in body if rec_t - 1.0 <= b[0] < rec_t]
        after = [b[1] for b in body if rec_t + 0.5 <= b[0] < rec_t + 3.0]
        if before and after: handover = {"t": rec_t, "y_before": before[-1], "y_after_min": min(after), "y_after_max": max(after),
                                          "jump_mm": 1000.0 * max(abs(min(after) - before[-1]), abs(max(after) - before[-1]))}
    out = {"scenario": sc, "pond_woke": woke, "pond_asleep_again_s": asleep_again, "pond_state_end": pond_trace[-1]["state"],
           "awake_columns_end": pond_trace[-1]["awake_columns"],
           "mass_min_max": [min(p["mass"] for p in pond_trace), max(p["mass"] for p in pond_trace)],
           "body_slept_at_s": slept, "body_y_end": body[-1][1] if body else None,
           "draft_last5s": [min(drafts), sum(drafts) / len(drafts), max(drafts)] if drafts else None,
           "speed_last5s_max": max((b[2] for b in tail), default=None),
           # A2j: after ENTRY (the first row whose bottom is below the level). It was "after 0.3 s" on the old clock, which ran ~3x fast:
           # 0.3 s there was ~0.9 s of solver time, after entry; on the true clock 0.3 s is still above the water (a 1 m drop takes 0.45 s)
           "body_y_max_after_entry": max((b[1] for b in body[next((i for i, b in enumerate(body) if b[1] - a < LEVEL), len(body)):]), default=None),
           # the REBOUND: the highest point after the dive (the lowest point in the 2 s after entry) - what "nothing rises" is judged on
           "body_y_max_after_dive": after_dive(body, a),
           "handover": handover, "pressure_force": press, "pond": pond_trace, "body": body[::10]}
    d = out["draft_last5s"]
    print(f"{sc['name']:20s} pond woke {woke} asleep again {asleep_again} end {out['pond_state_end']} awake_cols {out['awake_columns_end']}"
          f" mass {out['mass_min_max']} | body slept {slept} y_end {out['body_y_end']}"
          f" draft(last 5 s) {'-' if not d else f'{d[0]:.3f}/{d[1]:.3f}/{d[2]:.3f}'} speed max {out['speed_last5s_max']}"
          f" y_max {out['body_y_max_after_entry']} after the dive {out['body_y_max_after_dive']}", flush=True)
    if press and press.get("on"): print(f"    pressure: {press}", flush=True)
    if handover: print(f"    hand-over to the record at {handover['t']} s: y {handover['y_before']:.4f} -> {handover['y_after_min']:.4f}..{handover['y_after_max']:.4f} ({handover['jump_mm']:.1f} mm)", flush=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--seconds", type=float, default=40.0)
    ap.add_argument("--only", default="")
    ap.add_argument("--tag", default="now")
    ap.add_argument("--pressure", action="store_true", help="kept for old command lines: the push is always on (the off rule is refused)")
    ap.add_argument("--no-displace", action="store_true", help="TEST: wet bodies never rasterized (the pump's pressure out)")
    ap.add_argument("--auto-sleep", action="store_true", help="the shipped default: the pond sleeps into the record (31.6 item 7: the hand-over)")
    ap.add_argument("--settle", type=float, default=8.0, help="seconds the fill settles before the block goes in")
    args = ap.parse_args()
    ap2 = args
    global PRESSURE, DISPLACE, AUTO_SLEEP; PRESSURE = True; DISPLACE = not args.no_displace; AUTO_SLEEP = args.auto_sleep
    global SETTLE; SETTLE = args.settle
    api = Api(args.url); st = api.wait_status(900)
    EV.mkdir(parents=True, exist_ok=True)
    res = {"status": {k: st.get(k) for k in ("build_config", "version", "git") if k in st}, "runs": []}
    for sc in SCEN:
        if args.only and args.only not in sc["name"]: continue
        res["runs"].append(run(api, sc, args.seconds))
    (EV / f"float_rest_{args.tag}.json").write_text(json.dumps(res, indent=1))
    api.debug("clear_dynamics")


if __name__ == "__main__":
    main()
