"""WaterCore.md 26.31 - (c), the partial wake: how much of a pond is MOVING after a stone, over time - measured before building.

A disturbance wakes the whole volume today; every pass of every step covers every stored brick. A partial wake would step only
the brick columns (8 x 8 cells, 0.89 m square at 1/9 m) that move, plus a margin. This reads the GPU's own per-unit rest partials
(water_av_units: the reductions the volume's sleep test sums - KE and max |df| per 8 x 2 x 8 cells) after a STONE (as
water_pond_ladder.py) and reports, every sample:
  moving     the wet columns whose own test fails: KE / mass >= ke_wake (1e-6) or max |df| >= df_quiet (1e-5) - the volume's
             sleep test, applied per column
  margin1/2  those columns dilated by 1 / 2 columns (Chebyshev) - what would be stepped
  share      of the wet columns; cells_share = the same weighted by the water held (the solver's cost goes with stored cells)
  solver_ms  the whole volume's device time (what it costs today)
Thresholds x 0.1 and x 10 are reported too (how sensitive the share is to the test).

    python tools/water_wake_map.py [--size 32x28] [--depth 1] [--at centre|corner] [--seconds 15] [--tag c_32x28]
Writes docs/evidence/water_core_h1/wake_map_<tag>.json.
"""
import argparse, json, pathlib, sys, time
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
KE_WAKE, DF_QUIET = 1e-6, 1e-5   # SolverParams keWake / maxDeltaFQuiet (WaterCore.h)


def dilate(m, k):
    out = m.copy()
    for _ in range(k):
        p = np.pad(out, 1)
        out = np.zeros_like(out)
        for dz in (-1, 0, 1):
            for dx in (-1, 0, 1):
                out |= p[1 + dz:1 + dz + m.shape[0], 1 + dx:1 + dx + m.shape[1]]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--size", default="32x28")
    ap.add_argument("--depth", type=int, default=1)
    ap.add_argument("--at", choices=["centre", "corner"], default="centre")
    ap.add_argument("--strength", type=float, default=4.0)
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument("--tag", default=None)
    ap.add_argument("--partial", choices=["on", "off"], default="off", help="26.31: the partial wake on the volume (on = frozen columns are not stepped)")
    ap.add_argument("--quiet-scale", type=float, default=3.0, help="26.34: the per-column freeze test's thresholds x this (water_av_partial_wake {quiet_scale})")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(120)
    api.debug("water_av_realtime", {"on": True})
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    l, w = (int(v) for v in args.size.split("x"))
    box = {"x1": 2, "y1": 16, "z1": 0, "x2": 2 + l - 1, "y2": 16 + args.depth, "z2": w - 1}
    api.debug("water_av_partial_wake", {"quiet_scale": args.quiet_scale})   # 26.34
    vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False, "partial_wake": args.partial == "on"})["volume"]["id"]
    api.debug("place_water_box", {"target": "core", **{**box, "y2": 16 + args.depth - 1}, "mass": 1.0})
    api.debug("water_av_units", {"capture": True})
    time.sleep(4.0)
    cx, cz = (2 + l / 2, w / 2) if args.at == "centre" else (2 + 2.0, 2.0)
    vol = lambda: next(v for v in api.debug("water_av_list")["volumes"] if v["id"] == vid)
    pre = vol()
    api.debug("water_av_impulse", {"x": cx, "y": 16 + args.depth - 0.1, "z": cz, "radius": 1.0 / 3.0, "strength": args.strength, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    t0 = time.time()
    rows = []
    while time.time() - t0 < args.seconds:
        u = api.debug("water_av_units", {"id": vid})
        if "error" in u: time.sleep(0.1); continue
        nx, nz = u["nx"], u["nz"]
        mass = np.array(u["mass"]).reshape(nz, nx); ke = np.array(u["ke"]).reshape(nz, nx); df = np.array(u["max_df"]).reshape(nz, nx)
        wet = mass > 1e-3
        ratio = np.where(wet, ke / np.maximum(mass, 1e-9), 0.0)
        vv = vol()
        row = {"t": round(time.time() - t0, 2), "wet_cols": int(wet.sum()), "solver_ms": vv.get("gpu_device_ms"), "asleep": vv.get("asleep"),
               "awake_columns": vv.get("awake_columns"), "frozen_columns": vv.get("frozen_columns"), "moving_columns": vv.get("moving_columns"),
               "step_slots": vv.get("step_slots"), "partition_moves": vv.get("partition_moves"), "bricks": vv.get("gpu_bricks"),
               "substeps": (vv.get("last") or {}).get("substeps"), "sweeps": vv.get("gpu_sweeps")}
        for name, s in (("x1", 1.0), ("x0.1", 0.1), ("x10", 10.0)):
            moving = wet & ((ratio >= KE_WAKE * s) | (df >= DF_QUIET * s))
            r = {"moving": round(float(moving.sum()) / max(1, wet.sum()), 4)}
            for k in (1, 2):
                m = dilate(moving, k) & wet
                r[f"margin{k}"] = round(float(m.sum()) / max(1, wet.sum()), 4)
                r[f"margin{k}_cells"] = round(float(mass[m].sum()) / max(1e-9, mass[wet].sum()), 4)
            row[name] = r
        if not rows:
            row["map_x1"] = ["".join("#" if (wet & ((ratio >= KE_WAKE) | (df >= DF_QUIET)))[z, x] else ("." if wet[z, x] else " ") for x in range(nx)) for z in range(nz)]
        rows.append(row)
        print(f"t {row['t']:5.2f}s  moving {row['x1']['moving']:.3f}  +1 {row['x1']['margin1']:.3f}  +2 {row['x1']['margin2']:.3f} (cells {row['x1']['margin2_cells']:.3f})"
              f"  | x0.1: +2 {row['x0.1']['margin2']:.3f}  x10: +2 {row['x10']['margin2']:.3f}  | solver {row['solver_ms']:.2f} ms  awake {row['awake_columns']} frozen {row['frozen_columns']} slots {row['step_slots']}/{row['bricks']} moves {row['partition_moves']} asleep {row['asleep']}", flush=True)
        time.sleep(0.25)
    api.debug("water_av_units", {"capture": False})
    api.debug("water_av_destroy", {"id": vid})
    out = {"size_m": [l, w], "depth_m": args.depth, "partial": args.partial, "quiet_scale": args.quiet_scale, "at": args.at, "strength": args.strength, "before_stone": {k: pre.get(k) for k in ("asleep", "gpu_device_ms", "kinetic_energy")},
           "thresholds": {"ke_wake": KE_WAKE, "df_quiet": DF_QUIET}, "rows": rows}
    EV.mkdir(parents=True, exist_ok=True)
    tag = args.tag or f"{args.size}_{args.at}"
    (EV / f"wake_map_{tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"wake_map_{tag}.json")


if __name__ == "__main__":
    main()
