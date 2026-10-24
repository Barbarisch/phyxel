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
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
LEVEL = 16.5
CX, CZ = 102.0, 14.0           # the pond's centre (water x 100..104, z 12..16)
FAR = (100.3, 12.3)            # the level probe, ~2.4 m from the block
SCEN = [
    {"name": "control", "size": 0.0},
    {"name": "wood_third_gentle", "size": 1 / 3, "drop": 0.0, "spin": 0.0},
    {"name": "wood_third_drop1m", "size": 1 / 3, "drop": 1.0, "spin": 0.0},
    {"name": "wood_third_tumble", "size": 1 / 3, "drop": 1.0, "spin": 3.0},
    {"name": "wood_ninth_gentle", "size": 1 / 9, "drop": 0.0, "spin": 0.0},
    {"name": "wood_one_gentle", "size": 1.0, "drop": 0.0, "spin": 0.0},
]


def pond(api):
    api.debug("clear_dynamics")
    for v in api.debug("water_av_list").get("volumes", []):
        api.debug("water_av_destroy", {"id": v["id"], "discard": True})
    box = {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16, "cellSize": 1.0 / 9.0, "transport": "eulerian", "backend": "auto",
           # kept as a volume (the shipped default sleeps a still pond into the record and removes the volume): "asleep" is the
           # volume's own sleep test, read without acting on it
           "auto_sleep": False}
    cr = api.debug("water_av_create", box)
    if "error" in cr and cr.get("recorded_m3"):
        # the last run's pond slept into the record (auto_sleep, the shipped default): wake THAT water, add none
        cr = api.debug("water_av_create", {**box, "seed": "spans"})
        if "error" in cr: raise SystemExit(f"create (seeded): {cr}")
    elif "error" in cr: raise SystemExit(f"create: {cr}")
    else:
        api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
        api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
    time.sleep(1.0)
    m = api.debug("water_av_list").get("total_mass")
    if m is None or abs(m - 24.0) > 0.05: raise SystemExit(f"the pond holds {m} m^3, not 24")
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_coupling", {"enabled": True, "solids": True})
    return cr


def vol(api):
    v = api.debug("water_av_list").get("volumes", [])
    return v[0] if v else {}


def level(api):
    return api.debug("water_av_probe", {"x": FAR[0], "y": 16.0, "z": FAR[1]}).get("surface_y")


def read_log(path, a):
    """The engine's per-frame particle log -> [(t, y, speed, sleeping)] for the one block (slot of the first active particle)."""
    rows, t, dt = [], 0.0, 0.0
    prev = None
    for line in pathlib.Path(path).read_text().splitlines():
        if line.startswith("F,"):
            f = line.split(","); dt = float(f[2]); t += dt
        elif line.startswith("P,"):
            f = line.split(",")
            flags = int(f[9])    # P,frame,slot,x,y,z,px,py,pz,flags,material
            if not flags & 1: continue
            y, py = float(f[4]), float(f[7])
            speed = abs(y - py) / dt if dt > 0 else 0.0     # vertical speed from the particle's own previous pose
            rows.append((round(t, 3), y, speed, bool(flags & 2)))
    return rows


def run(api, sc, seconds):
    pond(api)
    t_settle = time.time()                        # let the fill settle before the block goes in (the control's clock too)
    while time.time() - t_settle < 8.0: time.sleep(0.5)
    a = sc["size"] / 2
    log = (EV / f"float_rest_{sc['name']}.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    if a > 0:
        y0 = LEVEL + a + sc["drop"] + 0.01
        r = api.debug("spawn_gpu_particle", {"x": CX, "y": y0, "z": CZ, "material": "Wood", "scale": sc["size"],
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
    api.debug("particle_log", {"action": "stop"})
    body = read_log(log, a) if a > 0 else []
    log.unlink(missing_ok=True)                   # the per-frame log is large; the JSON keeps every 10th row
    lv = LEVEL                                    # the pond's level: 24 m^3 over 16 m^2 above the floor at 15 (mass checked)
    tail = [b for b in body if b[0] > body[-1][0] - 5.0] if body else []
    drafts = [(lv - (b[1] - a)) / (2 * a) for b in tail]
    slept = next((b[0] for b in body if b[3]), None)
    out = {"scenario": sc, "pond_woke": woke, "pond_asleep_again_s": asleep_again, "pond_state_end": pond_trace[-1]["state"],
           "awake_columns_end": pond_trace[-1]["awake_columns"],
           "mass_min_max": [min(p["mass"] for p in pond_trace), max(p["mass"] for p in pond_trace)],
           "body_slept_at_s": slept, "body_y_end": body[-1][1] if body else None,
           "draft_last5s": [min(drafts), sum(drafts) / len(drafts), max(drafts)] if drafts else None,
           "speed_last5s_max": max((b[2] for b in tail), default=None),
           "body_y_max_after_entry": max((b[1] for b in body if b[0] > 0.3), default=None),
           "pond": pond_trace, "body": body[::10]}
    d = out["draft_last5s"]
    print(f"{sc['name']:20s} pond woke {woke} asleep again {asleep_again} end {out['pond_state_end']} awake_cols {out['awake_columns_end']}"
          f" mass {out['mass_min_max']} | body slept {slept} y_end {out['body_y_end']}"
          f" draft(last 5 s) {'-' if not d else f'{d[0]:.3f}/{d[1]:.3f}/{d[2]:.3f}'} speed max {out['speed_last5s_max']}"
          f" y_max {out['body_y_max_after_entry']}", flush=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--seconds", type=float, default=40.0)
    ap.add_argument("--only", default="")
    ap.add_argument("--tag", default="now")
    args = ap.parse_args()
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
