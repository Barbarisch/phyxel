#!/usr/bin/env python3
"""Debris settling benchmark: proves (and later disproves) the "bubbling crater" bug.

docs/DebrisSettlingPlan.md §0. Runs fixed GPU-debris scenarios in the DebrisLab project and
measures them with the engine's settle probe (POST /api/debug/settle_probe), which analyses
EVERY physics tick:

  * injected energy: total mechanical energy of a closed pile can only fall; any rise is
    energy the solver created. Reported as an equivalent lift of the whole pile (mm).
  * rebounds: a body that was not rising and is now rising > 0.3 m/s was launched by a
    contact. Rubble rebounds a few times; a bubbling pile keeps rebounding after impact.
  * settle time, clean vs FORCE-frozen sleeps, wakes, tunnelling through the floor, and the
    solver's silent failures (colour-skipped bodies, dropped constraints, hard-contact pushes).

Scenarios (each is one variable away from its control; one 32-voxel chunk each in DebrisLab):
  chunk 0  drop_layer      CONTROL  36 separated cubes dropped 2 m (no packing, no walls)
  chunk 1  drop_pile                loose 5x6x5 drop (the 2026-07-31 sleep-verification case)
  chunk 2  packed                   6x6x6 cubes spawned touching, at rest, on flat ground
  chunk 3  crater                   same packing inside a 6x4x6 pit (adds static walls)
  chunk 4  crater_subcube           4x3x4 pit filled with touching 1/3-scale debris
  chunk 5  blast                    real /api/damage/apply blast (DamageSystem debris)

The simulation is driven by STEPPING (POST /api/debug/gpu_physics): physics is frozen, the
scene is spawned, and exactly --seconds of sim time is advanced. Results do not depend on how
fast the (Debug) engine renders, and --frames captures land on exact ticks.

Usage:
  # once: author the lab terrain into DebrisLab/worlds/default.db (engine running DebrisLab)
  python tools/debris_settle_bench.py --build-lab
  # every run (exit code 0 = every scenario SETTLES — the done-gate for the fixes)
  python tools/debris_settle_bench.py [--only packed crater] [--seconds 8] [--tag T] [--frames]
Writes docs/evidence/debris_settle/<tag>/<scenario>.{json,csv}, summary.json, frames/.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TICK = 1.0 / 60.0

# ---- lab layout (DebrisLab project) ----
SLAB_Y0, SLAB_Y1 = 8, 15          # solid Stone; top face at y = 16
GROUND = SLAB_Y1 + 1
LAB_CHUNKS = 6
SITE_Z = 13                        # every site's min-z corner (chunk z 0..31)


def site_x(chunk):                 # min-x corner of a scenario's footprint
    return 32 * chunk + 13


CRATER = dict(chunk=3, w=6, depth=4)          # pit x0..x0+5, z 13..18, y 12..15
CRATER_SUB = dict(chunk=4, w=4, depth=3)      # pit x0..x0+3, z 13..16, y 13..15
BLAST_CHUNK = 5


class Api:
    def __init__(self, url: str):
        self.url = url.rstrip("/")

    def _req(self, method, path, body=None, timeout=60):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.url + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode() or "{}")

    def post(self, path, body=None, timeout=60):
        return self._req("POST", path, body if body is not None else {}, timeout)

    def get(self, path, timeout=60):
        return self._req("GET", path, None, timeout)

    def wait_loop(self, max_s=300):
        """Block until the game loop answers queued commands (Debug remesh can stall it)."""
        t0 = time.time()
        while time.time() - t0 < max_s:
            try:
                r = self.post("/api/debug/gpu_physics", {})
                if r.get("success"):
                    return r
            except Exception:
                pass
            time.sleep(2.0)
        raise RuntimeError("game loop did not respond")


def ground_top(api, x, z):
    r = api.get(f"/api/world/terrain_height?x={x}&z={z}")
    if r.get("surface_y") is None:
        raise RuntimeError(f"no terrain at ({x},{z}): {r}")
    return int(r["surface_y"]) + 1


def world_job(api, path, box, **extra):
    """/api/world/fill and /api/world/clear are async: wait for the job, verify the count."""
    body = dict(box, **extra)
    r = api.post(path, body)
    aid, res = r.get("async_id"), r
    for _ in range(600):
        if aid is None:
            break
        res = api.get(f"/api/async/{aid}")
        if res.get("status") not in ("pending", "running", "accepted", "processing", "queued"):
            break
        time.sleep(0.5)
    api.wait_loop()
    return res.get("result") or res


def lattice(api, **kw):
    want = kw.get("nx", 4) * kw.get("ny", 4) * kw.get("nz", 4)
    r = api.post("/api/debug/spawn_gpu_lattice", kw)
    if r.get("success"):
        return r
    if "timed out" not in str(r.get("error", "")):
        raise RuntimeError(f"spawn_gpu_lattice failed: {r}")
    for _ in range(60):          # the queued command still runs; never re-send (double spawn)
        time.sleep(1.0)
        if api.get("/api/debug/dynamic_stats").get("gpu_active", 0) >= want:
            return {"success": True, "spawned": want, "late": True}
    raise RuntimeError(f"spawn_gpu_lattice never landed ({want} bodies): {r}")


def physics(api, **kw):
    return api.post("/api/debug/gpu_physics", kw)


def step_until(api, ticks_from_start, start_total):
    """Advance the frozen solver until exactly `ticks_from_start` ticks have run."""
    while True:
        st = physics(api)
        done = st["total_ticks"] - start_total
        if done >= ticks_from_start and st["pending_steps"] == 0:
            return done
        if st["pending_steps"] == 0:
            physics(api, step=min(ticks_from_start - done, 240))
        time.sleep(0.05)


def screenshot(api, dst):
    for attempt in range(6):
        try:
            r = api.get("/api/screenshot", timeout=90)
            path = r.get("path") or next((s.get("path") for s in r.get("screenshots", [])), None)
            if path and not os.path.isabs(path):
                path = os.path.join(ROOT, path)
            if path and os.path.exists(path):
                shutil.copy(path, dst)
                return dst
        except Exception as e:
            print(f"   screenshot retry {attempt}: {e}")
        time.sleep(3)
    return None


# ---- lab authoring (once) ----

def build_lab(api):
    print("filling slab ...")
    x1 = 32 * LAB_CHUNKS - 1
    # 100k-voxel cap per job: one chunk-wide strip at a time.
    for c in range(LAB_CHUNKS):
        r = world_job(api, "/api/world/fill",
                      {"x1": 32 * c, "y1": SLAB_Y0, "z1": 0, "x2": 32 * c + 31, "y2": SLAB_Y1, "z2": 31},
                      material="Stone")
        print(f"   chunk {c}: {json.dumps(r)[:160]}")
    for spec in (CRATER, CRATER_SUB):
        x0, w, d = site_x(spec["chunk"]), spec["w"], spec["depth"]
        r = world_job(api, "/api/world/clear",
                      {"x1": x0, "y1": GROUND - d, "z1": SITE_Z,
                       "x2": x0 + w - 1, "y2": GROUND - 1, "z2": SITE_Z + w - 1})
        print(f"   pit chunk {spec['chunk']}: {json.dumps(r)[:160]}")
    print("saving ...", api.post("/api/world/save", {"all": True}, timeout=120))
    verify_lab(api)


def verify_lab(api):
    """The world, not the API response: probe the slab and both pits."""
    bad = []
    for c in range(LAB_CHUNKS):
        g = ground_top(api, 32 * c + 3, 3)
        if g != GROUND:
            bad.append(f"chunk {c} ground {g} != {GROUND}")
    for spec in (CRATER, CRATER_SUB):
        x0 = site_x(spec["chunk"])
        floor = ground_top(api, x0 + 1, SITE_Z + 1)
        if floor != GROUND - spec["depth"]:
            bad.append(f"pit chunk {spec['chunk']} floor {floor} != {GROUND - spec['depth']}")
    # The WHOLE slab top of every non-blast chunk, not one probe column: on 2026-10-04 stray
    # apply_damage/spell tests left 39 holes under drop_layer, the single probe at (3, 3) never
    # saw them, and the bench reported 10-12 bodies "tunnelling" through the floor. The blast
    # chunk is excluded: run() restores it right before its own scenario and leaves it cratered.
    pits = {spec["chunk"]: spec for spec in (CRATER, CRATER_SUB)}
    for c in range(LAB_CHUNKS):
        if c == BLAST_CHUNK:
            continue
        r = api.get(f"/api/world/scan?x1={32 * c}&y1={SLAB_Y1}&z1=0&x2={32 * c + 31}&y2={SLAB_Y1}&z2=31",
                    timeout=120)
        solid = {(v["x"], v["z"]) for v in r.get("voxels", [])}
        expect = {(x, z) for x in range(32 * c, 32 * c + 32) for z in range(32)}
        if c in pits:
            p = pits[c]
            x0 = site_x(c)
            expect -= {(x, z) for x in range(x0, x0 + p["w"]) for z in range(SITE_Z, SITE_Z + p["w"])}
        missing = sorted(expect - solid)
        if missing:
            bad.append(f"chunk {c}: {len(missing)} slab-top cells missing at y={SLAB_Y1} "
                       f"(first {missing[:6]})")
    if bad:
        raise RuntimeError("lab terrain wrong: " + "; ".join(bad) +
                           " -- repair with: python tools/debris_settle_bench.py --build-lab")
    print("lab terrain verified: slab top", GROUND, "pits at depth",
          CRATER["depth"], CRATER_SUB["depth"])


def restore_blast_site(api):
    x0 = 32 * BLAST_CHUNK
    # Clear ABOVE the slab first: anything other tests built on the blast chunk's surface (the
    # sub-voxel rest check's 1/3 slab, 2026-10-05) used to survive and sit in the crater's path,
    # quietly changing the blast scenario for every later run.
    world_job(api, "/api/world/clear",
              {"x1": x0, "y1": GROUND, "z1": 0, "x2": x0 + 31, "y2": GROUND + 15, "z2": 31})
    world_job(api, "/api/world/fill",
              {"x1": x0, "y1": SLAB_Y0, "z1": 0, "x2": x0 + 31, "y2": SLAB_Y1, "z2": 31},
              material="Stone")


# ---- scenarios: (chunk, setup(api, x, z) -> description, floor_y) ----

def s_drop_layer(api, x, z):
    lattice(api, x=x, y=GROUND + 2.0, z=z, nx=6, ny=1, nz=6, scale=1.0, gap=0.6, seed=1)
    return "6x1x6 cubes, gap 0.6, dropped 2 m onto flat ground"


def s_drop_pile(api, x, z):
    lattice(api, x=x, y=GROUND + 3.0, z=z, nx=5, ny=6, nz=5, scale=1.0, gap=0.1, spin=3.0, seed=2)
    return "5x6x5 cubes, gap 0.1, spin<=3 rad/s, dropped 3 m"


def s_packed(api, x, z):
    lattice(api, x=x, y=GROUND + 0.002, z=z, nx=6, ny=6, nz=6, scale=1.0, gap=0.0, seed=3)
    return "6x6x6 cubes touching, zero velocity, resting on flat ground"


def s_crater(api, x, z):
    w, d = CRATER["w"], CRATER["depth"]
    lattice(api, x=x, y=GROUND - d + 0.002, z=z, nx=w, ny=d, nz=w, scale=1.0, gap=0.0, seed=4)
    return f"{w}x{d}x{w} pit filled with touching cubes at rest"


def s_crater_subcube(api, x, z):
    w, d = CRATER_SUB["w"], CRATER_SUB["depth"]
    lattice(api, x=x, y=GROUND - d + 0.002, z=z, nx=3 * w, ny=3 * d, nz=3 * w,
            scale=1.0 / 3.0, gap=0.0, seed=5)
    return f"{w}x{d}x{w} pit filled with touching 1/3-scale debris at rest"


def s_blast(api, x, z):
    r = api.post("/api/damage/apply", {"x": x + 3.5, "y": GROUND - 0.5, "z": z + 3.5,
                                       "radius": 3.5, "energy": 600.0, "type": "force"})
    return f"/api/damage/apply r=3.5 e=600 at ground -> {json.dumps(r)[:140]}"


def s_box_through_pile(api, x, z):
    # DebrisInteractionPlan 1f/Phase 2: a settled 6x3x6 pile; a scripted 1x2x1 box (half 0.5,1,0.5)
    # starts 3 m short of it and crosses at 2 m/s through the bottom two layers, then leaves.
    # Box motion is SIMULATED time, so the stepped solver drives it exactly. Until Phase 2 the box
    # rides the character-collider push (shove) path; the analyzer reports its pushes as DRIVEN.
    lattice(api, x=x, y=GROUND + 0.002, z=z, nx=6, ny=3, nz=6, scale=1.0, gap=0.0, seed=6)
    api.post("/api/debug/gpu_kinematic_box", {"id": "bench_box", "center": [x - 3.0, GROUND + 1.0, z + 3.0],
                                              "half": [0.5, 1.0, 0.5], "velocity": [2.0, 0.0, 0.0],
                                              "ttl": 6.0})
    return "6x3x6 touching pile; 1x2x1 kinematic box crosses at 2 m/s (enters ~1.25 s, leaves ~5.75 s)"


SCENARIOS = {
    "drop_layer":     (0, s_drop_layer, GROUND),
    "drop_pile":      (1, s_drop_pile, GROUND),
    "packed":         (2, s_packed, GROUND),
    "crater":         (3, s_crater, GROUND - CRATER["depth"]),
    "crater_subcube": (4, s_crater_subcube, GROUND - CRATER_SUB["depth"]),
    "blast":          (5, s_blast, GROUND - 5),
    # Opt-in (--only box_through_pile): not in the default regression band until Phase 2 gives
    # the box real AVBD contacts. Reuses the packed site (chunk 2); needs >= 10 s of sim time.
    "box_through_pile": (2, s_box_through_pile, GROUND),
}
OPT_IN = {"box_through_pile"}

# box_through_pile's mover, in site coordinates (x, z = the site's min corner): it starts at
# (x - 3, GROUND + 1, z + 3), half extents (0.5, 1, 0.5), moves +x at 2 m/s for 6 s (12 m).
BOX_START_DX, BOX_HALF, BOX_SPEED, BOX_TTL = -3.0, (0.5, 1.0, 0.5), 2.0, 6.0


def judge_box(summ, x, z):
    """Phase 2 'works': >= 80 % of the bodies in the box's swept volume end >= 0.2 m from where
    they started, and no body is ever deeper than 2 cm into the box at a tick start."""
    paths = summ.get("body_paths") or []
    x0 = x + BOX_START_DX - BOX_HALF[0]
    x1 = x + BOX_START_DX + BOX_SPEED * BOX_TTL + BOX_HALF[0]
    y0, y1 = GROUND, GROUND + 2.0 * BOX_HALF[1]
    z0, z1 = z + 3.0 - BOX_HALF[2], z + 3.0 + BOX_HALF[2]
    swept = displaced = 0
    for sx, sy, sz, ex, ey, ez in paths:
        # a unit body overlaps the swept box if its centre is within half a unit of it
        if x0 - 0.5 < sx < x1 + 0.5 and y0 - 0.5 < sy < y1 + 0.5 and z0 - 0.5 < sz < z1 + 0.5:
            swept += 1
            if ((ex - sx) ** 2 + (ey - sy) ** 2 + (ez - sz) ** 2) ** 0.5 >= 0.2:
                displaced += 1
    depth = (summ.get("driven") or {}).get("kinematic_max_depth_m", 0.0) or 0.0
    frac = displaced / swept if swept else 0.0
    box = {"swept": swept, "displaced": displaced, "displaced_fraction": frac,
           "kinematic_max_depth_m": depth,
           "pass": swept > 0 and frac >= 0.8 and depth <= 0.02}
    summ["box"] = box
    return box
MIN_SECONDS = {"box_through_pile": 10.0}
FRAME_TICKS = [0, 20, 60, 120, 240, 480]


def run(api, name, seconds, outdir, frames):
    chunk, setup, floor_y = SCENARIOS[name]
    seconds = max(seconds, MIN_SECONDS.get(name, 0.0))
    x, z = site_x(chunk), SITE_Z
    api.post("/api/debug/clear_dynamics", {})
    physics(api, frozen=True)
    if frames:
        api.post("/api/camera", {"position": {"x": x + 3.0, "y": GROUND + 6.0, "z": z + 15.0},
                                 "yaw": -90.0, "pitch": -30.0})
    api.post("/api/debug/settle_probe", {"op": "start", "floor_y": floor_y})
    desc = setup(api, x, z)
    print(f"\n== {name} (chunk {chunk}) @ ({x},{GROUND},{z}): {desc}")
    start = physics(api)["total_ticks"]
    total = int(round(seconds / TICK))
    marks = sorted(set([t for t in FRAME_TICKS if t <= total] if frames else []) |
                   set(range(0, total + 1, 120)) | {total})
    for m in marks:
        step_until(api, m, start)
        time.sleep(0.4)   # let the probe readback (2 frames behind) catch up
        st = api.post("/api/debug/settle_probe", {"op": "status", "series_last": 1})
        row = (st.get("series") or [{}])[-1]
        line = (f"   tick {m:4d} (t={m * TICK:5.2f}s) awake={row.get('awake', 0):5d} "
                f"maxv={row.get('max_speed', 0):6.2f} rebounds/tick={row.get('rebounds', 0):3d} "
                f"hc/tick={row.get('hardcontact', 0):4d} below_floor={row.get('below_floor', 0):3d}")
        if frames and m in FRAME_TICKS:
            os.makedirs(os.path.join(outdir, "frames"), exist_ok=True)
            dst = screenshot(api, os.path.join(outdir, "frames", f"{name}_t{m:04d}.png"))
            line += f"  frame={os.path.basename(dst) if dst else 'FAILED'}"
        print(line)
    csv = os.path.join(outdir, f"{name}.csv")
    st = api.post("/api/debug/settle_probe", {"op": "stop", "csv": csv, "bodies": 12})
    for b in st.get("awake_bodies", [])[:4]:
        print(f"   still awake: slot {b['slot']} y={b['pos'][1]:.3f} "
              f"vel=({b['vel'][0]:.3f},{b['vel'][1]:.3f},{b['vel'][2]:.3f}) tilt={b['tilt_deg']:.1f} "
              f"cons={b['constraints']} rebounds={b['rebounds']}")
    summ = st["summary"]
    summ.update(scenario=name, setup=desc, site={"x": x, "z": z, "ground_top": GROUND},
                sim_seconds=seconds)
    if name == "box_through_pile":
        box = judge_box(summ, x, z)
        print(f"   box: {box['displaced']}/{box['swept']} swept bodies displaced >= 0.2 m "
              f"({100 * box['displaced_fraction']:.0f} %), max depth into the box "
              f"{1000 * box['kinematic_max_depth_m']:.1f} mm -> {'PASS' if box['pass'] else 'FAIL'}")
    # 1c step 6: the scenario's chunk must agree across store / physics grid / packed pool
    # (what debris collided with must be what was placed). Judged BEFORE the blast-site restore.
    x0 = 32 * chunk
    occ = api.post("/api/debug/occupancy_diff", {"x1": x0, "y1": 0, "z1": 0,
                                                 "x2": x0 + 31, "y2": 31, "z2": 31})
    summ["occupancy_diff"] = occ
    occ_bad = (occ.get("cell_mismatches", 1) + occ.get("grid_mismatches", 1) +
               occ.get("cells_pool_unknown", 1))
    if occ_bad:
        summ["verdict"] = "FAILS(occupancy)"
        print(f"   occupancy_diff: {json.dumps({k: occ.get(k) for k in ('cell_mismatches', 'grid_mismatches', 'cells_pool_unknown', 'first_mismatches', 'error')})}")
    with open(os.path.join(outdir, f"{name}.json"), "w") as f:
        json.dump(st, f, indent=2)
    api.post("/api/debug/clear_dynamics", {})
    api.post("/api/debug/gpu_kinematic_box", {"id": "bench_box", "remove": True})
    physics(api, frozen=False)
    if name == "blast":
        restore_blast_site(api)
    return summ


def fmt(v, nd=2):
    if v is None:
        return "never"
    if isinstance(v, float):
        return f"{v:.{nd}f}"
    return str(v)


def creep(sl):
    f = (sl.get("creep_before_sleep_mm") or {}).get("forced")
    return "-" if not f else f'{f["p90"]:.1f}/{f["max"]:.1f}'


def occ_cell(s):
    o = s.get("occupancy_diff") or {}
    if "cell_mismatches" not in o:
        return "ERR"
    return o["cell_mismatches"] + o["grid_mismatches"] + o["cells_pool_unknown"]


def table(results):
    hdr = ["scenario", "bodies", "verdict", "judged from s", "reb/body>win", "max reb", "inj>win mm",
           "t_all s", "forced", "forced creep p90/max mm", "woke", "skipColor", "hc>1s",
           "hc max mm", "tunnelled", "occ diff", "driven kin/hc"]
    rows = []
    for s in results:
        e, r, sl, so = s["energy"], s["rebounds"], s["sleep"], s["solver"]
        rows.append([s["scenario"], s["bodies"], s["verdict"],
                     fmt(s.get("criteria", {}).get("judged_from_s")), fmt(r["per_body_after_mean"]),
                     r["per_body_max"], fmt(e["injected_after_window_lift_m"] * 1000, 1),
                     fmt(s["time_all_asleep_s"]), f'{sl["forced"]}/{sl["clean"] + sl["forced"]}',
                     creep(sl), sl["woke"], so["max_color_skipped"], so["hardcontact_fires_after_window"],
                     fmt(so["hardcontact_max_depth_m"] * 1000, 1),
                     s["tunnelled"]["max_bodies_below_floor"], occ_cell(s),
                     f'{(s.get("driven") or {}).get("kinematic_contacts", 0)}/'
                     f'{(s.get("driven") or {}).get("hardcontact_fires_while_driven", 0)}'])
    w = [max(len(str(x)) for x in col) for col in zip(hdr, *rows)]
    line = lambda r: "  ".join(str(c).ljust(n) for c, n in zip(r, w))
    print("\n" + line(hdr))
    print("  ".join("-" * n for n in w))
    for r in rows:
        print(line(r))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default=os.environ.get("PHYXEL_API_URL", "http://localhost:8090"))
    ap.add_argument("--build-lab", action="store_true", help="author + save the DebrisLab terrain")
    ap.add_argument("--only", nargs="*", choices=list(SCENARIOS))
    ap.add_argument("--seconds", type=float, default=8.0, help="SIM seconds per scenario")
    ap.add_argument("--frames", action="store_true", help="capture frames at fixed ticks")
    ap.add_argument("--tag", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--flags", type=int, default=None,
                    help="solver fix switches for A/B (GpuParticlePhysics SOLVER_FLAG_*): "
                         "1 massPenalty, 2 startAtRest, 4 hard-contact neutral, 8 post-stab, "
                         "16 static friction, 32 kinematic contacts (Phase 2)")
    ap.add_argument("--cold-scale", type=float, default=None,
                    help="cold-contact stiffness multiplier (x m/dt^2) for A/B")
    args = ap.parse_args()

    api = Api(args.url)
    api.wait_loop()
    if args.flags is not None:
        print("solver flags ->", physics(api, flags=args.flags)["solver_flags"])
    if args.cold_scale is not None:
        print("cold scale ->", physics(api, cold_scale=args.cold_scale)["cold_scale"])
    if args.build_lab:
        build_lab(api)
        return 0
    verify_lab(api)
    outdir = os.path.join(ROOT, "docs", "evidence", "debris_settle", args.tag)
    os.makedirs(outdir, exist_ok=True)
    results = [run(api, n, args.seconds, outdir, args.frames) for n in (args.only or [k for k in SCENARIOS if k not in OPT_IN])]
    with open(os.path.join(outdir, "summary.json"), "w") as f:
        json.dump(results, f, indent=2)
    table(results)
    print(f"\nraw: {outdir}")
    return 0 if all(r["verdict"] == "SETTLES" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
