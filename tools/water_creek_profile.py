"""WaterCore.md 26.15 - the creek bench's steady profile: the drawn surface over every voxel column of the creek (no
read-back - water_av_column), minus the carve, averaged across the channel; and the depth over whole reaches (a reach = the
run between two of the carve's drops), which takes out the drawdown each drop makes at its edge.

    python tools/water_creek_profile.py [--model pond|column] [--grid 9] [--q 0.2] [--samples 10]   # the one running

Prints per-x depth and surface speed (the mean of --samples reads 1 s apart), each reach's mean depth, and the mean over the
whole reaches against Manning's normal depth for q on the 1:200 line. Writes docs/evidence/water_core_h1/
creekbench_pond_g<G>_q<Q>_profile.json.
"""
import argparse, itertools, json, math, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_creek_bench as B


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--grid", type=int, default=9, choices=(3, 9))
    ap.add_argument("--q", type=float, default=0.2)
    ap.add_argument("--samples", type=int, default=10)
    ap.add_argument("--model", default="pond", choices=("pond", "column"), help="the running pond volume, or the running column region")
    args = ap.parse_args()
    B.GRID = args.grid
    api = Api(args.url)
    xs = list(range(B.X0 + 1, B.X1))
    pts = [[x + 0.5, z + 0.5] for x in xs for z in (B.Z0, B.Z1)]
    acc = {x: [] for x in xs}
    for _ in range(args.samples):
        cols = []
        if args.model == "pond":
            for i in range(0, len(pts), 64):   # the route answers at most 64 points a call
                cols += api.debug("water_av_column", {"points": pts[i:i + 64]}).get("columns", [])
        else:
            rid = api.debug("water_columns", {}).get("regions", [])[0]["id"]
            for z in (B.Z0 + 0.5, B.Z1 + 0.5):
                st = api.debug("water_columns", {"id": rid, "profile": {"x0": xs[0] + 0.5, "z0": z, "x1": xs[-1] + 0.5, "z1": z, "n": len(xs)}})["profile"]
                cols += [{"x": s["x"], "surface_y": s["surface_y"] if s["wet"] else None, "u": s["u"]} for s in st]
        for c in cols:
            if c.get("surface_y") is None: continue
            acc[int(math.floor(c["x"]))].append((c["surface_y"] - B.bed_top(int(math.floor(c["x"]))), c["u"]))
        time.sleep(1.0)
    prof = [{"x": x, "bed": B.bed_top(x), "depth": sum(d for d, _ in acc[x]) / len(acc[x]) if acc[x] else None,
             "u_surface": sum(u for _, u in acc[x]) / len(acc[x]) if acc[x] else None} for x in xs]
    reaches = []
    for bed, g in itertools.groupby(prof, key=lambda p: round(p["bed"], 4)):
        g = [p for p in g if p["depth"] is not None]
        if g: reaches.append({"bed": bed, "x0": g[0]["x"], "x1": g[-1]["x"], "mean_depth": sum(p["depth"] for p in g) / len(g),
                              "head_depth": g[0]["depth"], "edge_depth": g[-1]["depth"]})
    whole = [r for r in reaches[1:-1]]   # the first and last reaches are cut by the head and the foot
    width = B.Z1 - B.Z0 + 1
    dn = (args.q / width * B.N / math.sqrt(B.SLOPE)) ** 0.6
    mean = sum(r["mean_depth"] * (r["x1"] - r["x0"] + 1) for r in whole) / max(1, sum(r["x1"] - r["x0"] + 1 for r in whole))
    out = {"q": args.q, "grid": args.grid, "manning_depth": dn, "whole_reach_mean_depth": mean, "ratio": mean / dn,
           "speed_by_continuity": args.q / (width * mean), "manning_speed": args.q / (width * dn), "reaches": reaches, "profile": prof}
    for r in reaches: print(f"reach bed {r['bed']:.3f}  x {r['x0']}-{r['x1']}  mean {r['mean_depth']:.3f}  head {r['head_depth']:.3f}  edge {r['edge_depth']:.3f}")
    print(json.dumps({k: v for k, v in out.items() if k not in ("reaches", "profile")}))
    (B.EV / f"creekbench_{args.model}_g{args.grid}_q{args.q:g}_profile.json").write_text(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
