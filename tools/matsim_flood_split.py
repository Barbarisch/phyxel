"""MaterialSimPlan 6l - where a hybrid tick goes while the live rig's dam break is in flight (no video, view off).

Same rig and fills as tools/matsim_hybrid_video.py (the hand-placed MatSimLab project, port 8120; NOT generator output).
Steps the dam break and reads the lab's per-phase split (last_tick_ms, last_step_split_ms, ca_gpu_last_ms) every
`--every` ticks, averaging over the ticks of each read (the split is the LAST tick's; we read after single steps).

    python tools/matsim_flood_split.py --cpm 9 --wake-radius 1 [--seconds 6] [--out docs/evidence/...json]
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import matsim_lab as L
from water_bench import Api, world_job


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8120")
    ap.add_argument("--cpm", type=int, default=9)
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--before", type=float, default=1.0)
    ap.add_argument("--every", type=int, default=30, help="read the split after one tick every N ticks")
    ap.add_argument("--wake-radius", type=float, default=1.0)
    ap.add_argument("--hybrid-ca", default="auto")
    ap.add_argument("--extra", default="{}", help="extra create params as JSON (experiments)")
    ap.add_argument("--out", default="")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(120)
    L.fill(api, L.DAM); L.wait_occupancy()
    create = {"min": L.BOX_MIN, "size": L.BOX_SIZE, "cells_per_m": a.cpm, "particles": "hybrid",
              "wake_radius_m": a.wake_radius, "hybrid_ca": a.hybrid_ca, **json.loads(a.extra)}
    L.must(L.ms(api, "create", create), "create")
    L.must(L.ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 8}, "size": {"x": 3, "y": 3, "z": 8}, "fraction": 1.0}), "fill")
    L.must(L.ms(api, "fill_box", {"material": "Water", "min": {"x": 8, "y": 16, "z": 17}, "size": {"x": 8, "y": 5, "z": 7}, "fraction": 1.0}), "fill")
    L.ms(api, "view", {"on": False})
    rows = []

    def run(ticks, phase, t_off):
        done = 0
        while done < ticks:
            k = min(a.every - 1, ticks - done)
            for j in range(0, k, 4): L.must(L.ms(api, "step", {"n": min(4, k - j)}), "step")   # the API's game-loop wait
            w0 = time.perf_counter()
            L.must(L.ms(api, "step", {"n": 1}), "step")
            wall = (time.perf_counter() - w0) * 1000
            done += k + 1
            st = L.ms(api, "stats")
            rows.append({"phase": phase, "t_sim": round(t_off + done / 60, 3), "particles": st.get("particles", 0),
                         "substeps": st.get("particle_substeps"), "api_wall_ms": round(wall, 2),
                         "tick": st.get("last_tick_ms"), "split": st.get("last_step_split_ms"),
                         "ca_gpu": st.get("ca_gpu_last_ms"), "wake": st.get("last_wake"),
                         "ledger_exact": st["ledger"]["exact"]})
            r = rows[-1]
            print(f"{r['t_sim']:6.2f} s  n={r['particles']:>7,} sub={r['substeps']}  tick={ {k: round(v, 1) for k, v in r['tick'].items()} }")
            print("         split", {k: round(v, 1) if isinstance(v, float) else v for k, v in r["split"].items()})

    run(int(round(a.before * 60)), "before", -a.before)
    world_job(api, "/api/world/clear", L.DAM); L.wait_occupancy()
    L.must(L.ms(api, "resolidify"), "resolidify")
    run(int(round(a.seconds * 60)), "after", 0.0)
    L.fill(api, L.DAM)
    fl = [r for r in rows if r["particles"] > 100000]
    if fl:
        keys = list(fl[0]["split"].keys())
        print(f"\nmedian over {len(fl)} samples with > 100k particles:")
        print("  tick ", {k: round(statistics.median(r["tick"][k] for r in fl), 1) for k in fl[0]["tick"]})
        print("  split", {k: round(statistics.median(r["split"][k] for r in fl), 1) for k in keys})
    if a.out:
        pathlib.Path(a.out).write_text(json.dumps({"create": create, "rows": rows}, indent=1))
        print("wrote", a.out)


if __name__ == "__main__":
    main()
