"""WaterCore.md 25.29 RC - ripples on the creek: the flow-forcing A/B on the simple creek bench (WaterBench_Creek, 25.24; the
creek running at steady flow - `python tools/water_creek_bench.py --run` first and give it ~2 minutes).

For each forcing value: set `water_ripples {flow_forcing}`, let the field reach its steady state (the layer's film damping
time is ~20 s), read the ripple stats five times (peak height, the facet-slope histogram the shader draws, the step's cost)
and record a clip from the bench camera. Forcing 0 is the control: the creek's ripple layer must stay asleep (exact zero).

    python tools/water_creek_ripples.py [--forcing 0 200] [--settle 40] [--clip 15]
Writes docs/evidence/water_core_h1/ripples_ff<F>.mp4 and ripples_sweep.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_creek_bench as bench

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"


def quantile_edge(stats, q):
    h, e = stats["slope_hist"], stats["slope_edges"]
    n, c = sum(h), 0
    if n == 0: return 0.0
    for i, v in enumerate(h):
        c += v
        if c >= q * n: return e[i] if i < len(e) else float("inf")
    return float("inf")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--forcing", type=float, nargs="+", default=[0.0, 200.0])
    ap.add_argument("--settle", type=float, default=40.0)
    ap.add_argument("--clip", type=float, default=15.0)
    ap.add_argument("--pond", action="store_true", help="26.24: the creek as POND water (its volume's ripple layer) - the only model after P3d")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(60)
    tag = "_pond" if args.pond else ""
    if args.pond:
        if not api.debug("water_av_list").get("volumes"): raise SystemExit("start the creek first: python tools/water_creek_bench.py --run --pond, and let it settle")
    else:
        regs = api.debug("water_columns", {}).get("regions", [])
        if not regs or regs[0]["wet_columns"] < 200: raise SystemExit("start the creek first: python tools/water_creek_bench.py --run, and let it settle")
    EV.mkdir(parents=True, exist_ok=True)
    camera_set(api, {"x": bench.X0 + 2.0, "y": 18.2, "z": bench.Z0 + 3.5, "yaw": -12.0, "pitch": -16.0})
    ff = shutil.which("ffmpeg")
    out = []
    for f in args.forcing:
        api.debug("water_ripples", {"flow_forcing": f})
        time.sleep(args.settle)
        rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.clip), "-i", "title=WaterBench_Creek",
                                "-vf", "crop=960:630:240:42", "-c:v", "libx264", "-crf", "28", "-pix_fmt", "yuv420p",
                                str(EV / f"ripples{tag}_ff{f:g}.mp4")]) if ff else None
        rows = []
        for _ in range(5): rows.append(api.debug("water_ripples", {})); time.sleep(1.0)
        if rec: rec.wait()
        r = rows[-1]
        row = {"flow_forcing": f, "max_abs_mm": max(x["max_abs_m"] for x in rows) * 1e3, "max_slope": max(x["max_slope"] for x in rows),
               "slope_p50_below": quantile_edge(r, 0.5), "slope_p95_below": quantile_edge(r, 0.95), "slope_hist": r["slope_hist"],
               "awake": r["awake"], "ms_mean": sum(x["ms"] for x in rows) / len(rows)}
        print(json.dumps(row))
        out.append(row)
    (EV / f"ripples{tag}_sweep.json").write_text(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
