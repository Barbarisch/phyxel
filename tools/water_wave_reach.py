"""Does the partial wake change what the waves do? (the owner, 2026-10-18, on pw_look3: "it looks like the left one is better
visually, but i really cant see much going on on the right one"). The 46 x 28 m pond (1 m deep, the video's rig), a REPEATABLE push
(water_av_impulse - the GPU stone tumbles differently every drop) at the video's stone spot, then the surface of single 1/9 m
columns at 1, 2, 4, 8 and 16 m from it (along +x) polled for --seconds (water_surface_dump: the field the mesh is drawn from).
Per point: the wave's RMS about the level before the push, its largest swing, and when it first moved more than 1 mm.

    python tools/water_wave_reach.py [--partial on|off|both] [--strength 4] [--seconds 8] [--tag wave_reach]
                                     [--sweeps 40,20,10]   (26.35: one run per pressure sweep count; 0 = the volume's own)
Writes docs/evidence/water_core_h1/<tag>.json.
"""
import argparse, json, math, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
DIST = [1, 2, 4, 8, 16]


def run(api, partial, strength, seconds, sweeps=0):
    l, w, x0, z0 = 46, 28, 2, 0
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    box = {"x1": x0, "y1": 16, "z1": z0, "x2": x0 + l - 1, "y2": 17, "z2": z0 + w - 1}
    vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False, "partial_wake": partial,
                                        **({"sweeps": sweeps} if sweeps else {})})["volume"]["id"]
    api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
    time.sleep(6.0)
    sx, sz = x0 + 4.0, z0 + w - 4.0
    pts = [(sx + d, sz) for d in DIST]
    def sample():
        out = []
        for (px, pz) in pts:
            d = api.debug("water_surface_dump", {"x1": px, "z1": pz, "x2": px + 0.11, "z2": pz + 0.11})
            f = (d.get("fields") or [None])[0]
            c = f["cols"][0] if f and f.get("cols") else None
            out.append(c[0][-1][1] if c and c[0] else float("nan"))
        return out
    base = sample()
    api.debug("water_av_impulse", {"x": sx, "y": 16.9, "z": sz, "radius": 0.5, "strength": strength, "dy": -1.0})
    t0 = time.time(); rows = []; resid = []; ms = []
    while time.time() - t0 < seconds:
        rows.append([round(time.time() - t0, 3)] + sample()); time.sleep(0.03)
        if len(rows) % 10 == 0:   # 26.35: the pressure residual after the sweeps and the solver's time, every tenth sample
            vv = next((x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid), {})
            resid.append(vv.get("rbgs_residual")); ms.append(vv.get("gpu_device_ms"))
    v = next((x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid), {})
    res = []
    for i, d in enumerate(DIST):
        dev = [r[1 + i] - base[i] for r in rows if not math.isnan(r[1 + i])]
        first = next((r[0] for r in rows if not math.isnan(r[1 + i]) and abs(r[1 + i] - base[i]) > 1e-3), None)
        res.append({"d_m": d, "rms_mm": 1000 * math.sqrt(sum(x * x for x in dev) / max(1, len(dev))), "max_mm": 1000 * max((abs(x) for x in dev), default=0), "first_s": first})
    resid = [r for r in resid if r is not None]; ms = sorted(m for m in ms if m)
    return {"partial": partial, "sweeps": v.get("gpu_sweeps"), "base": base, "rows": rows, "points": res, "samples": len(rows), "awake_end": v.get("awake_columns"),
            "residual_max": max(resid, default=None), "residual_median": sorted(resid)[len(resid) // 2] if resid else None,
            "solver_ms_median": ms[len(ms) // 2] if ms else None}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--partial", choices=["on", "off", "both"], default="both")
    ap.add_argument("--strength", type=float, default=4.0)
    ap.add_argument("--seconds", type=float, default=8.0)
    ap.add_argument("--quiet-scale", type=float, default=3.0, help="26.34: the per-column freeze test's thresholds x this (the ON run)")
    ap.add_argument("--tag", default="wave_reach")
    ap.add_argument("--sweeps", default="0", help="26.35: comma list of pressure sweep counts, one run each (0 = the volume's own)")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_av_partial_wake", {"quiet_scale": args.quiet_scale})
    out = {"quiet_scale": args.quiet_scale}
    for sw in [int(x) for x in args.sweeps.split(",")]:
        for name, p in ((("off", False), ("on", True)) if args.partial == "both" else ((args.partial, args.partial == "on"),)):
            key = name if sw == 0 else f"{name}_s{sw}"
            out[key] = r = run(api, p, args.strength, args.seconds, sw)
            print(f"partial wake {name.upper():3s} sweeps {r['sweeps']} ({r['samples']} samples, residual median {r['residual_median']} max {r['residual_max']}, "
                  f"solver {r['solver_ms_median']} ms):  " + "  ".join(
                f"{q['d_m']:2d} m: rms {q['rms_mm']:5.2f} max {q['max_mm']:5.1f} mm first {q['first_s'] if q['first_s'] is not None else '-'}" for q in r["points"]), flush=True)
    api.debug("water_av_partial_wake", {"quiet_scale": 3.0})
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    (EV / f"{args.tag}.json").write_text(json.dumps(out))
    print("wrote", EV / f"{args.tag}.json")


if __name__ == "__main__":
    main()
