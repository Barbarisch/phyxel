#!/usr/bin/env python3
"""The water feel harness (docs/WaterCore.md sec. 3 and sec. 14.5): run one scenario on a bench,
measure it with the stated primitives, compare to the prediction written in the scenario, record
an evidence row + a capture. Every row carries the engine ("ca" = today's WaterManager CA,
"core" = WaterCore once it exists), the bench, git head, build config, the prediction and the
measurement, so the CA's rows are the red baseline the core's rows are compared to.

    python tools/water_feel.py S3 --bench basin --engine ca      # dam break on the Basin rig
    python tools/water_feel.py list

Measurement primitives on TODAY's engine (Phase A routes):
  water_probe_rect  - per-column surface Y + column mass over a rect in one call (front tracking)
  water_ledger      - sum of mass by representation (conservation)
  place_water_box   - the initial condition in one command
The CA's sim region is a 64x32x64 window that FOLLOWS THE CAMERA (origin.y = cam.y - 16), so the
camera must be posed LOW near the rig or the basin floor sits under the window and nothing
simulates - the harness poses it and asserts the rig is inside the region before placing water.

Evidence: docs/evidence/water_feel/<scenario>_<bench>_<engine>_<timestamp>.json (+ .png).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set, load_def, project_url, vantage  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
EVID = ROOT / "docs" / "evidence" / "water_feel"
G = 9.81


def git_head():
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT).decode().strip()
    except Exception:
        return "unknown"


def capture(api, dst):
    try:
        api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception:
        pass
    r = api.get("/api/screenshot", timeout=90)
    p = r.get("path") or next((s.get("path") for s in r.get("screenshots", [])), None)
    if p and not os.path.isabs(p):
        p = os.path.join(ROOT, p)
    if p and os.path.exists(p):
        shutil.copy(p, dst)
        return str(dst)
    return None


def assert_dry(api):
    st = api.debug("water_stats")
    if "error" in st:
        raise SystemExit(f"water_stats: {st}")
    if st.get("total_mass", 0.0) > 1e-6:
        raise SystemExit(f"rig is not dry: total_mass {st['total_mass']} (restart the engine)")
    return st


def region_contains(api, box):
    """box = (x1,y1,z1,x2,y2,z2) world cells; the CA region must contain it entirely."""
    # water_ledger reports the region as integers; water_stats formats it as strings.
    led = api.debug("water_ledger")
    if not led.get("sim_region"):
        raise SystemExit(f"no sim region (WaterManager absent?): {led}")
    o, d = led["sim_region"]["origin"], led["sim_region"]["dims"]
    ox, oy, oz = (int(v) for v in o); dx, dy, dz = (int(v) for v in d)
    x1, y1, z1, x2, y2, z2 = box
    inside = ox <= x1 and x2 < ox + dx and oy <= y1 and y2 < oy + dy and oz <= z1 and z2 < oz + dz
    return inside, {"origin": [ox, oy, oz], "dims": [dx, dy, dz]}


# ---------------------------------------------------------------- scenarios -------------------
def s3_dam_break(api, gdef, args):
    """S3 on the Basin rig: a 3-deep block x 22..28 over the flat floor (top 12) released toward the
    ramp foot at x 12 (L = 10 u). Prediction: Ritter front speed 2*sqrt(g*h0) = 10.8 m/s -> 0.93 s
    (frictionless bound). Reflected crest at the east wall >= 0.8x incident; flat within 1 mm after
    10 s; mass +- 1e-4. Measured: front x(t) along the floor row z=15 (column mass > 0.5), the max
    surface at the west end over time, the final surface flatness, the mass ledger."""
    spec = gdef["waterBench"]
    a = spec["basinA"]
    floor_top = a["flat"]["floorTop"]            # 12
    z0, z1 = a["z"]                              # 10..21
    # The block starts at the WEST end of the flat floor and runs EAST into the vertical wall at
    # x = eastWallX, so the reflection is a real wall reflection (the first version released it
    # against the wall and read the draining block as a "crest" - a false pass, 2026-10-08).
    bx1, bx2 = a["flat"]["x"][0], a["flat"]["x"][0] + 6      # 12..18
    h0 = 3.0
    y1, y2 = floor_top + 1, floor_top + int(h0)  # cells 13..15
    wall_x = a["eastWallX"]                      # 29 (solid); last floor column is 28
    front_target = wall_x - 1
    L = front_target - bx2                       # 10
    ritter_v = 2.0 * (G * h0) ** 0.5
    area = (a["flat"]["x"][1] - a["flat"]["x"][0] + 1) * (z1 - z0 + 1)  # flat floor columns
    mass_expected = (bx2 - bx1 + 1) * (z1 - z0 + 1) * h0
    pred = {"ritter_front_speed_mps": ritter_v, "front_time_s": L / ritter_v, "L": L, "h0": h0,
            "mass_expected": mass_expected,
            "still_level_over_flat_floor": floor_top + 1 + mass_expected / area,   # ignores the ramp's extra area: a lower bound
            "reflected_crest_min_ratio": 0.8, "flat_after_s": 10.0, "flat_tolerance": 1e-3, "mass_tolerance": 1e-4}

    # Pose low so the CA window (cam.y - 16 .. +16) contains the basin floor; assert it.
    v = vantage(gdef, "east_wall")
    camera_set(api, v)
    time.sleep(2.0)
    api.debug("water_sync")
    inside, region = region_contains(api, (a["x"][0], floor_top, z0, a["x"][1], y2 + 1, z1))
    if not inside:
        raise SystemExit(f"basin not inside the CA sim region {region} - pose the camera lower/closer")
    assert_dry(api)
    led0 = api.debug("water_ledger")

    # Release: one command places the block; sampling starts immediately.
    t0 = time.time()
    r = api.debug("place_water_box", {"x1": bx1, "y1": y1, "z1": z0, "x2": bx2, "y2": y2, "z2": z1, "mass": 1.0})
    samples = []
    front_hit_t = None
    row_z = (z0 + z1) // 2
    rect = {"x1": a["x"][0], "z1": row_z, "x2": a["x"][1], "z2": row_z, "y1": floor_top, "y2": y2 + 2}
    end = t0 + args.duration
    while time.time() < end:
        t = time.time() - t0
        pr = api.debug("water_probe_rect", rect)
        cols = pr["columns"]
        wet = [c for c in cols if c[3] is not None and c[3] > 0.5]
        front_x = max(c[0] for c in wet) if wet else None          # the front runs EAST
        if front_x is not None and front_x >= front_target and front_hit_t is None:
            front_hit_t = t
        west = [c for c in cols if c[0] <= bx2 and c[2] is not None]             # where the block was
        east = [c for c in cols if c[0] >= front_target - 1 and c[2] is not None]  # the wall columns 27..28
        samples.append({"t": round(t, 3), "front_x": front_x,
                        "west_max_surface": max((c[2] for c in west), default=None),
                        "east_max_surface": max((c[2] for c in east), default=None),
                        "row_mass": pr["total_mass"],
                        "total_mass": api.debug("water_ledger")["cells"]})
        time.sleep(max(0.0, args.dt - (time.time() - t0 - t)))
    # Final flatness over the whole basin row.
    pr = api.debug("water_probe_rect", rect)
    surf = [c[2] for c in pr["columns"] if c[2] is not None]
    flat = (max(surf) - min(surf)) if surf else None
    led1 = api.debug("water_ledger")
    meas = {"placed_cells": r.get("placed"), "mass_after_place": r.get("total_mass"),
            "front_hit_time_s": front_hit_t,
            "front_speed_ratio_vs_ritter": (pred["front_time_s"] / front_hit_t) if front_hit_t else 0.0,
            "west_surface_peak": max((s["west_max_surface"] for s in samples if s["west_max_surface"] is not None), default=None),
            # the wall crest: the highest surface at the wall columns AFTER the front arrived there
            "east_surface_peak_after_1s": max((s["east_max_surface"] for s in samples
                                               if front_hit_t is not None and s["t"] >= front_hit_t and s["east_max_surface"] is not None), default=None),
            "final_surface_spread": flat, "final_mass": led1["cells"],
            "mass_drift": led1["cells"] - pred["mass_expected"], "samples": samples}
    verdict = {
        "front_speed": "FAIL" if not front_hit_t or meas["front_speed_ratio_vs_ritter"] < 0.85 else "PASS",
        # a reflection piles the wall columns above the still level by >= 0.8 x the incident
        # amplitude (h0 - still depth); a surface that merely rises to the still level is not a crest
        "reflected_crest": "FAIL" if (meas["east_surface_peak_after_1s"] or 0) <
                                     pred["still_level_over_flat_floor"] + pred["reflected_crest_min_ratio"] * (floor_top + 1 + h0 - pred["still_level_over_flat_floor"]) else "PASS",
        "flat_at_rest": "FAIL" if flat is None or flat > pred["flat_tolerance"] else "PASS",
        "mass": "FAIL" if abs(meas["mass_drift"]) > pred["mass_tolerance"] else "PASS"}
    return pred, meas, verdict


SCENARIOS = {"S3": ("basin", s3_dam_break)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario")
    ap.add_argument("--bench")
    ap.add_argument("--engine", default="ca", choices=["ca", "core"])
    ap.add_argument("--duration", type=float, default=12.0, help="sampling window, s")
    ap.add_argument("--dt", type=float, default=0.1, help="sampling period, s")
    ap.add_argument("--url", default=os.environ.get("PHYXEL_API_URL"))
    args = ap.parse_args()
    if args.scenario == "list":
        for k, (b, f) in SCENARIOS.items():
            print(k, b, (f.__doc__ or "").strip().splitlines()[0])
        return
    if args.scenario not in SCENARIOS:
        raise SystemExit(f"unknown scenario {args.scenario}; have {list(SCENARIOS)}")
    bench_default, fn = SCENARIOS[args.scenario]
    bench = args.bench or bench_default
    gdef = load_def(bench)
    api = Api(project_url(bench, args.url))
    api.wait_status()
    status = api.get("/api/status")
    pred, meas, verdict = fn(api, gdef, args)
    EVID.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    base = EVID / f"{args.scenario}_{bench}_{args.engine}_{stamp}"
    png = capture(api, base.with_suffix(".png"))
    row = {"scenario": args.scenario, "bench": bench, "engine": args.engine, "build_config": status.get("build_config"),
           "git_head": git_head(), "timestamp": stamp, "prediction": pred,
           "measurement": {k: v for k, v in meas.items() if k != "samples"}, "samples": meas.get("samples"),
           "verdict": verdict, "capture": png}
    base.with_suffix(".json").write_text(json.dumps(row, indent=1), encoding="utf-8")
    print(json.dumps({"prediction": pred, "measurement": row["measurement"], "verdict": verdict}, indent=1))
    print("evidence ->", base.with_suffix(".json").relative_to(ROOT))


if __name__ == "__main__":
    main()
