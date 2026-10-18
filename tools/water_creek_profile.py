"""WaterCore.md 26.15 - the creek bench's steady profile: the drawn surface over every voxel column of the creek (no
read-back - water_av_column), minus the carve, averaged across the channel; and the depth over whole reaches (a reach = the
run between two of the carve's drops), which takes out the drawdown each drop makes at its edge.

    python tools/water_creek_profile.py [--grid 9] [--q 0.2] [--samples 10] [--stat highest|mean|mass]   # the one running

Prints per-x depth and surface speed (the mean of --samples reads 1 s apart), each reach's mean depth, and the mean over the
whole reaches against Manning's normal depth for q on the 1:200 line. Writes docs/evidence/water_core_h1/
creekbench_pond_g<G>_q<Q>[_<stat>]_profile.json. Pond water is the only model since WaterCore 26.27 (--pond and
--model pond are accepted and do nothing).
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
    ap.add_argument("--model", default="pond", choices=("pond",), help="accepted, pond water is the only mode since WaterCore 26.27")
    ap.add_argument("--pond", action="store_true", help="accepted, pond water is the only mode since WaterCore 26.27")
    ap.add_argument("--stat", default="highest", choices=("highest", "mean", "mass"),
                    help="26.20: the voxel column's HIGHEST wet sub-column (water_av_column's surface_y, what 26.15 read) "
                         "or the MEAN over its wet sub-columns (a depth gauge), or MASS: the water in the column as a depth (a GPU read-back)")
    args = ap.parse_args()
    B.GRID = args.grid
    api = Api(args.url)
    xs = list(range(B.X0 + 1, B.X1))
    pts = [[x + 0.5, z + 0.5] for x in xs for z in (B.Z0, B.Z1)]
    acc = {x: [] for x in xs}
    for _ in range(args.samples):
        cols = []
        for i in range(0, len(pts), 64):   # the route answers at most 64 points a call
            got = api.debug("water_av_column", {"points": pts[i:i + 64], "mass": args.stat == "mass"}).get("columns", [])
            for c in got:
                if c.get("surface_y") is None: continue
                if args.stat == "mean": c["surface_y"] = c["surface_mean"]
                if args.stat == "mass": c["surface_y"] = B.bed_top(int(math.floor(c["x"]))) + c["mass_depth"]
            cols += got
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
    out = {"q": args.q, "grid": args.grid, "stat": args.stat, "manning_depth": dn, "whole_reach_mean_depth": mean, "ratio": mean / dn,
           "speed_by_continuity": args.q / (width * mean), "manning_speed": args.q / (width * dn), "reaches": reaches, "profile": prof}
    for r in reaches: print(f"reach bed {r['bed']:.3f}  x {r['x0']}-{r['x1']}  mean {r['mean_depth']:.3f}  head {r['head_depth']:.3f}  edge {r['edge_depth']:.3f}")
    print(json.dumps({k: v for k, v in out.items() if k not in ("reaches", "profile")}))
    (B.EV / f"creekbench_pond_g{args.grid}_q{args.q:g}{'_' + args.stat if args.stat != 'highest' else ''}_profile.json").write_text(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
