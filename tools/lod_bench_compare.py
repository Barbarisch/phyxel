"""Compare LodBench runs — separates the variance sources instead of conflating them.

  python tools/lod_bench_compare.py a.jsonl b.jsonl [c.jsonl ...]

Reports, per pose: the structural metrics (draws / instances / visible chunks) which SHOULD be
bit-identical for the same world, and the shadow-ms medians which will not be. Poses where any
run dropped samples (pose drift, incl. someone touching the camera) are flagged and excluded
from the timing spread, because a partial sample set is not a measurement.
"""
import json, sys, statistics


def load(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        r = json.loads(line)
        if r.get("event") == "summary":
            out[r["label"]] = r
    return out


def main():
    paths = sys.argv[1:]
    if len(paths) < 2:
        print(__doc__)
        return 1
    runs = [(p.split("/")[-1].replace(".jsonl", ""), load(p)) for p in paths]
    labels = [l for l in runs[0][1] if all(l in r for _, r in runs)]

    print("=== STRUCTURAL (must be identical for the same world state) ===")
    all_ident = True
    for lab in labels:
        draws = [r[lab]["shadow_chunks_drawn"][0] for _, r in runs]
        inst = [r[lab]["shadow_instances_drawn"][0] for _, r in runs]
        vis = [r[lab]["visible_chunks"][0] for _, r in runs]
        faces = [r[lab]["total_visible_faces"][0] for _, r in runs]
        ident = len(set(draws)) == 1 and len(set(inst)) == 1 and len(set(vis)) == 1
        all_ident &= ident
        print(f"  {lab:<10} draws={draws} inst={inst} vis={vis} faces={faces} "
              f"{'OK' if ident else '<<< DIFFERS'}")
    print(f"  => all structural metrics identical: {all_ident}")

    print("\n=== TIMING (shadow ms median) ===")
    hdr = "  {:<10}".format("pose") + "".join(f"{n[:14]:>16}" for n, _ in runs) + f"{'spread%':>10}{'drops':>10}"
    print(hdr)
    spreads = []
    for lab in labels:
        vals = [r[lab]["shadow_ms_median"] for _, r in runs]
        drops = [r[lab]["n_dropped"] for _, r in runs]
        clean = sum(drops) == 0
        spread = (max(vals) - min(vals)) / min(vals) * 100
        if clean:
            spreads.append(spread)
        row = "  {:<10}".format(lab) + "".join(f"{v:>16.2f}" for v in vals)
        print(row + f"{spread:>9.1f}%{str(drops):>10}" + ("" if clean else "  <-- DROPS, excluded"))
    if spreads:
        print(f"\n  clean-pose timing spread: max={max(spreads):.1f}%  mean={statistics.mean(spreads):.1f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
