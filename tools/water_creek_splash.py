"""WaterCore.md 25.30 SC-T2 - a stone into the creek (WaterBench_Creek, the creek running at steady flow - `python
tools/water_creek_bench.py --run` first and give it ~2 minutes).

A Stone cube (1/3 m by default) is dropped from `--height` above the creek's free surface at the centre line. Polled every
~50 ms for `--watch` seconds: the splash droplets (real water: born / landed / alive), the volume's ripple layer (peak,
kinematic cells), the drawn surface at the drop column and the volume's ledger error. Pond water is the only model since
WaterCore 26.27 (--pond is accepted and does nothing).
`--forcing 0` (default) turns the flow's own ripples off so the stone's rings are what the ripple layer holds (the A/B
control: before the drop the layer is asleep); a clip is recorded from a camera beside the drop point.

Predictions (written before the first run, 2026-10-12, for the column model): impact ~ sqrt(2 g h) = 5.4 m/s from 1.5 m;
the crossing of 11 cm of water takes a frame or two -> 5-30 droplets, each in flight <= 1.5 s; kinematic cells > 0 and the
layer wakes. On pond water the ledger is unchanged by the splash (a splash moves water, it never makes or loses it).

    python tools/water_creek_splash.py [--x 40] [--size 0.3333] [--height 1.5] [--watch 4] [--forcing 0]
Writes docs/evidence/water_core_h1/splash_pond_<tag>.json and splash_pond_<tag>.mp4.
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
    ap.add_argument("--pond", action="store_true", help="accepted, pond water is the only mode since WaterCore 26.27")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(60)
    return main_pond(api, args)


def main_pond(api, args):
    """26.24 P3d-1: the drop into the POND creek. The pond's splash is REAL water (droplets that leave the volume and land
    back - births / landings / alive, and the volume's own book); the gauges: the droplets, the
    volume's ripple layer, the drawn surface at the drop column, and the ledger (mass - (springs in - outfalls out)) before
    and after - a splash moves water, it never makes or loses it."""
    vols = api.debug("water_av_list").get("volumes", [])
    if not vols: raise SystemExit("start the creek first: python tools/water_creek_bench.py --run, and let it settle")
    vid = vols[0]["id"]
    EV.mkdir(parents=True, exist_ok=True)
    api.debug("water_coupling", {"enabled": True, "displace": True})
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
                     "ripple_max_mm": rp["max_abs_m"] * 1e3, "kinematic_cells": rp["kinematic_cells"], "awake": rp["awake"],
                     "surface": col(), "ledger_error_m3": ledger()})
    if rec: rec.wait()
    first_birth = next((r["t"] for r in rows if r["births"] > 0), None)
    all_landed = next((r["t"] for r in rows if r["births"] > 0 and r["landings"] >= r["births"]), None)
    surf = [r["surface"] for r in rows if r["surface"] is not None]
    summary = {"tag": tag, "model": "pond", "surface_before": surface, "droplets_born": rows[-1]["births"], "droplets_landed": rows[-1]["landings"],
               "first_birth_t": first_birth, "all_landed_t": all_landed,
               "ripple_peak_mm": max(r["ripple_max_mm"] for r in rows), "kinematic_cells_max": max(r["kinematic_cells"] for r in rows),
               "surface_min": min(surf) if surf else None, "surface_max": max(surf) if surf else None,
               "ledger_error_before": led0, "ledger_error_after": rows[-1]["ledger_error_m3"], "ledger_change": rows[-1]["ledger_error_m3"] - led0}
    (EV / f"splash_{tag}.json").write_text(json.dumps({"summary": summary, "rows": rows}, indent=1))
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
