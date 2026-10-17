"""WaterCore.md 25.30 SC-T2 - a stone into the creek (WaterBench_Creek, the creek running at steady flow - `python
tools/water_creek_bench.py --run` first and give it ~2 minutes).

A Stone cube (1/3 m by default) is dropped from `--height` above the creek's free surface at the centre line. Polled every
~50 ms for `--watch` seconds: the creek's cosmetic splash droplets (born / landed / alive), the region's ripple layer (peak,
kinematic cells, impulses), the column under the stone (water surface, the body's load) and the region's ledger error.
`--forcing 0` (default) turns the flow's own ripples off so the stone's rings are what the ripple layer holds (the A/B
control: before the drop the layer is asleep); a clip is recorded from a camera beside the drop point.

Predictions (written before the first run, 2026-10-12): impact ~ sqrt(2 g h) = 5.4 m/s from 1.5 m; the crossing of 11 cm of
water takes a frame or two -> 5-30 droplets, each in flight <= 1.5 s; kinematic cells > 0 and the layer wakes; the
region's ledger error unchanged by the droplets (they carry no water).

    python tools/water_creek_splash.py [--x 40] [--size 0.3333] [--height 1.5] [--watch 4] [--forcing 0]
Writes docs/evidence/water_core_h1/splash_<tag>.json and splash_<tag>.mp4.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
ZC = 32.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--x", type=float, default=40.0)
    ap.add_argument("--size", type=float, default=1.0 / 3.0)
    ap.add_argument("--height", type=float, default=1.5)
    ap.add_argument("--watch", type=float, default=4.0)
    ap.add_argument("--forcing", type=float, default=0.0)
    ap.add_argument("--tag", default="stone")
    ap.add_argument("--pond", action="store_true", help="26.24: the creek as POND water - the only model after P3d")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(60)
    if args.pond: return main_pond(api, args)
    regs = api.debug("water_columns", {}).get("regions", [])
    if not regs or regs[0]["wet_columns"] < 200: raise SystemExit("start the creek first: python tools/water_creek_bench.py --run, and let it settle")
    rid = regs[0]["id"]
    EV.mkdir(parents=True, exist_ok=True)
    api.debug("water_coupling", {"enabled": True})
    api.debug("water_ripples", {"flow_forcing": args.forcing})
    zc = ZC - 0.5 + (0.5 / 3.0)   # a column centre near the line, so a 1/3 m stone covers one column
    xc = int(args.x * 3) / 3.0 + 1.0 / 6.0
    pr = api.debug("water_columns", {"id": rid, "probe": {"x": xc, "z": zc}})["probe"]
    surface = pr["surface_y"]
    camera_set(api, {"x": xc - 3.0, "y": surface + 1.6, "z": zc + 3.0, "yaw": -45.0, "pitch": -22.0})
    time.sleep(40.0 if args.forcing == 0 else 2.0)   # the layer sleeps before the drop (film damping ~20 s: 6 s left 30 mm, 2026-10-12)
    led0 = next(g for g in api.debug("water_columns", {})["regions"] if g["id"] == rid)["ledger_error_m3"]
    d0 = api.debug("water_droplets", {})
    rec = None
    if shutil.which("ffmpeg"):
        rec = subprocess.Popen([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.watch + 1.0),
                                "-i", "title=WaterBench_Creek", "-vf", "crop=960:630:240:42", "-c:v", "libx264", "-crf", "26", "-pix_fmt", "yuv420p",
                                str(EV / f"splash_{args.tag}.mp4")])
        time.sleep(0.5)
    api.debug("spawn_gpu_particle", {"x": xc, "y": surface + args.height + args.size / 2, "z": zc, "material": "Stone", "scale": args.size, "lifetime": 600.0})
    t0 = time.time(); rows = []
    while time.time() - t0 < args.watch:
        dr = api.debug("water_droplets", {}); rp = api.debug("water_ripples", {})
        p = api.debug("water_columns", {"id": rid, "probe": {"x": xc, "z": zc}})["probe"]
        g = next(x for x in api.debug("water_columns", {"id": rid, "wet_list": True})["regions"] if x["id"] == rid)
        wl = g.get("wet_list", [])
        top = max(wl, key=lambda c: c[4] if len(c) > 4 else 0.0) if wl else None
        near = [c for c in wl if len(c) > 5 and abs(c[0] - xc) <= 1.5 and abs(c[1] - zc) <= 1.5]   # foam around the drop only
        fo = max(near, key=lambda c: c[5]) if near else None
        rows.append({"t": round(time.time() - t0, 3), "cosmetic_births": dr["cosmetic_births"] - d0["cosmetic_births"],
                     "cosmetic_landings": dr["cosmetic_landings"] - d0["cosmetic_landings"], "alive": dr["alive"],
                     "ripple_max_mm": rp["max_abs_m"] * 1e3, "kinematic_cells": rp["kinematic_cells"], "awake": rp["awake"],
                     "depth_m": p.get("depth_m"), "surface": p.get("surface_y"), "ledger_error_m3": g["ledger_error_m3"],
                     "max_load_m": top[4] if top and len(top) > 4 else 0.0, "max_load_at": [top[0], top[1]] if top else None,
                     "max_load_depth_m": top[2] if top else None,
                     "max_foam": fo[5] if fo and len(fo) > 5 else 0.0, "max_foam_at": [fo[0], fo[1]] if fo else None})
    if rec: rec.wait()
    births = rows[-1]["cosmetic_births"]
    first_birth = next((r["t"] for r in rows if r["cosmetic_births"] > 0), None)
    all_landed = next((r["t"] for r in rows if r["cosmetic_births"] > 0 and r["cosmetic_landings"] >= r["cosmetic_births"]), None)
    summary = {"tag": args.tag, "surface_before": surface, "depth_before": pr.get("depth_m"),
               "droplets_born": births, "first_birth_t": first_birth, "all_landed_t": all_landed,
               "flight_upper_bound_s": (all_landed - first_birth) if (all_landed is not None and first_birth is not None) else None,
               "ripple_peak_mm": max(r["ripple_max_mm"] for r in rows), "kinematic_cells_max": max(r["kinematic_cells"] for r in rows),
               "max_load_m": max(r["max_load_m"] for r in rows), "max_foam": max(r["max_foam"] for r in rows), "load_cleared_t": next((r["t"] for r in rows[::-1] if r["max_load_m"] > 1e-4), None),
               "min_depth_at_drop_m": min((r["depth_m"] for r in rows if r["depth_m"] is not None), default=None),
               "ledger_error_before": led0, "ledger_error_after": rows[-1]["ledger_error_m3"]}
    (EV / f"splash_{args.tag}.json").write_text(json.dumps({"summary": summary, "rows": rows}, indent=1))
    print(json.dumps(summary, indent=1))


def main_pond(api, args):
    """26.24 P3d-1: the same drop into the POND creek. The pond's splash is REAL water (droplets that leave the volume and land
    back - births / landings / alive, and the volume's own book), not the columns' cosmetic crown; the gauges: the droplets, the
    volume's ripple layer, the drawn surface at the drop column, and the ledger (mass - (springs in - outfalls out)) before
    and after - a splash moves water, it never makes or loses it."""
    vols = api.debug("water_av_list").get("volumes", [])
    if not vols: raise SystemExit("start the creek first: python tools/water_creek_bench.py --run --pond, and let it settle")
    vid = vols[0]["id"]
    EV.mkdir(parents=True, exist_ok=True)
    api.debug("water_coupling", {"enabled": True})
    api.debug("water_ripples", {"flow_forcing": args.forcing})
    zc = ZC - 0.5 + (0.5 / 3.0)
    xc = int(args.x * 3) / 3.0 + 1.0 / 6.0
    def col():
        c = api.debug("water_av_column", {"points": [[xc, zc]]}).get("columns", [{}])[0]
        return c.get("surface_mean") if c.get("surface_y") is not None else None
    def ledger():
        v = next(x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid)
        return v["mass"] - (v.get("source_in_m3", 0.0) - v.get("outfall_out_m3", 0.0))
    surface = col()
    if surface is None: raise SystemExit(f"no water at x {xc:.2f} z {zc:.2f}")
    camera_set(api, {"x": xc - 3.0, "y": surface + 1.6, "z": zc + 3.0, "yaw": -45.0, "pitch": -22.0})
    time.sleep(40.0 if args.forcing == 0 else 2.0)
    led0 = ledger(); d0 = api.debug("water_droplets", {})
    tag = f"pond_{args.tag}"
    rec = None
    if shutil.which("ffmpeg"):
        rec = subprocess.Popen([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.watch + 1.0),
                                "-i", "title=WaterBench_Creek", "-vf", "crop=960:630:240:42", "-c:v", "libx264", "-crf", "26", "-pix_fmt", "yuv420p",
                                str(EV / f"splash_{tag}.mp4")])
        time.sleep(0.5)
    api.debug("spawn_gpu_particle", {"x": xc, "y": surface + args.height + args.size / 2, "z": zc, "material": "Stone", "scale": args.size, "lifetime": 600.0})
    t0 = time.time(); rows = []
    while time.time() - t0 < args.watch:
        dr = api.debug("water_droplets", {}); rp = api.debug("water_ripples", {})
        rows.append({"t": round(time.time() - t0, 3), "births": dr["births"] - d0["births"], "landings": dr["landings"] - d0["landings"], "alive": dr["alive"],
                     "cosmetic_births": dr.get("cosmetic_births", 0) - d0.get("cosmetic_births", 0),
                     "ripple_max_mm": rp["max_abs_m"] * 1e3, "kinematic_cells": rp["kinematic_cells"], "awake": rp["awake"],
                     "surface": col(), "ledger_error_m3": ledger()})
    if rec: rec.wait()
    first_birth = next((r["t"] for r in rows if r["births"] > 0), None)
    all_landed = next((r["t"] for r in rows if r["births"] > 0 and r["landings"] >= r["births"]), None)
    surf = [r["surface"] for r in rows if r["surface"] is not None]
    summary = {"tag": tag, "model": "pond", "surface_before": surface, "droplets_born": rows[-1]["births"], "droplets_landed": rows[-1]["landings"],
               "cosmetic_births": rows[-1]["cosmetic_births"], "first_birth_t": first_birth, "all_landed_t": all_landed,
               "ripple_peak_mm": max(r["ripple_max_mm"] for r in rows), "kinematic_cells_max": max(r["kinematic_cells"] for r in rows),
               "surface_min": min(surf) if surf else None, "surface_max": max(surf) if surf else None,
               "ledger_error_before": led0, "ledger_error_after": rows[-1]["ledger_error_m3"], "ledger_change": rows[-1]["ledger_error_m3"] - led0}
    (EV / f"splash_{tag}.json").write_text(json.dumps({"summary": summary, "rows": rows}, indent=1))
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
