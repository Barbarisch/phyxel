"""WaterCore 37 (plan A2e) - a dry room under a recorded pond is DRY to debris.

The debris fallback (solver_integrate.comp, the table law) read one number per world column - the top of the column's highest
water run - so a body anywhere under a pond read as under water. Since 37 a body reads the run it is IN (the highest run whose
bottom is below the body's top).

The rig (Small bench, port 8111): the 4 x 4 m pond (floor top y 15, level 16.5) slept into the record as shipped. Two sealed rooms
are carved in the ground, 2 x 3 x 2 voxels (x 101..102 / 109..110, y 10..12, z 13..14): one UNDER the pond (two voxels of floor
between, y 13..14), one 8 m away under nothing (the control). One STONE (1/3 m, buoyancy < 1 - nothing in this rig can rise; owner
2026-10-25) is released at rest in each room at y 12.75 and falls to the room floor (y 10, the top of voxel 9).

Prediction, written before the run (2026-10-25):
  - RED (the A2d binary): the stone under the pond reads the pond's level - buoyed (g (1 - b)) and water-dragged - and takes
    clearly longer to fall than the control (free fall 2.58 m: ~0.73 s);
  - GREEN: the two fall times agree within a frame or two;
  - both: the stone never rises above its release height; the pond stays in the record (the rooms are under its box).
The rooms are filled back with the material scanned out of them at the end (also on failure); the engine should then be KILLED
(not closed), so nothing of the rig is saved.

    python tools/water_a2e_dryroom.py [--url http://127.0.0.1:8111] [--tag red]
Writes docs/evidence/water_core_m4/a2e_<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, world_job, particle_tracks
import water_float_rest as w

ROOMS = {"under_pond": (101, 102), "control": (109, 110)}
Y0, Y1, Z0, Z1 = 10, 12, 13, 14   # the bench ground is y 9..14 (empty below 9): floor y 9, roof y 13..14
SIZE = 1.0 / 3.0
REL_Y = 12.75


def voxel(api, x, y, z):
    return api.get(f"/api/world/voxel?x={x}&y={y}&z={z}")


def room_cells(x0, x1):
    return [(x, y, z) for x in range(x0, x1 + 1) for y in range(Y0, Y1 + 1) for z in range(Z0, Z1 + 1)]


def read_stone(path):
    """[(t, y)] for the ONE released stone, on the solver's clock (water_bench.particle_tracks: the log's snap_ticks, A2j - the
    37 runs used the spawn age, flags >> 16, which agreed while under 255 ticks). The track that starts at the release height."""
    for tr in particle_tracks(path).values():
        k = next((i for i, r in enumerate(tr) if abs(r[1] - REL_Y) < 0.02), None)
        if k is not None: return [(r[0] - tr[k][0], r[1]) for r in tr[k:]]
    return []


def fall(api, name, x, z, seconds=4.0):
    api.debug("clear_dynamics")
    time.sleep(0.5)
    log = (w.EV / f"a2e_{name}.csv").resolve()
    api.debug("particle_log", {"action": "start", "file": str(log)})
    r = api.debug("spawn_gpu_particle", {"x": x, "y": REL_Y, "z": z, "material": "Stone", "scale": SIZE, "lifetime": 30.0, "spin": 0})
    if not r.get("success"): raise SystemExit(f"spawn: {r}")
    time.sleep(seconds)
    api.debug("particle_log", {"action": "stop"})
    rows = [(t, y, 0.0, False) for t, y in read_stone(log)]
    api.debug("clear_dynamics")
    if not rows: raise SystemExit(f"{name}: no particle rows for the released stone (log kept: {log})")
    log.unlink(missing_ok=True)
    t0 = rows[0][0]
    land = next((t - t0 for t, y, _, _ in rows if y < Y0 + SIZE / 2 + 0.05), None)
    half = next((t - t0 for t, y, _, _ in rows if y < REL_Y - 1.5), None)
    return {"rows": len(rows), "y_first": rows[0][1], "y_max": max(r[1] for r in rows), "y_last": rows[-1][1],
            "t_fall_1_5m_s": half, "t_land_s": land, "trace": [[round(t - t0, 3), round(y, 4)] for t, y, _, _ in rows[::5]]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--tag", default="now")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    w.AUTO_SLEEP = True; w.DISPLACE = False                     # the shipped settings
    w.pond(api)
    t = time.time()
    while api.debug("water_av_list").get("volumes") and time.time() - t < 60: time.sleep(0.5)
    if api.debug("water_av_list").get("volumes"): raise SystemExit("the pond did not sleep into the record in 60 s")
    rect = {"x1": 100, "z1": 12, "x2": 103, "z2": 15}
    spans0 = api.debug("water_spans_stored", rect)
    saved = {}
    out = {"tag": a.tag, "spans_before": {k: spans0.get(k) for k in ("span_count", "sum_depth", "max_top", "min_top")}}
    try:
        for name, (x0, x1) in ROOMS.items():
            mats = {}
            for c in room_cells(x0, x1) + [(x, y, z) for x in (x0, x1) for y in (Y0 - 1, Y1 + 1, Y1 + 2) for z in (Z0, Z1)]:
                v = voxel(api, *c)
                if not v.get("exists"): raise SystemExit(f"{name}: {c} is not solid ground ({v}) - the rig needs solid ground")
                mats[c] = v.get("material") or "Stone"
            saved[name] = {c: mats[c] for c in room_cells(x0, x1)}
            world_job(api, "/api/world/clear", {"x1": x0, "y1": Y0, "z1": Z0, "x2": x1, "y2": Y1, "z2": Z1})
            time.sleep(1.0)
            left = [c for c in room_cells(x0, x1) if voxel(api, *c).get("exists")]
            if left: raise SystemExit(f"{name}: {len(left)} voxels left in the room after the clear")
        spans1 = api.debug("water_spans_stored", rect)
        out["spans_after_carve"] = {k: spans1.get(k) for k in ("span_count", "sum_depth", "max_top", "min_top")}
        if api.debug("water_av_list").get("volumes"): raise SystemExit("carving woke the pond (a volume exists) - not the rig")
        for name, (x0, x1) in ROOMS.items():
            out[name] = fall(api, name, x0 + 1.0, Z0 + 1.0)
            out[name]["pond_volumes_after"] = len(api.debug("water_av_list").get("volumes", []))
            print(f"{name}: 1.5 m in {out[name]['t_fall_1_5m_s']} s, landed {out[name]['t_land_s']} s, "
                  f"y max {out[name]['y_max']:.4f} (released {out[name]['y_first']:.4f}), rest {out[name]['y_last']:.4f}", flush=True)
    finally:
        for name, cells in saved.items():
            for (x, y, z), m in cells.items():
                api.post("/api/world/voxel", {"x": x, "y": y, "z": z, "material": m})
        if saved:
            time.sleep(1.0)
            out["restored_missing"] = sum(1 for cells in saved.values() for c in cells if not voxel(api, *c).get("exists"))
    u, c = out.get("under_pond", {}), out.get("control", {})
    rose = any(r.get("y_max", 0) > REL_Y + 1e-3 for r in (u, c))
    same = (u.get("t_land_s") is not None and c.get("t_land_s") is not None and abs(u["t_land_s"] - c["t_land_s"]) <= 0.05)
    out["verdict"] = "ROSE (never acceptable)" if rose else ("GREEN: the dry room is dry" if same else "RED: the room under the pond is not dry")
    print(out["verdict"])
    (w.EV / f"a2e_{a.tag}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
    print("wrote", w.EV / f"a2e_{a.tag}.json")


if __name__ == "__main__":
    main()
