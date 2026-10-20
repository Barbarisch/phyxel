"""26.35: what the partial wake costs or saves on water that never stops moving (the creek: every column awake). The creek
bench must already be running (python tools/water_creek_bench.py --run --q 0.1, then wait for steady flow). Alternates the
partial wake OFF / ON on the SAME volume every --period s for --cycles cycles; per phase the solver's median GPU ms
(water_av_list gpu_device_ms, sampled every 0.1 s) and the per-stage split (water_av_kernel_timing). Same volume, same flow,
interleaved - the drift between separate runs (~0.2 ms) cancels.

    python tools/water_partial_wake_ab.py [--period 10] [--cycles 6] [--tag pw_ab_creek]
Writes docs/evidence/water_core_h1/<tag>.json.
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--period", type=float, default=10.0)
    ap.add_argument("--cycles", type=int, default=6)
    ap.add_argument("--tag", default="pw_ab_creek")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(300)
    vols = api.debug("water_av_list")["volumes"]
    if not vols: sys.exit("no water volume - start the creek bench first")
    vid = max(vols, key=lambda v: v.get("cells", 0))["id"]
    phases = {"off": [], "on": []}; stages = {"off": [], "on": []}
    for c in range(args.cycles):
        for name, on in (("off", False), ("on", True)):
            api.debug("water_av_partial_wake", {"id": vid, "on": on})
            time.sleep(1.0)   # the switch settles (a woken volume re-partitions)
            api.debug("water_av_kernel_timing", {"on": True, "reset": True})
            ms = []; t0 = time.time(); awake = None
            while time.time() - t0 < args.period:
                v = next(x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid)
                if v.get("gpu_device_ms"): ms.append(v["gpu_device_ms"])
                awake = (v.get("awake_columns"), v.get("frozen_columns"))
                time.sleep(0.1)
            t = api.debug("water_av_kernel_timing", {})
            api.debug("water_av_kernel_timing", {"on": False})
            phases[name].append(statistics.median(ms)); stages[name].append(t["ms_per_call"])
            print(f"cycle {c} {name.upper():3s}: solver median {statistics.median(ms):.3f} ms ({len(ms)} samples), timed total {t['total_ms_per_call']:.3f}, awake/frozen {awake}", flush=True)
    api.debug("water_av_partial_wake", {"id": vid, "on": True})
    out = {"volume": vid, "period_s": args.period, "phases": phases,
           "stage_mean": {k: {s: statistics.mean(d[s] for d in v) for s in v[0]} for k, v in stages.items()}}
    for k in ("off", "on"): print(f"{k.upper():3s}: median of phase medians {statistics.median(phases[k]):.3f} ms")
    diff = {s: out["stage_mean"]["on"][s] - out["stage_mean"]["off"][s] for s in out["stage_mean"]["on"]}
    print("ON - OFF by stage (ms):", ", ".join(f"{s} {d:+.3f}" for s, d in sorted(diff.items(), key=lambda kv: -abs(kv[1])) if abs(d) > 0.005))
    (EV / f"{args.tag}.json").write_text(json.dumps(out))
    print("wrote", EV / f"{args.tag}.json")


if __name__ == "__main__":
    main()
