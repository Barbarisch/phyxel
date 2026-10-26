"""WaterCore A2j - the particle log's clock, live: one stone released at rest in open air, read three ways from ONE log.

  old   - the frame header's dt summed (what water_float_rest / water_a2g_safe used before A2j)
  age   - the body's spawn age, flags bits 16..23 (exact while under 255 ticks; what water_a2e_dryroom used in 37)
  snap  - the header's snap_ticks (new in A2j; water_bench.particle_tracks)

Prediction, written before the run (2026-10-25): old ~10 g (37's finding); age and snap agree tick for tick and give g within
1 % over the fall; vy at the last free-fall row = g t within 2 %. The stone falls from y 25.0 onto open ground (the Small bench,
x 120.5 z 30.5, ground top y 17 - a sinker, nothing rises).

    python tools/water_a2j_clock.py [--url http://127.0.0.1:8111] [--tag a2j]
Writes docs/evidence/water_core_m4/a2j_clock_<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, SOLVER_TICK, particle_tracks

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
G = 9.81
X, Y0, Z = 120.5, 25.0, 30.5


def accel(rows):
    """g from the fall: 2 (y0 - y) / t^2 at the last row still in free fall (above y 18.5: the ground top there is y 17)."""
    free = [r for r in rows if r[1] > 18.5]
    (t0, y0), (t, y) = free[0][:2], free[-1][:2]
    return 2 * (y0 - y) / (t - t0) ** 2 if t > t0 else None, round(t - t0, 4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--tag", default="a2j")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    api.debug("clear_dynamics"); time.sleep(0.5)
    log = (EV / f"a2j_clock_{a.tag}.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    r = api.debug("spawn_gpu_particle", {"x": X, "y": Y0, "z": Z, "material": "Stone", "scale": 1 / 3, "lifetime": 10, "spin": 0})
    if not r.get("success"): raise SystemExit(f"spawn: {r}")
    time.sleep(3.0)
    api.debug("particle_log", {"action": "stop"}); api.debug("clear_dynamics")
    text = log.read_text().splitlines()
    # old: dt summed; age: flags >> 16 & 0xFF; both for the row whose body starts at Y0
    old, age, t = [], [], 0.0
    for line in text:
        f = line.split(",")
        if f[0] == "F": t += float(f[2])
        elif f[0] == "P" and int(f[9]) & 1 and (old or abs(float(f[4]) - Y0) < 0.01):
            old.append((t, float(f[4]))); age.append((((int(f[9]) >> 16) & 0xFF) * SOLVER_TICK, float(f[4])))
    snap = next((tr for tr in particle_tracks(log).values() if abs(tr[0][1] - Y0) < 0.01), [])
    log.unlink(missing_ok=True)
    if not snap: raise SystemExit("no track for the released stone")
    out = {"tag": a.tag}
    for name, rows in (("old_dt_summed", old), ("spawn_age", age), ("snap_ticks", snap)):
        g, span = accel(rows)
        out[name] = {"rows": len(rows), "g_measured": round(g, 4) if g else None, "g_ratio": round(g / G, 4) if g else None, "span_s": span}
    # The clock checks (independent of the physics - the solver damps a falling body every tick, solver_integrate.comp
    # `mat.linearDamp * GAMMA`, so a long fall is NOT g; first run: 0.90 g over 1.2 s on every clock but the old one):
    # (1) age vs snap, per snapshot: the age (exact, < 255) and snap_ticks give the same elapsed time for the same pose
    # (only free-fall rows: at rest many rows share one height, and a lookup by height then pairs the wrong times)
    by_y = {}
    for t_, y in age: by_y.setdefault(round(y, 5), t_)
    pairs = [((s[0] - snap[0][0]), by_y[round(s[1], 5)] - age[0][0]) for s in snap
             if s[1] > 18.5 and round(s[1], 5) in by_y and by_y[round(s[1], 5)] < 254 * SOLVER_TICK]
    out["age_vs_snap"] = {"pairs": len(pairs), "max_diff_s": round(max(abs(a_ - b_) for a_, b_ in pairs), 6) if pairs else None}
    # (2) the logged vy of the step ending at a row (its pose minus the pose one tick before, x 60) against the finite difference
    # over the new clock, for rows exactly one tick apart (semi-implicit Euler: the two are the same number)
    fd = [((r1[1] - r0[1]) / (r1[0] - r0[0]), r1[2]) for r0, r1 in zip(snap, snap[1:])
          if r1[1] > 18.5 and abs((r1[0] - r0[0]) - SOLVER_TICK) < 1e-9]
    out["vy_vs_finite_difference"] = {"pairs": len(fd), "max_abs_diff_ms": round(max(abs(f_ - v_) for f_, v_ in fd), 5) if fd else None}
    # (3) g from the per-tick second difference over the first 10 ticks (the damping has had no time to act): dv per tick / dt
    early = [r for r in snap if r[0] - snap[0][0] <= 10 * SOLVER_TICK + 1e-9]
    dv = [(r1[2] - r0[2]) / (r1[0] - r0[0]) for r0, r1 in zip(early, early[1:]) if r1[0] > r0[0]]
    if dv:
        dv.sort()
        out["g_first_10_ticks"] = {"steps": len(dv), "g_ratio_median": round(-dv[len(dv) // 2] / G, 4)}
    last_free = [r for r in snap if r[1] > 18.5][-1]
    out["vy_at_last_free_row"] = {"vy": round(last_free[2], 4), "g_t_undamped": round(-G * (last_free[0] - snap[0][0]), 4)}
    print(json.dumps(out, indent=1))
    (EV / f"a2j_clock_{a.tag}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
