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
from water_bench import world_job, Api, camera_set, load_def, project_url, vantage  # noqa: E402

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
class Engine:
    """How a scenario talks to today's CA ("ca", real time) or WaterCore ("core", explicit ticks
    in SIMULATION time). Same primitives, same predictions, so rows are comparable."""

    def __init__(self, api, name, dt=1.0 / 60.0, cell_size=1.0, transport="eulerian", backend="cpu", sweeps=40):
        self.api, self.name, self.dt, self.cell_size, self.transport, self.backend, self.sweeps = api, name, dt, cell_size, transport, backend, sweeps
        self.av_id = None          # the first volume (S3's only one)
        self.av_ids = []
        self.sim_t = 0.0
        self.wall_t0 = None

    def require_core(self, scenario):
        if self.name != "core":
            raise SystemExit(f"{scenario} runs on WaterCore only (--engine core): the CA has no explicit ticks, sources or volumes")

    def create_volume(self, box):
        if self.name != "core":
            return None
        x1, y1, z1, x2, y2, z2 = box
        r = self.api.debug("water_av_create", {"x1": x1, "y1": y1, "z1": z1, "x2": x2, "y2": y2, "z2": z2,
                                               "cellSize": self.cell_size, "transport": self.transport, "backend": self.backend, "sweeps": self.sweeps})
        if "error" in r:
            raise SystemExit(f"water_av_create: {r}")
        vid = r["volume"]["id"]
        self.av_ids.append(vid)
        if self.av_id is None:
            self.av_id = vid
        return r["volume"]

    def destroy_volume(self):
        for vid in self.av_ids:
            self.api.debug("water_av_destroy", {"id": vid})
        self.av_ids = []
        self.av_id = None

    def volume(self, vid):
        for v in self.api.debug("water_av_list")["volumes"]:
            if v["id"] == vid:
                return v
        raise SystemExit(f"volume {vid} vanished")

    def source(self, vid, x, y, z, rate):
        r = self.api.debug("water_av_source", {"id": vid, "x": x, "y": y, "z": z, "rate": rate})
        if "error" in r:
            raise SystemExit(f"water_av_source: {r}")
        return r

    def clear_sources(self, vid):
        return self.api.debug("water_av_source", {"id": vid, "clear": True})

    def rect_mass(self, x1, z1, x2, z2, y1=None, y2=None):
        """Mass (m^3) over world columns x1..x2, z1..z2, optionally only cells with world y in y1..y2."""
        body = {"x1": x1, "z1": z1, "x2": x2, "z2": z2, "target": "core"}
        if y1 is not None:
            body["y1"], body["y2"] = y1, y2
        return self.api.debug("water_probe_rect", body)["total_mass"]

    def place_box(self, x1, y1, z1, x2, y2, z2, fill=1.0):
        body = {"x1": x1, "y1": y1, "z1": z1, "x2": x2, "y2": y2, "z2": z2, "mass": fill}
        if self.name == "core":
            body["target"] = "core"
        self.wall_t0 = time.time()
        return self.api.debug("place_water_box", body)

    def probe_rect(self, rect):
        body = dict(rect)
        if self.name == "core":
            body["target"] = "core"
        return self.api.debug("water_probe_rect", body)

    def mass(self):
        led = self.api.debug("water_ledger")
        return led["core_cells"] if self.name == "core" else led["cells"]

    def advance(self, seconds):
        """Advance by `seconds`: real sleep for the CA, explicit ticks for EVERY volume for the core."""
        if self.name == "core":
            ticks = max(1, int(round(seconds / self.dt)))
            last = None
            for vid in self.av_ids:
                r = self.api.debug("water_av_step", {"id": vid, "ticks": ticks, "dt": self.dt}, timeout=600)
                if "error" in r:
                    raise SystemExit(f"water_av_step: {r}")
                last = r["volume"]
            self.sim_t += ticks * self.dt
            return last
        time.sleep(seconds)
        return None

    def now(self):
        return self.sim_t if self.name == "core" else (time.time() - self.wall_t0)


def merian_period(a, still_level):
    """Fundamental seiche of a closed basin with a stepped floor (Merian 1828, variable depth):
    T = 2 * sum(section length / sqrt(g * depth)) over the sections the still level wets."""
    total = 0.0
    sections = [(a["flat"]["x"], a["flat"]["floorTop"])] + [(r["x"], r["floorTop"]) for r in a["ramp"]]
    for xs, floor in sections:
        depth = still_level - (floor + 1)
        if depth > 0.0:
            total += (xs[1] - xs[0] + 1) / (G * depth) ** 0.5
    return 2.0 * total


def s3_dam_break(api, gdef, args):
    """S3 on the Basin rig: a 3-deep block x 12..18 at the WEST end of the flat floor, released EAST
    toward the vertical wall at x 29 (L = 10 u). Prediction: the front measured at the resolvable
    contour d_th = 0.5 * cell runs at 2 sqrt(g h0) - 3 sqrt(g d_th) (Ritter 1892); wall crest >=
    still level + 0.8 x incident; flat within 1 mm after 10 s; mass +- 1e-4. Measured along the
    floor row z = 15. CA rows are in wall time, core rows in simulation time."""
    spec = gdef["waterBench"]
    a = spec["basinA"]
    floor_top = a["flat"]["floorTop"]            # 12
    z0, z1 = a["z"]                              # 10..21
    bx1, bx2 = a["flat"]["x"][0], a["flat"]["x"][0] + 6      # 12..18
    h0 = 3.0
    y1, y2 = floor_top + 1, floor_top + int(h0)  # cells 13..15
    wall_x = a["eastWallX"]                      # 29 (solid); last floor column is 28
    front_target = wall_x - 1
    L = front_target - bx2                       # 10
    eng = Engine(api, args.engine, cell_size=args.cell_size, transport=args.transport, backend=args.backend, sweeps=args.sweeps)
    dth = 0.5 * eng.cell_size if args.engine == "core" else 0.5
    vth = 2.0 * (G * h0) ** 0.5 - 3.0 * (G * dth) ** 0.5   # Ritter tip minus the contour offset
    area = (a["flat"]["x"][1] - a["flat"]["x"][0] + 1) * (z1 - z0 + 1)
    mass_expected = (bx2 - bx1 + 1) * (z1 - z0 + 1) * h0
    pred_still_level = floor_top + 1 + mass_expected / area
    pred = {"engine": args.engine, "transport": eng.transport, "backend": eng.backend, "gpu_sweeps": eng.sweeps, "cell_size": eng.cell_size, "time_base": "simulation" if args.engine == "core" else "wall",
            "front_depth_threshold_m": dth, "front_speed_mps": vth, "front_time_s": L / vth,
            "tip_time_s": L / (2.0 * (G * h0) ** 0.5), "L": L, "h0": h0,
            "mass_expected": mass_expected, "still_level_over_flat_floor": floor_top + 1 + mass_expected / area,
            # wall run-up: dam-break surges on a vertical wall reach 2.1-2.3 x the reservoir depth above
            # the bed (Fluids 2022, 7(8), 258: H = 200-300 mm, ultrasonic surge height); gate = 2.2 h0 +- 25 %
            "wall_runup_ratio_lit": 2.2, "wall_runup_tolerance": 0.25,
            "wall_runup_expected_y": floor_top + 1 + 2.2 * h0,
            "flat_after_s": 10.0, "flat_tolerance": 1e-3, "mass_tolerance": 1e-4,
            # the settled pool is a closed basin: its fundamental seiche (Merian 1828) has
            # T = 2 L / sqrt(g D) with L the basin length and D the still depth over the flat floor;
            # an inviscid solver sloshes for minutes, so REST on this rig is judged by the seiche
            # period (+- 20 %) and a non-growing envelope, not by "flat within 1 mm in 10 s"
            # the flat floor carries the mode (the 0.24 m-deep ramp step reflects it like a shoal):
            # the 1 m row read 9.1 s against 9.7 s flat-only and 13.7 s for the full stepped integral
            "seiche_period_s": 2.0 * (a["flat"]["x"][1] - a["flat"]["x"][0] + 1) / (G * (pred_still_level - (a["flat"]["floorTop"] + 1))) ** 0.5,
            "seiche_period_stepped_integral_s": merian_period(a, pred_still_level), "seiche_period_tolerance": 0.20,
            # float32 fill fractions: every face move rounds at ~1e-7 relative, so a 252 m^3 pool
            # drifts ~1e-9 x mass per tick (measured +2e-4 over 3 600 ticks); the gate scales with it
            "mass_tolerance_scaled": max(1e-4, 1e-9 * mass_expected * args.duration / (1.0 / 60.0))}

    v = vantage(gdef, "east_wall")
    camera_set(api, v)
    time.sleep(2.0)
    if args.engine == "ca":
        api.debug("water_sync")
        inside, region = region_contains(api, (a["x"][0], floor_top, z0, a["x"][1], y2 + 1, z1))
        if not inside:
            raise SystemExit(f"basin not inside the CA sim region {region} - pose the camera lower/closer")
        assert_dry(api)
    else:
        # the volume covers the basin interior plus the air above the rim up to y 21, so a 2.2 h0
        # run-up (y 19.6) is not clipped by the volume's own ceiling (it was, at 17: 2026-10-08)
        vol = eng.create_volume((a["x"][0], floor_top + 1, z0, a["x"][1], 21, z1))
        print(f"   core volume {vol['id']}: {vol['cells']} cells at h={vol['cellSize']:.4f}")

    r = eng.place_box(bx1, y1, z0, bx2, y2, z1, 1.0)
    samples = []
    front_hit_t = None
    row_z = (z0 + z1) // 2
    rect = {"x1": a["x"][0], "z1": row_z, "x2": a["x"][1], "z2": row_z, "y1": floor_top, "y2": 21}
    while eng.now() < args.duration:
        if args.engine == "core":
            eng.advance(args.dt)
        t = eng.now()
        pr = eng.probe_rect(rect)
        cols = pr["columns"]
        wet = [c for c in cols if c[3] is not None and c[3] > dth]
        front_x = max(c[0] for c in wet) if wet else None
        if front_x is not None and front_x >= front_target and front_hit_t is None:
            front_hit_t = t
        west = [c for c in cols if c[0] <= bx2 and c[2] is not None]
        east = [c for c in cols if c[0] >= front_target - 1 and c[2] is not None]
        fx0, fx1 = a["flat"]["x"]
        wpool = [c[2] for c in cols if fx0 <= c[0] <= fx0 + 5 and c[2] is not None and c[3] is not None and c[3] > 0.05]
        epool = [c[2] for c in cols if fx1 - 5 <= c[0] <= fx1 and c[2] is not None and c[3] is not None and c[3] > 0.05]
        pool = [c[2] for c in cols if c[2] is not None and c[3] is not None and c[3] > 0.05]
        samples.append({"t": round(t, 3), "front_x": front_x, "pool_spread": (max(pool) - min(pool)) if pool else None,
                        "west_pool_mean": (sum(wpool) / len(wpool)) if wpool else None,
                        "east_pool_mean": (sum(epool) / len(epool)) if epool else None,
                        "west_max_surface": max((c[2] for c in west), default=None),
                        "east_max_surface": max((c[2] for c in east), default=None),
                        "row_mass": pr["total_mass"], "total_mass": eng.mass()})
        if args.engine == "ca":
            time.sleep(max(0.0, args.dt - (eng.now() - t)))
    def seiche_from_samples(samples, t_from):
        """Period of the west-vs-east surface see-saw (zero crossings of the difference) and the
        envelope trend over the window after t_from."""
        raw = [(x["t"], x["west_pool_mean"] - x["east_pool_mean"]) for x in samples
               if x["t"] >= t_from and x["west_pool_mean"] is not None and x["east_pool_mean"] is not None]
        if len(raw) < 10:
            return None, None
        mean = sum(v for _, v in raw) / len(raw)
        vals = [v - mean for _, v in raw]                  # the see-saw about its own mean (the floor is not symmetric)
        # the period is the lag of the first autocorrelation maximum beyond 3 s: zero crossings of a
        # smoothed signal counted a secondary wobble at 1/3 m and read half-periods (4.8 s and 5.4 s for
        # the same water whose autocorrelation reads 11.6 s and 10.9 s, 2026-10-08)
        n = len(vals); lag0 = sum(a * a for a in vals)
        period = None
        if lag0 > 0:
            best, best_lag = -2.0, None
            for lag in range(int(3.0 / args.dt), n // 2):
                c = sum(vals[i] * vals[i + lag] for i in range(n - lag)) / lag0
                if c > best:
                    best, best_lag = c, lag
            period = best_lag * args.dt if best_lag else None
        pts = [(raw[i][0], vals[i]) for i in range(n)]
        half = len(pts) // 2
        env_first = max(abs(v) for _, v in pts[:half]); env_second = max(abs(v) for _, v in pts[half:])
        return period, (env_second / env_first) if env_first > 0 else None

    pr = eng.probe_rect(rect)
    # flatness is judged over POOL columns (depth > 5 cm): a held film of a centimetre or two on
    # the ramp step is S1 behaviour (films >= 0.01 m hold), not a slope in the pool's surface
    surf = [c[2] for c in pr["columns"] if c[2] is not None and c[3] is not None and c[3] > 0.05]
    flat = (max(surf) - min(surf)) if surf else None
    held_films = [(c[0], round(c[3], 4)) for c in pr["columns"] if c[3] is not None and 0 < c[3] <= 0.05]
    final_mass = eng.mass()
    meas = {"placed": r.get("placed", r.get("cells_set")), "mass_after_place": r.get("total_mass", r.get("core_total_mass")),
            "front_hit_time_s": front_hit_t,
            "front_speed_ratio_vs_ritter": (pred["front_time_s"] / front_hit_t) if front_hit_t else 0.0,
            "west_surface_peak": max((s["west_max_surface"] for s in samples if s["west_max_surface"] is not None), default=None),
            "east_surface_peak_after_1s": max((s["east_max_surface"] for s in samples
                                               if front_hit_t is not None and s["t"] >= front_hit_t and s["east_max_surface"] is not None), default=None),
            "final_surface_spread": flat, "held_films_x_depth": held_films, "final_mass": final_mass,
            "residue_dropped_m3": (eng.volume(eng.av_id).get("residue_dropped_m3") if args.engine == "core" else None),
            "seiche_period_measured_s": seiche_from_samples(samples, (front_hit_t or 0.0) + 2.0)[0],
            "seiche_envelope_ratio_second_half_over_first": seiche_from_samples(samples, (front_hit_t or 0.0) + 2.0)[1],
            "mass_drift": final_mass - pred["mass_expected"], "samples": samples}
    verdict = {
        # the front must lie inside the Ritter envelope: no slower than 0.85 x the contour speed and
        # no faster than the dry-bed TIP 2 sqrt(g h0) (a front beating the tip is a numerical artefact)
        "front_speed": "FAIL" if not front_hit_t or meas["front_speed_ratio_vs_ritter"] < 0.85
                       or front_hit_t < L / (2.0 * (G * h0) ** 0.5) else "PASS",
        "wall_runup_vs_literature": "FAIL" if meas["east_surface_peak_after_1s"] is None or
                                     abs((meas["east_surface_peak_after_1s"] - (floor_top + 1)) / h0 - pred["wall_runup_ratio_lit"]) > pred["wall_runup_tolerance"] * pred["wall_runup_ratio_lit"] else "PASS",
        "flat_at_rest_10s_1mm": "FAIL" if flat is None or flat > pred["flat_tolerance"] else "PASS",   # kept for the record: unphysical on this rig (see seiche)
        "seiche_period_vs_merian": "FAIL" if meas["seiche_period_measured_s"] is None or
                                    abs(meas["seiche_period_measured_s"] - pred["seiche_period_s"]) > pred["seiche_period_tolerance"] * pred["seiche_period_s"] else "PASS",
        "seiche_envelope_not_growing": "FAIL" if meas["seiche_envelope_ratio_second_half_over_first"] is None or meas["seiche_envelope_ratio_second_half_over_first"] > 1.0 else "PASS",
        "mass": "FAIL" if abs(meas["mass_drift"]) > pred["mass_tolerance_scaled"] else "PASS"}
    if args.engine == "core" and not args.keep_volume:
        eng.destroy_volume()
    return pred, meas, verdict


def rig_named(gdef, name):
    for r in gdef["waterBench"]["rigs"]:
        if r["name"] == name:
            return r
    raise SystemExit(f"no rig '{name}' in game.json")


def columns_stats(api, x1, z1, x2, z2, film_min=1e-3):
    pr = api.debug("water_probe_rect", {"x1": x1, "z1": z1, "x2": x2, "z2": z2, "target": "core"})
    cols = pr["columns"]
    wet = [c for c in cols if c[3] is not None and c[3] > film_min]
    return {"mass": pr["total_mass"], "wet_columns": len(wet),
            "max_depth": max((c[3] for c in wet), default=0.0),
            "min_wet_depth": min((c[3] for c in wet), default=0.0)}


def s1_pour(api, gdef, args):
    """S1 on the Small pad rig: 0.02 m^3 released 1 m above the flat stone pad (and, as the control,
    the same pour over the 1-deep pit). Prediction: the pad keeps 0.02 +- 1e-4 m^3 and nothing
    leaves the pad box; the puddle is at rest (volume asleep or specific KE < 1e-6) within 3 s; a
    film >= 0.01 m is what holds (P: puddles do not thin to nothing); the pit run ends with all
    0.02 m^3 inside the pit columns and 0 on the pad around it. Core only; simulation time."""
    rig = rig_named(gdef, "pad")
    sc = rig["scenarios"]["S1"]
    px, _, pz = sc["pour_at"]
    pit = sc["pit"]                                    # x1, z1, x2, z2
    pad_top = rig["slab"]["y"][1]                      # 16: the pad surface is y = 17.0
    vol = 0.02
    eng = Engine(api, args.engine, cell_size=args.cell_size, transport=args.transport, backend=args.backend, sweeps=args.sweeps)
    eng.require_core("S1")
    pred = {"engine": "core", "transport": eng.transport, "backend": eng.backend, "gpu_sweeps": eng.sweeps, "cell_size": eng.cell_size, "time_base": "simulation", "volume_m3": vol,
            "mass_tolerance": 1e-4, "rest_within_s": 3.0, "film_holds_m": 0.01, "pit": pit}
    boxA = (px - 6, pad_top + 1, pz - 6, px + 6, pad_top + 2, pz + 6)
    boxB = (pit[0] - 4, pad_top, pit[1] - 4, pit[2] + 4, pad_top + 2, pit[3] + 4)   # pit cells are y = 16
    camera_set(api, vantage(gdef, "pad"))
    vA = eng.create_volume(boxA)
    vB = eng.create_volume(boxB)
    print(f"   volumes {vA['id']} ({vA['cells']} cells) and {vB['id']} ({vB['cells']} cells) at h={eng.cell_size:.4f}")
    # 1 m above the pad: the voxel whose floor is y = pad_top + 2 = 18.0; `fill_each` is m^3 per voxel
    rA = eng.place_box(px, pad_top + 2, pz, px, pad_top + 2, pz, vol)
    cx, cz = (pit[0] + pit[2]) // 2, (pit[1] + pit[3]) // 2
    rB = eng.place_box(cx, pad_top + 2, cz, cx, pad_top + 2, cz, vol)
    samples = []
    rest_t = None
    while eng.now() < args.duration:
        eng.advance(args.dt)
        t = eng.now()
        a, b = eng.volume(vA["id"]), eng.volume(vB["id"])
        padA = columns_stats(api, *boxA[0:1], *boxA[2:3], *boxA[3:4], *boxA[5:6])
        pitB = eng.rect_mass(pit[0], pit[1], pit[2], pit[3])
        spec_ke = a["kinetic_energy"] / max(a["mass"], 1e-9)
        # rest = the volume went to sleep (30 quiet ticks); the bare KE threshold reads 9e-7 at the
        # very first sample, before gravity has done anything, so it is not a rest signal on its own
        if rest_t is None and a["asleep"]:
            rest_t = t
        samples.append({"t": round(t, 3), "pad_mass": padA["mass"], "wet_columns": padA["wet_columns"],
                        "max_depth": padA["max_depth"], "min_wet_depth": padA["min_wet_depth"],
                        "specific_ke": spec_ke, "asleep": a["asleep"], "pit_mass": pitB, "volB_mass": b["mass"]})
    a, b = eng.volume(vA["id"]), eng.volume(vB["id"])
    last = samples[-1]
    shape_t = next((x["t"] for i, x in enumerate(samples) if all(abs(y["max_depth"] - x["max_depth"]) < 1e-4 and y["wet_columns"] == x["wet_columns"] for y in samples[i:])), None)
    meas = {"placed_pad": rA.get("core_total_mass"), "pad_mass_final": a["mass"], "pad_mass_drift": a["mass"] - vol,
            "rest_time_s": rest_t, "shape_settled_s": shape_t, "final_wet_columns": last["wet_columns"], "final_max_depth": last["max_depth"],
            "final_min_wet_depth": last["min_wet_depth"], "residue_dropped_m3": a.get("residue_dropped_m3"),
            "pit_mass_final": last["pit_mass"], "pit_outside_mass": b["mass"] - last["pit_mass"], "samples": samples}
    # "a film holds": the puddle stops spreading once nothing is deeper than the hold depth - its
    # wet area is stable over the last 2 s and its deepest column sits in [0.5, 1.0] x hold (a
    # sheet thinning toward a monolayer would keep adding columns and fall far below the hold)
    tail = [x for x in samples if x["t"] >= samples[-1]["t"] - 2.0]
    area_stable = len({x["wet_columns"] for x in tail}) == 1
    meas["wet_area_stable_last_2s"] = area_stable
    verdict = {"mass_on_pad": "PASS" if abs(meas["pad_mass_drift"]) <= pred["mass_tolerance"] else "FAIL",
               "at_rest_within_3s": "PASS" if rest_t is not None and rest_t <= pred["rest_within_s"] else "FAIL",
               "film_holds": "PASS" if area_stable and 0.5 * pred["film_holds_m"] <= last["max_depth"] <= 1.0 * pred["film_holds_m"] + 1e-4 else "FAIL",
               "control_pit_holds_all": "PASS" if abs(last["pit_mass"] - vol) <= pred["mass_tolerance"] and abs(meas["pit_outside_mass"]) <= pred["mass_tolerance"] else "FAIL"}
    if not args.keep_volume:
        eng.destroy_volume()
    return pred, meas, verdict


def s11_rest_persist(api, gdef, args):
    """S11 on the Small pad rig (WaterCore Phase D, docs/WaterCore.md 16.7): 0.02 m^3 poured onto the
    pad rests, the volume SLEEPS (write-back to chunk spans + body record, volume freed), the stored
    spans equal the record, the pad is re-woken from spans and 60 ticks change nothing, and after
    save -> cold restart the spans and the body mass are identical. Predictions: asleep <= 10 s;
    surface_vs_mass_mm <= 1; mass_written = poured - residue_dropped (tick-scaled gate); volumes 0
    after sleep; stored span depth over the pad = mass_written +- 1e-4; re-wake delta mass <= 1e-6;
    restart spans identical +- 1e-4. Control: a column 1 voxel outside the pour box has no span
    before and after. Core only; the cold-restart leg needs --restart-cmd (the bench's `phyxel up`)."""
    rig = rig_named(gdef, "pad")
    sc = rig["scenarios"]["S1"]
    px, _, pz = sc["pour_at"]
    pad_top = rig["slab"]["y"][1]
    vol = 0.02
    eng = Engine(api, args.engine, cell_size=args.cell_size, transport=args.transport, backend=args.backend, sweeps=args.sweeps)
    eng.require_core("S11")
    pred = {"engine": "core", "transport": eng.transport, "backend": eng.backend, "cell_size": eng.cell_size, "time_base": "simulation",
            "asleep_within_s": 10.0, "surface_vs_mass_mm_max": 1.0, "mass_gate": "max(1e-4, 1e-9*mass*ticks)", "volumes_after_sleep": 0,
            "stored_depth_equals_written_tol": 1e-4, "rewake_delta_mass_tol": 1e-6, "restart_span_tol": 1e-4}
    box = (px - 6, pad_top + 1, pz - 6, px + 6, pad_top + 2, pz + 6)
    rect = {"x1": box[0], "z1": box[2], "x2": box[3], "z2": box[5]}
    ctrl = (box[3] + 1, pz)   # one voxel east of the box
    camera_set(api, vantage(gdef, "pad"))
    # control + baseline: no stored water on the pad or the control column before the pour
    before = api.debug("water_spans_stored", rect)
    ctrl_before = api.debug("water_spans_stored", {"x1": ctrl[0], "z1": ctrl[1], "x2": ctrl[0], "z2": ctrl[1]})
    v = eng.create_volume(box)
    eng.place_box(px, pad_top + 2, pz, px, pad_top + 2, pz, vol)
    rest_t = None; ticks = 0
    while eng.now() < args.duration:
        last = eng.advance(args.dt); ticks += max(1, int(round(args.dt / eng.dt)))
        if last and last["asleep"]:
            rest_t = eng.now(); break
    a = eng.volume(v["id"])
    sl = api.debug("water_av_sleep", {"id": v["id"]})
    after = api.debug("water_spans_stored", rect)
    ctrl_after = api.debug("water_spans_stored", {"x1": ctrl[0], "z1": ctrl[1], "x2": ctrl[0], "z2": ctrl[1]})
    vols = api.debug("water_av_list")["volumes"]
    table = api.debug("water_body_table", {"verify": True})
    led = api.debug("water_ledger")
    # re-wake from the spans and step 60 ticks: nothing changes
    w = api.debug("water_av_create", {"x1": box[0], "y1": box[1], "z1": box[2], "x2": box[3], "y2": box[4], "z2": box[5],
                                      "cellSize": eng.cell_size, "transport": "eulerian", "backend": eng.backend, "seed": "spans", "auto_sleep": False})
    rewake = {}
    if "error" in w:
        rewake["error"] = w["error"]
    else:
        m0 = w["seeded_mass"]
        st = api.debug("water_av_step", {"id": w["volume"]["id"], "ticks": 60, "dt": eng.dt}, timeout=600)["volume"]
        sl2 = api.debug("water_av_sleep", {"id": w["volume"]["id"], "force": True})
        after2 = api.debug("water_spans_stored", rect)
        table = api.debug("water_body_table", {"verify": True})   # the table the save will persist
        rewake = {"seeded_mass": m0, "mass_after_60": st["mass"], "delta_mass": st["mass"] - m0, "asleep_after_60": st["asleep"],
                  "written_again": sl2.get("mass_written"), "stored_again": sl2.get("mass_stored"), "depth_after_second_sleep": after2.get("total_depth"), "depth_delta_second_sleep": after2.get("total_depth", 0) - after.get("total_depth", 0),
                  "body_table_after": table.get("table")}
    meas = {"rest_time_s": rest_t, "ticks_run": ticks, "sleep": {k: sl.get(k) for k in ("success", "error", "columns", "runs", "held_columns", "unwritten_columns", "chunks_touched", "mass_written", "surface_vs_mass_mm", "spread_mm", "body_id")},
            "residue_dropped_m3": a.get("residue_dropped_m3"), "placed": vol,
            "spans_before": before.get("spans"), "spans_after": after.get("spans"), "stored_depth_after": after.get("total_depth"),
            "control_spans_before": ctrl_before.get("spans"), "control_spans_after": ctrl_after.get("spans"),
            "volumes_after_sleep": len(vols), "ledger_after": {k: led.get(k) for k in ("core_cells", "spans", "bodies", "total")},
            "body_table": table.get("table"), "rewake": rewake}
    gate = max(1e-4, 1e-9 * vol * ticks)
    written = sl.get("mass_written") or 0.0
    expected = vol - (a.get("residue_dropped_m3") or 0.0)
    verdict = {"asleep_within_10s": "PASS" if rest_t is not None and rest_t <= pred["asleep_within_s"] else "FAIL",
               "write_back_ok": "PASS" if sl.get("success") else "FAIL",
               "surface_vs_mass_1mm": "PASS" if sl.get("success") and sl["surface_vs_mass_mm"] <= pred["surface_vs_mass_mm_max"] else "FAIL",
               "mass_written_equals_poured": "PASS" if abs(written - expected) <= gate else "FAIL",
               "volume_freed": "PASS" if len(vols) == 0 else "FAIL",
               "stored_depth_equals_written": "PASS" if after.get("total_depth") is not None and abs(after["total_depth"] - written) <= pred["stored_depth_equals_written_tol"] else "FAIL",
               "control_column_dry": "PASS" if ctrl_before.get("spans") == 0 and ctrl_after.get("spans") == 0 else "FAIL",
               "rewake_changes_nothing": "PASS" if rewake and "error" not in rewake and abs(rewake["delta_mass"]) <= pred["rewake_delta_mass_tol"] and abs(rewake["depth_delta_second_sleep"]) <= pred["restart_span_tol"] else "FAIL",
               "body_mass_equals_spans": "PASS" if table.get("table") and all(abs(b.get("diff", 1e9)) <= 1e-9 for b in table["table"] if b["origin"] == "av") else "FAIL"}
    # cold restart leg: save, restart the engine, re-read the spans and the table
    if getattr(args, "restart_cmd", None):
        api.debug("water_save", {})
        save = api.post("/api/world/save", {})
        import subprocess, shlex
        subprocess.run(shlex.split(args.restart_cmd), check=False)
        api.wait_status(300)
        after_restart = api.debug("water_spans_stored", rect)
        table_restart = api.debug("water_body_table", {})
        meas["restart"] = {"save": save, "spans": after_restart.get("spans"), "stored_depth": after_restart.get("total_depth"),
                           "depth_delta": (after_restart.get("total_depth") or 0.0) - (after.get("total_depth") or 0.0),
                           "body_table": table_restart.get("table")}
        same_mass = table.get("table") and table_restart.get("table") and abs(sum(b["mass"] for b in table["table"] if b["origin"] == "av") - sum(b["mass"] for b in table_restart["table"] if b["origin"] == "av")) <= 1e-9
        verdict["restart_identical"] = "PASS" if abs(meas["restart"]["depth_delta"]) <= pred["restart_span_tol"] and same_mass else "FAIL"
    else:
        verdict["restart_identical"] = "SKIPPED (no --restart-cmd)"
    return pred, meas, verdict


def r3_edits_never_create(api, gdef, args):
    """R3 on the Small pond rig (WaterCore Phase D2, docs/WaterCore.md 16.3): edits never create water.
    The pond (4x4, floor top 14) is filled to 16.5 through a volume, rested and SLEPT (spans
    stored). Then, with no volume awake: (b) a Stone cube placed into a pond column at y 15 clips
    that column's span to [16, 16.5], displaces exactly 1.0 m^3 (ledger total -1.0, body displaced
    1.0); (c) the floor voxel under another pond column is removed: its span falls one voxel
    (mass unchanged); (d) a pit dug in the dry slab 3 voxels from the pond stays DRY; (e) a wake
    over the pond seeds exactly the stored mass and sleeps back to the same spans. Control: an
    untouched pond column's span is identical throughout. Core only; simulation time."""
    rig = rig_named(gdef, "pond")
    pond = rig["scenarios"]["S13"]["pond"]           # x1, z1, x2, z2 = 100, 12, 103, 15
    x1, z1, x2, z2 = pond
    eng = Engine(api, args.engine, cell_size=args.cell_size, transport=args.transport, backend=args.backend, sweeps=args.sweeps)
    eng.require_core("R3")
    pred = {"engine": "core", "cell_size": eng.cell_size, "backend": eng.backend, "fill_top": 16.5,
            "cube_displaces_m3": 1.0, "undercut_shift": -1.0, "pit_spans": 0, "wake_seed_equals_stored_tol": 1e-4}
    box = (x1 - 1, 14, z1 - 1, x2 + 1, 18, z2 + 1)
    camera_set(api, vantage(gdef, "pond"))
    def spans_at(x, z):
        r = api.debug("water_spans_stored", {"x1": x, "z1": z, "x2": x, "z2": z, "columns": True})
        return sorted((c[2] for c in r.get("span_columns", [])))   # tops (one per vertical clip)
    def depth_at(x, z):
        return api.debug("water_spans_stored", {"x1": x, "z1": z, "x2": x, "z2": z}).get("total_depth")
    v = eng.create_volume(box)
    eng.place_box(x1, 15, z1, x2, 15, z2, 1.0)
    eng.place_box(x1, 16, z1, x2, 16, z2, 0.5)
    rest_t = None
    while eng.now() < args.duration:
        last = eng.advance(args.dt)
        if last and last["asleep"]:
            rest_t = eng.now(); break
    sl = api.debug("water_av_sleep", {"id": v["id"], "force": rest_t is None})
    eng.av_ids = []; eng.av_id = None
    led0 = api.debug("water_ledger")
    ctrl = (x1, z1)                                   # the pond's corner column, never edited
    ctrl0 = spans_at(*ctrl); d_ctrl0 = depth_at(*ctrl)
    # (b) a cube into (x1+1, 15, z1+1)
    cube = (x1 + 1, 15, z1 + 1)
    before_b = depth_at(cube[0], cube[2])
    pb = api.post("/api/world/voxel", {"x": cube[0], "y": cube[1], "z": cube[2], "material": "Stone"})
    after_b = depth_at(cube[0], cube[2]); tops_b = spans_at(cube[0], cube[2])
    led_b = api.debug("water_ledger"); table_b = api.debug("water_body_table", {})
    # (c) the floor voxel under (x1+2, z1+1): y 14
    under = (x1 + 2, 14, z1 + 1)
    before_c = depth_at(under[0], under[2]); tops_c0 = spans_at(under[0], under[2])
    pc = api.post("/api/world/voxel/remove", {"x": under[0], "y": under[1], "z": under[2]})
    after_c = depth_at(under[0], under[2]); tops_c = spans_at(under[0], under[2])
    # (d) a pit in the dry slab 3 voxels east of the pond: (x2+3, 16, z1+1)
    pit = (x2 + 3, 16, z1 + 1)
    pd = api.post("/api/world/voxel/remove", {"x": pit[0], "y": pit[1], "z": pit[2]})
    pit_spans = api.debug("water_spans_stored", {"x1": pit[0], "z1": pit[2], "x2": pit[0], "z2": pit[2]}).get("spans")
    led_d = api.debug("water_ledger")
    ctrl1 = spans_at(*ctrl); d_ctrl1 = depth_at(*ctrl)
    # (e) wake the pond and sleep it back
    stored_before_wake = api.debug("water_spans_stored", {"x1": x1, "z1": z1, "x2": x2, "z2": z2}).get("total_depth")
    w = api.debug("water_av_wake", {"x": x2, "y": 16, "z": z2, "cellSize": eng.cell_size, "backend": eng.backend, "auto_sleep": False})
    wake = {"error": w.get("error")} if "error" in w else {"seeded_mass": w["seeded_mass"], "flood_columns": w["flood_columns"], "truncated": w["truncated"], "box": w["box"]}
    if "error" not in w:
        st = api.debug("water_av_step", {"id": w["volume"]["id"], "ticks": 60, "dt": eng.dt}, timeout=600)["volume"]
        sl2 = api.debug("water_av_sleep", {"id": w["volume"]["id"], "force": True})
        wake.update({"mass_after_60": st["mass"], "asleep_after_60": st["asleep"], "stored_again": sl2.get("mass_stored"), "sleep_ok": sl2.get("success"), "sleep_error": sl2.get("error")})
        wake["stored_after"] = api.debug("water_spans_stored", {"x1": x1, "z1": z1, "x2": x2, "z2": z2}).get("total_depth")
    meas = {"rest_time_s": rest_t, "sleep": {k: sl.get(k) for k in ("success", "error", "columns", "mass_written", "mass_stored", "surface_vs_mass_mm")},
            "cube": {"at": cube, "placed": pb, "depth_before": before_b, "depth_after": after_b, "tops_after": tops_b,
                     "ledger_total_before": led0.get("total"), "ledger_total_after": led_b.get("total"), "displaced_ledger": led_b.get("displaced"),
                     "body_displaced": [b.get("displaced") for b in table_b.get("table", []) if b["origin"] == "av"]},
            "undercut": {"at": under, "removed": pc, "depth_before": before_c, "depth_after": after_c, "tops_before": tops_c0, "tops_after": tops_c},
            "pit": {"at": pit, "removed": pd, "spans": pit_spans, "ledger_total_after": led_d.get("total"), "held_edits": led_d.get("held_edits")},
            "control": {"at": ctrl, "tops_before": ctrl0, "tops_after": ctrl1, "depth_before": d_ctrl0, "depth_after": d_ctrl1},
            "wake": wake, "stored_before_wake": stored_before_wake}
    verdict = {"pond_slept": "PASS" if sl.get("success") else "FAIL",
               "cube_displaces_exactly_1": "PASS" if before_b is not None and after_b is not None and abs((before_b - after_b) - 1.0) <= 1e-4 and abs((led0.get("total", 0) - led_b.get("total", 0)) - 1.0) <= 1e-4 else "FAIL",
               "cube_span_top_unchanged": "PASS" if tops_b and before_b is not None and after_b is not None and abs(after_b - (before_b - 1.0)) <= 1e-6 and abs(tops_b[-1] - (15.0 + before_b)) <= 1e-4 else "FAIL",
               "body_debited": "PASS" if any(abs((d or 0) - 1.0) <= 1e-6 for d in meas["cube"]["body_displaced"]) else "FAIL",
               "undercut_falls_one_voxel_mass_exact": "PASS" if before_c is not None and after_c is not None and abs(after_c - before_c) <= 1e-6 and tops_c and tops_c0 and abs((tops_c[-1] - tops_c0[-1]) + 1.0) <= 1e-4 else "FAIL",
               "pit_stays_dry": "PASS" if pit_spans == 0 and abs(led_d.get("total", 0) - led_b.get("total", 0)) <= 1e-6 else "FAIL",
               "control_unchanged": "PASS" if ctrl0 == ctrl1 and d_ctrl0 == d_ctrl1 else "FAIL",
               "wake_seeds_stored_mass": "PASS" if "error" not in wake and abs(wake["seeded_mass"] - stored_before_wake) <= pred["wake_seed_equals_stored_tol"] else "FAIL",
               "wake_sleep_round_trip": "PASS" if "error" not in wake and wake.get("sleep_ok") and abs((wake.get("stored_after") or 0) - stored_before_wake) <= 1e-4 else "FAIL"}
    return pred, meas, verdict


def s2_trough(api, gdef, args):
    """S2 on the Small trough rig: pump 0.1 m^3/s into trough A (2 m^3, rim at the pad) and, as the
    control, into trough B (6 m^3). Prediction: A is full at 20 s (+- 2 s), then overflows onto the
    pad so that pad film = pumped - 2.0 (+- 0.1 at 40 s); B holds 4.0 +- 0.01 at 40 s with 0 on the
    pad; every ledger closes: volume mass = pumped - unplaced +- 1e-4. Core only; simulation time."""
    rig = rig_named(gdef, "trough")
    sc = rig["scenarios"]["S2"]
    ta, tb, rate = sc["trough"], sc["control"], sc["pump_rate"]
    pad_top = rig["slab"]["y"][1]
    eng = Engine(api, args.engine, cell_size=args.cell_size, transport=args.transport, backend=args.backend, sweeps=args.sweeps)
    eng.require_core("S2")
    dur = args.duration if args.duration != 12.0 else 40.0
    capA, capB = 2.0, 6.0
    pred = {"engine": "core", "transport": eng.transport, "backend": eng.backend, "gpu_sweeps": eng.sweeps, "cell_size": eng.cell_size, "time_base": "simulation", "rate_m3s": rate, "duration_s": dur,
            "capacity_A": capA, "capacity_B": capB, "t_full_A": capA / rate, "t_full_tolerance_s": 2.0,
            "pumped_at_end": rate * dur, "overflow_A_at_end": max(0.0, rate * dur - capA), "overflow_tolerance": 0.1,
            "B_at_end": min(capB, rate * dur), "B_tolerance": 0.01, "ledger_tolerance": 1e-4}
    boxA = (ta[0] - 6, pad_top, ta[1] - 6, ta[2] + 6, pad_top + 2, ta[3] + 6)
    boxB = (tb[0] - 6, pad_top - 2, tb[1] - 6, tb[2] + 6, pad_top + 2, tb[3] + 6)
    camera_set(api, vantage(gdef, "trough"))
    vA = eng.create_volume(boxA)
    vB = eng.create_volume(boxB)
    print(f"   volumes {vA['id']} ({vA['cells']} cells) and {vB['id']} ({vB['cells']} cells) at h={eng.cell_size:.4f}")
    eng.source(vA["id"], ta[0] + 0.5, pad_top + 0.5, ta[1] + 0.5, rate)
    eng.source(vB["id"], tb[0] + 0.5, pad_top + 0.5, tb[1] + 0.5, rate)
    samples = []
    t_full = None
    while eng.now() < dur:
        eng.advance(args.dt)
        t = eng.now()
        a, b = eng.volume(vA["id"]), eng.volume(vB["id"])
        inA = eng.rect_mass(ta[0], ta[1], ta[2], ta[3], pad_top, pad_top)          # the trough cells only
        inB = eng.rect_mass(tb[0], tb[1], tb[2], tb[3], pad_top - 2, pad_top)
        if t_full is None and inA >= capA * 0.98:
            t_full = t
        samples.append({"t": round(t, 3), "A_total": a["mass"], "A_in_trough": inA, "A_on_pad": a["mass"] - inA,
                        "A_pumped": a["source_placed_m3"], "A_unplaced": a["last"]["source_unplaced"],
                        "B_total": b["mass"], "B_in_trough": inB, "B_pumped": b["source_placed_m3"]})
    eng.clear_sources(vA["id"]); eng.clear_sources(vB["id"])
    last = samples[-1]
    meas = {"t_full_A": t_full, "A_in_trough_end": last["A_in_trough"], "A_on_pad_end": last["A_on_pad"],
            "A_pumped_end": last["A_pumped"], "A_ledger_gap": last["A_total"] - last["A_pumped"],
            "B_in_trough_end": last["B_in_trough"], "B_on_pad_end": last["B_total"] - last["B_in_trough"],
            "B_ledger_gap": last["B_total"] - last["B_pumped"], "samples": samples}
    verdict = {"A_full_at_20s": "PASS" if t_full is not None and abs(t_full - pred["t_full_A"]) <= pred["t_full_tolerance_s"] else "FAIL",
               "A_overflow_exact": "PASS" if abs(meas["A_on_pad_end"] - pred["overflow_A_at_end"]) <= pred["overflow_tolerance"] else "FAIL",
               "ledgers_close": "PASS" if abs(meas["A_ledger_gap"]) <= pred["ledger_tolerance"] and abs(meas["B_ledger_gap"]) <= pred["ledger_tolerance"] else "FAIL",
               "control_B_no_overflow": "PASS" if abs(meas["B_in_trough_end"] - pred["B_at_end"]) <= pred["B_tolerance"] and abs(meas["B_on_pad_end"]) <= pred["B_tolerance"] else "FAIL"}
    if not args.keep_volume:
        eng.destroy_volume()
    return pred, meas, verdict


def basin_fill_volume(a, y_top):
    """m^3 the Basin rig holds up to and including water cells at y_top (floor tops per column)."""
    z0, z1 = a["z"]
    width = z1 - z0 + 1
    v = 0.0
    parts = [(a["flat"]["x"], a["flat"]["floorTop"])] + [(r["x"], r["floorTop"]) for r in a["ramp"]]
    for xs, floor in parts:
        v += (xs[1] - xs[0] + 1) * width * max(0, y_top - floor)
    return v


def s4_spill(api, gdef, args):
    """S4 on the Basin rig: fill basin A 3 deep (surface 16.0, one metre over the channel sill at
    15.0) and let it spill through the 2-wide, 5-long channel into the dry control basin B.
    Prediction (broad-crested weir, Q = 1.705 b H^1.5, A's area 276 m^2 for 15 < level < 16):
    H(t) = 1 / (1 + 0.00618 t)^2, so 79.5 m^3 (+- 25 %) have crossed by 30 s; total mass exact;
    control: fill 2 deep over the flat (408 m^3, level 14.85 < sill) and nothing crosses."""
    spec = gdef["waterBench"]
    a, ch, b = spec["basinA"], spec["channel"], spec["basinB"]
    eng = Engine(api, args.engine, cell_size=args.cell_size, transport=args.transport, backend=args.backend, sweeps=args.sweeps)
    eng.require_core("S4")
    dur = args.duration if args.duration != 12.0 else 30.0
    floor_top = a["flat"]["floorTop"]
    z0, z1 = a["z"]
    sill = ch["floorTop"] + 1.0                         # 15.0
    area = (a["x"][1] - a["x"][0] + 1) * (z1 - z0 + 1)  # 312 above y 16; 276 for 15..16 (x 3..5 dry)
    area_15_16 = area - 3 * (z1 - z0 + 1)
    b_width = ch["x"][1] - ch["x"][0] + 1
    k = 1.705 * b_width / area_15_16
    H0 = 1.0
    H_end = H0 / (1.0 + 0.5 * k * (H0 ** 0.5) * dur) ** 2
    pred = {"engine": "core", "transport": eng.transport, "backend": eng.backend, "gpu_sweeps": eng.sweeps, "cell_size": eng.cell_size, "time_base": "simulation", "duration_s": dur,
            "mass_main": basin_fill_volume(a, floor_top + 3), "mass_control": 2.0 * 17 * (z1 - z0 + 1),   # the control fills the flat floor only
            "sill_y": sill, "weir_coefficient": 1.705, "channel_width": b_width, "area_15_16": area_15_16,
            "H_end_weir": H_end, "spilled_end_weir": area_15_16 * (H0 - H_end), "spill_tolerance": 0.25,
            "mass_tolerance": 1e-3}
    box = (a["x"][0], floor_top + 1, z0, a["x"][1], 17, b["z"][1])
    camera_set(api, vantage(gdef, "rig_overview"))

    def run(fill_box, label):
        v = eng.create_volume(box)
        print(f"   {label}: volume {v['id']} ({v['cells']} cells) at h={eng.cell_size:.4f}")
        r = eng.place_box(*fill_box, 1.0)
        rows = []
        t_local = 0.0
        while t_local < dur:
            eng.advance(args.dt)
            t_local += args.dt
            vol = eng.volume(v["id"])
            inA = eng.rect_mass(a["x"][0], z0, a["x"][1], z1)
            inCh = eng.rect_mass(ch["x"][0], ch["z"][0], ch["x"][1], ch["z"][1])
            inB = eng.rect_mass(b["x"][0], b["z"][0], b["x"][1], b["z"][1])
            pr = api.debug("water_probe_rect", {"x1": a["flat"]["x"][0], "z1": z0, "x2": a["flat"]["x"][1], "z2": z1, "target": "core"})
            surf = [c[2] for c in pr["columns"] if c[2] is not None]
            rows.append({"t": round(t_local, 3), "A": inA, "channel": inCh, "B": inB, "total": vol["mass"],
                         "A_surface_mean": sum(surf) / len(surf) if surf else None})
        eng.destroy_volume()
        return r, rows

    rM, main = run((a["x"][0], floor_top + 1, z0, a["x"][1], floor_top + 3, z1), "main")
    rC, ctrl = run((a["flat"]["x"][0], floor_top + 1, z0, a["flat"]["x"][1], floor_top + 2, z1), "control")
    lm, lc = main[-1], ctrl[-1]
    meas = {"placed_main": rM.get("core_total_mass"), "placed_control": rC.get("core_total_mass"),
            "spilled_end": lm["channel"] + lm["B"], "B_end": lm["B"], "A_surface_end": lm["A_surface_mean"],
            "total_end": lm["total"], "mass_drift": lm["total"] - pred["mass_main"],
            "control_crossed": lc["channel"] + lc["B"], "control_drift": lc["total"] - pred["mass_control"],
            "samples": main, "control_samples": ctrl}
    verdict = {"spill_matches_weir": "PASS" if abs(meas["spilled_end"] - pred["spilled_end_weir"]) <= pred["spill_tolerance"] * pred["spilled_end_weir"] else "FAIL",
               "mass": "PASS" if abs(meas["mass_drift"]) <= pred["mass_tolerance"] and abs(meas["control_drift"]) <= pred["mass_tolerance"] else "FAIL",
               "control_no_crossing": "PASS" if meas["control_crossed"] <= pred["mass_tolerance"] else "FAIL"}
    return pred, meas, verdict


def s5_drain(api, gdef, args):
    """S5 on the Basin rig: basin A 2 deep (408 m^3); carve a 4x4x3 sealed cavity under the floor and a
    1x1 hole into it. Prediction: the cavity fills to 48 +- 0.5 m^3 within 20 s (Torricelli through 1 m^2
    at ~2.8 m head: ~6 m^3/s), the basin keeps 360 and its surface drops 0.20 +- 0.01 m (14.85 -> 14.65);
    total mass exact; control: the cavity with no hole stays dry. The rig is restored afterwards."""
    spec = gdef["waterBench"]
    a = spec["basinA"]
    eng = Engine(api, args.engine, cell_size=args.cell_size, transport=args.transport, backend=args.backend, sweeps=args.sweeps)
    eng.require_core("S5")
    dur = args.duration if args.duration != 12.0 else 20.0
    floor_top = a["flat"]["floorTop"]
    z0, z1 = a["z"]
    cav = {"x1": 18, "y1": floor_top - 3, "z1": 13, "x2": 21, "y2": floor_top - 1, "z2": 16}
    hole = {"x1": 20, "y1": floor_top, "z1": 15, "x2": 20, "y2": floor_top, "z2": 15}
    cav_vol = 4 * 4 * 3
    flat_area, ramp9_area = 17 * (z1 - z0 + 1), 3 * (z1 - z0 + 1)
    mass0 = 2.0 * flat_area                 # the fill covers the flat floor only (408 m^3; level 14.85 < sill)
    def level(v):   # still level of basin A for v m^3 (flat 12..28 from 13.0; ramp x 9..11 from 14.0)
        if v <= flat_area:
            return floor_top + 1 + v / flat_area
        return floor_top + 2 + (v - flat_area) / (flat_area + ramp9_area)
    pred = {"engine": "core", "transport": eng.transport, "backend": eng.backend, "gpu_sweeps": eng.sweeps, "cell_size": eng.cell_size, "time_base": "simulation", "duration_s": dur,
            "mass_initial": mass0, "cavity_volume": cav_vol, "cavity_tolerance": 0.5,
            "surface_before": level(mass0), "surface_after": level(mass0 - cav_vol),
            "surface_drop": level(mass0) - level(mass0 - cav_vol), "surface_tolerance": 0.01, "mass_tolerance": 1e-3}
    box = (a["x"][0], floor_top - 3, z0, a["x"][1], 17, z1)
    camera_set(api, vantage(gdef, "rig_overview"))

    def surface_mean():
        pr = api.debug("water_probe_rect", {"x1": a["flat"]["x"][0], "z1": z0, "x2": a["flat"]["x"][1], "z2": z1,
                                            "y1": floor_top + 1, "y2": 17, "target": "core"})
        surf = [c[2] for c in pr["columns"] if c[2] is not None]
        return sum(surf) / len(surf) if surf else None

    def run(with_hole, label):
        v = eng.create_volume(box)
        print(f"   {label}: volume {v['id']} ({v['cells']} cells) at h={eng.cell_size:.4f}")
        r = eng.place_box(a["flat"]["x"][0], floor_top + 1, z0, a["flat"]["x"][1], floor_top + 2, z1, 1.0)
        for _ in range(int(2.0 / args.dt)):      # settle 2 s before the dig
            eng.advance(args.dt)
        s_before = surface_mean()
        world_job(api, "/api/world/clear", cav)
        if with_hole:
            world_job(api, "/api/world/clear", hole)
        time.sleep(1.5)                          # occupancy upload + the volume's solids refresh
        occ = api.debug("water_av_probe", {"id": v["id"], "x": hole["x1"], "y": hole["y1"], "z": hole["z1"]})["occupancy"]
        rows = []
        t_local = 0.0
        while t_local < dur:
            eng.advance(args.dt)
            t_local += args.dt
            vol = eng.volume(v["id"])
            rows.append({"t": round(t_local, 3), "cavity": eng.rect_mass(cav["x1"], cav["z1"], cav["x2"], cav["z2"], cav["y1"], cav["y2"]),
                         "basin": eng.rect_mass(a["x"][0], z0, a["x"][1], z1, floor_top + 1, 17),
                         "total": vol["mass"], "A_surface_mean": surface_mean()})
        eng.destroy_volume()
        # restore the rig (the project DB is never saved by the harness)
        world_job(api, "/api/world/fill", dict(hole, material="Stone"))
        world_job(api, "/api/world/fill", dict(cav, material="Stone"))
        return r, s_before, occ, rows

    rM, sb, occM, main = run(True, "main")
    rC, sbc, occC, ctrl = run(False, "control")
    lm, lc = main[-1], ctrl[-1]
    t_full = next((x["t"] for x in main if x["cavity"] >= cav_vol - pred["cavity_tolerance"]), None)
    meas = {"placed": rM.get("core_total_mass"), "hole_occupancy_after_dig": occM, "surface_before": sb,
            # the basin is still sloshing gently at the end (14.649 at 12 s, 14.657 at 16 s on the 1 m
            # row): the end level is the mean over the last 5 s, not one sample
            "cavity_end": lm["cavity"], "t_cavity_full": t_full, "basin_end": lm["basin"],
            "surface_end": (lambda v: sum(v) / len(v) if v else None)([x["A_surface_mean"] for x in main if x["t"] >= dur - 5.0 and x["A_surface_mean"] is not None]),
            "surface_drop": (sb - lm["A_surface_mean"]) if (sb is not None and lm["A_surface_mean"] is not None) else None,
            "total_end": lm["total"], "mass_drift": lm["total"] - mass0,
            "control_cavity_end": lc["cavity"], "control_hole_occupancy": occC, "control_drift": lc["total"] - mass0,
            "samples": main, "control_samples": ctrl}
    verdict = {"cavity_fills": "PASS" if abs(meas["cavity_end"] - cav_vol) <= pred["cavity_tolerance"] else "FAIL",
               # judged on the END level: the 2 s settle before the dig is not long enough for the flat
               # fill to equalise over the ramp steps (14.91 read vs 14.85 still), so a "drop" mixes the
               # equalisation in; the end level is the still level of (mass - 48) over the real floor
               "surface_ends_at_level_minus_48": "PASS" if meas["surface_end"] is not None and abs(meas["surface_end"] - pred["surface_after"]) <= pred["surface_tolerance"] else "FAIL",
               "mass": "PASS" if abs(meas["mass_drift"]) <= pred["mass_tolerance"] and abs(meas["control_drift"]) <= pred["mass_tolerance"] else "FAIL",
               "control_sealed_cavity_dry": "PASS" if meas["control_cavity_end"] <= pred["mass_tolerance"] else "FAIL"}
    return pred, meas, verdict


def s12_shore(api, gdef, args):
    """S12 on the Coast shore (WaterCore Phase G, docs/WaterCore.md 18.5): the shoreline band around
    the shore_eye vantage, driven by the sheet's swell. Rows: (a) swell - rest-dry sand gets wet
    within 5 s (swash columns > 0), the swash surface peaks between 0.05 and 0.6 m above still, the
    band's mean level stays within 5 cm of still (no pump), speeds stay under 5 m/s, a 96x96 band
    steps in under 4 ms at the 95th percentile (the max is reported); (b) calm control - amplitude 0 for 20 s: foam dies (< 0.02), the swash
    surface falls to under half its swell-time peak (and foam appeared during the swell: G2); (c) persistence - with the band off the stored
    spans over the shelf are what they were before (the band writes nothing; nothing stays above the
    still line in the world). The Coast beach is a 1 m staircase with a 24 m shelf AT sea level, so
    the Hunt run-up comparison lives in the unit tests (ShoreSolverTest, ShoreBandTest), not here."""
    pose = vantage(gdef, "shore_eye")
    camera_set(api, pose); time.sleep(6)
    waves0 = api.debug("water_waves", {})
    radius = 48.0
    pred = {"swash_within_s": 5.0, "swash_eta_peak_range_m": [0.05, 0.6], "mean_free_rise_max_m": 0.05, "max_speed_max": 5.0,
            "step_ms_p95_max": 4.0, "swell_foam_min": 0.3, "calm_foam_max": 0.02, "calm_swash_fraction_max": 0.5, "spans_unchanged_tol": 1e-4, "radius": radius}
    shelf = {"x1": 150, "z1": 674, "x2": 180, "z2": 697}
    api.debug("water_shore", {"on": False})
    spans_before = api.debug("water_spans_stored", shelf)
    on = api.debug("water_shore", {"on": True, "radius": radius})
    if not on.get("active"):
        return pred, {"error": on.get("error", "band not active"), "status": on}, "FAIL"
    samples = []; first_swash = None; peak_eta = 0.0; rise_max = 0.0; speed_max = 0.0; step_max = 0.0; foam_peak = 0.0
    t0 = time.time()
    while time.time() - t0 < 20.0:
        time.sleep(0.25)
        st = api.debug("water_shore", {})
        t = time.time() - t0
        samples.append({"t": round(t, 2), **{k: st.get(k) for k in ("swash_columns", "swash_eta_m", "wet", "mass_m3", "exchanged_m3", "foam_max", "max_speed", "mean_free_rise_m", "step_ms", "substeps")}})
        if first_swash is None and st.get("swash_columns", 0) > 0: first_swash = t
        peak_eta = max(peak_eta, st.get("swash_eta_m", 0.0)); rise_max = max(rise_max, abs(st.get("mean_free_rise_m", 0.0)))
        speed_max = max(speed_max, st.get("max_speed", 0.0)); step_max = max(step_max, st.get("step_ms", 0.0)); foam_peak = max(foam_peak, st.get("foam_max", 0.0))
    swell = api.debug("water_shore", {})
    api.debug("water_waves", {"amplitude": 0.0}); time.sleep(20.0)
    calm = api.debug("water_shore", {})
    api.debug("water_waves", {"amplitude": waves0.get("amplitude", 0.45)})
    api.debug("water_shore", {"on": False})
    spans_after = api.debug("water_spans_stored", shelf)
    steps = sorted(x["step_ms"] for x in samples if x.get("step_ms") is not None)
    step_p95 = steps[min(len(steps) - 1, int(0.95 * len(steps)))] if steps else 0.0
    meas = {"band": {k: on.get(k) for k in ("n", "columns", "prescribed", "sponge", "walls", "free", "still", "centre", "box", "site_ms")},
            "first_swash_s": first_swash, "swash_eta_peak_m": peak_eta, "mean_free_rise_max_m": rise_max, "max_speed": speed_max, "step_ms_max": step_max, "step_ms_p95": step_p95, "foam_peak": foam_peak,
            "swell_end": {k: swell.get(k) for k in ("swash_columns", "swash_eta_m", "wet", "exchanged_m3", "foam_max", "runup_peak_m")},
            "calm_end": {k: calm.get(k) for k in ("swash_columns", "swash_eta_m", "wet", "exchanged_m3", "foam_max", "mean_free_rise_m")},
            "spans_before": {k: spans_before.get(k) for k in ("total_depth", "columns", "wet_columns", "max_top") if k in spans_before},
            "spans_after": {k: spans_after.get(k) for k in ("total_depth", "columns", "wet_columns", "max_top") if k in spans_after},
            "samples": samples}
    ok = (first_swash is not None and first_swash <= pred["swash_within_s"]
          and pred["swash_eta_peak_range_m"][0] <= peak_eta <= pred["swash_eta_peak_range_m"][1]
          and rise_max <= pred["mean_free_rise_max_m"] and speed_max <= pred["max_speed_max"] and step_p95 <= pred["step_ms_p95_max"]
          and foam_peak >= pred["swell_foam_min"] and calm.get("foam_max", 1.0) <= pred["calm_foam_max"] and calm.get("swash_eta_m", 1.0) <= pred["calm_swash_fraction_max"] * max(peak_eta, 1e-6)
          and abs((spans_after.get("total_depth") or 0.0) - (spans_before.get("total_depth") or 0.0)) <= pred["spans_unchanged_tol"])
    return pred, meas, "PASS" if ok else "FAIL"


SCENARIOS = {"S1": ("small", s1_pour), "S2": ("small", s2_trough), "S3": ("basin", s3_dam_break),
             "S4": ("basin", s4_spill), "S5": ("basin", s5_drain), "S11": ("small", s11_rest_persist),
             "R3": ("small", r3_edits_never_create), "S12": ("coast", s12_shore)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario")
    ap.add_argument("--bench")
    ap.add_argument("--engine", default="ca", choices=["ca", "core"])
    ap.add_argument("--duration", type=float, default=12.0, help="sampling window, s")
    ap.add_argument("--dt", type=float, default=0.1, help="sampling period, s")
    ap.add_argument("--url", default=os.environ.get("PHYXEL_API_URL"))
    ap.add_argument("--cell-size", dest="cell_size", type=float, default=1.0, help="core: 1, 0.3333, 0.1111")
    ap.add_argument("--keep-volume", dest="keep_volume", action="store_true", help="core: leave the volume alive (eyes-on)")
    ap.add_argument("--transport", default="eulerian", choices=["eulerian", "flip"], help="core: fill fractions (Phase B) or FLIP particles (Phase B2)")
    ap.add_argument("--backend", default="auto", choices=["auto", "cpu", "gpu"], help="core: auto = the engine's default (gpu for fills when ready), cpu = the reference, gpu = forced (parity rows)")
    ap.add_argument("--sweeps", type=int, default=0, help="gpu: red-black SOR sweeps per projection (0 = auto, 1.5 x the longest dimension; else 8-160)")
    ap.add_argument("--restart-cmd", dest="restart_cmd", default=None, help="S11: a shell command that stops and relaunches the bench engine (the cold-restart leg)")
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
    # no dots in the tag: Path.with_suffix() would treat "h0.3333_<stamp>" as the suffix and every
    # fine-grid run would overwrite "S3_basin_core_h0.json" (it did, 2026-10-08)
    per = int(round(1.0 / args.cell_size))
    tag = (f"{args.engine}_h1-{per}" if per > 1 else f"{args.engine}_h1") if args.engine == "core" else args.engine
    if args.engine == "core" and args.transport != "eulerian":
        tag += f"_{args.transport}"
    if args.engine == "core" and args.backend != "cpu":
        tag += f"_{args.backend}"   # "auto" rows are tagged _auto: the record's backend field says what ran
        if args.sweeps > 0:
            tag += f"_s{args.sweeps}"
    base = EVID / f"{args.scenario}_{bench}_{tag}_{stamp}"
    png = capture(api, base.with_suffix(".png"))
    row = {"scenario": args.scenario, "bench": bench, "engine": args.engine, "build_config": status.get("build_config"),
           "git_head": git_head(), "timestamp": stamp, "prediction": pred,
           "measurement": {k: v for k, v in meas.items() if k not in ("samples", "control_samples")},
           "samples": meas.get("samples"), "control_samples": meas.get("control_samples"),
           "verdict": verdict, "capture": png}
    base.with_suffix(".json").write_text(json.dumps(row, indent=1), encoding="utf-8")
    print(json.dumps({"prediction": pred, "measurement": row["measurement"], "verdict": verdict}, indent=1))
    print("evidence ->", base.with_suffix(".json").relative_to(ROOT))


if __name__ == "__main__":
    main()
