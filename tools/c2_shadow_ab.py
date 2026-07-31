"""C2 shadow-submission A/B — legacy per-chunk draws vs one multidraw per arena buffer.

Written to the standard a solution-auditor set after failing an earlier n=6 attempt:
  * n >= 15 per state (the Shadow Pass GPU scope is documented BIMODAL — a tight ~3-4 ms mode
    plus ~10-12 ms spikes — so small-n medians are close to a coin flip);
  * TRUE interleaving (OFF, ON, OFF, ON, ...) so drift cannot masquerade as an effect;
  * per-sample draws / multidraw_calls / instances recorded on EVERY sample, so the ON path's
    activation is provable per sample rather than inferred from a separate probe;
  * BOTH median and mean reported, plus the spike rate, because choosing the statistic after
    the fact is how an n=6 null became a "3.5% regression".

Usage: python tools/c2_shadow_ab.py --port 8097 --out docs/evidence/<name>.jsonl [-n 20]
"""
import argparse, json, statistics, sys, time, urllib.request

def req(base, path, body=None, timeout=30):
    url = base + path
    r = (urllib.request.Request(url) if body is None else
         urllib.request.Request(url, data=json.dumps(body).encode(),
                                headers={"Content-Type": "application/json"}))
    with urllib.request.urlopen(r, timeout=timeout) as f:
        return json.loads(f.read().decode())

def scope_ms(scopes, name):
    for s in scopes:
        if s.get("name") == name:
            return s.get("ms")
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8097)
    ap.add_argument("--out", required=True)
    ap.add_argument("-n", type=int, default=20, help="samples per state (audit floor: 15)")
    ap.add_argument("--settle", type=float, default=5.0)
    a = ap.parse_args()
    base = f"http://localhost:{a.port}"
    out = open(a.out, "w", encoding="utf-8")

    def emit(rec):
        out.write(json.dumps(rec) + "\n"); out.flush()

    emit({"event": "run_start", "n_per_state": a.n, "status": req(base, "/api/status")})
    # Pin everything that animates, per the D1 lesson.
    emit({"event": "pin", "daynight": req(base, "/api/daynight/set",
                                          {"paused": True, "timeOfDay": 9.5})})
    for r_ in ("grass", "foliage"):
        try:
            req(base, f"/api/debug/{r_}", {"enabled": False})
        except Exception:
            pass

    samples = {"off": [], "on": []}
    print(f"{'i':>3} {'state':<5} {'shadow_ms':>10} {'draws':>7} {'multidraw':>10} {'instances':>11}")
    for i in range(a.n):
        for state in ("off", "on"):          # TRUE interleaving
            req(base, "/api/debug/gpu_driven_shadow", {"enabled": state == "on"})
            time.sleep(a.settle)
            # Read the stats AFTER settling, with an empty body so state is unchanged. Reading
            # them from the state-changing call returns the PREVIOUS frame's counters (stale by
            # one), which made an earlier run label every row with the opposite state's
            # multidraw_calls -- the exact activation proof this A/B exists to provide.
            t = req(base, "/api/debug/gpu_driven_shadow", {})
            g = req(base, "/api/debug/gpu_scopes")
            ms = scope_ms(g["scopes"], "Shadow Pass")
            rec = {"event": "sample", "i": i, "state": state, "shadow_ms": ms,
                   "draws": g.get("shadow_chunks_drawn"),
                   "instances": g.get("shadow_instances_drawn"),
                   "multidraw_calls": t.get("shadow_multidraw_calls"),
                   "toggle_reported": t.get("gpu_driven_shadow")}
            emit(rec)
            samples[state].append(rec)
            print(f"{i:>3} {state:<5} {ms:>10.3f} {rec['draws']:>7} "
                  f"{rec['multidraw_calls']:>10} {rec['instances']:>11}")

    def stats(rows):
        v = [r["shadow_ms"] for r in rows if r["shadow_ms"] is not None]
        lo = min(v)
        spikes = sum(1 for x in v if x > 1.5 * lo)
        return {"n": len(v), "median": statistics.median(v), "mean": statistics.mean(v),
                "min": lo, "max": max(v),
                "stdev": statistics.pstdev(v) if len(v) > 1 else 0.0,
                "spike_rate": spikes / len(v)}

    so, sn = stats(samples["off"]), stats(samples["on"])
    # Did the ON path actually engage on every ON sample?
    on_active = [r["multidraw_calls"] for r in samples["on"]]
    off_active = [r["multidraw_calls"] for r in samples["off"]]
    summary = {"event": "summary", "off": so, "on": sn,
               "on_multidraw_calls_seen": sorted(set(on_active)),
               "off_multidraw_calls_seen": sorted(set(off_active)),
               "on_path_active_every_sample": all(x and x > 0 for x in on_active),
               "draws_off": sorted({r["draws"] for r in samples["off"]}),
               "draws_on": sorted({r["draws"] for r in samples["on"]})}
    emit(summary)

    print("\n--- SUMMARY ---")
    for k, s in (("OFF", so), ("ON ", sn)):
        print(f"{k}: n={s['n']} median={s['median']:.3f} mean={s['mean']:.3f} "
              f"min={s['min']:.3f} max={s['max']:.3f} stdev={s['stdev']:.3f} "
              f"spike_rate={s['spike_rate']*100:.0f}%")
    print(f"ON path active on every ON sample: {summary['on_path_active_every_sample']} "
          f"(multidraw_calls seen: {summary['on_multidraw_calls_seen']})")
    dmed = (sn["median"] - so["median"]) / so["median"] * 100.0
    dmean = (sn["mean"] - so["mean"]) / so["mean"] * 100.0
    print(f"median delta: {dmed:+.1f}%   mean delta: {dmean:+.1f}%")
    # An effect is only claimable if median AND mean agree in sign and exceed the OFF spread.
    band = so["stdev"] / so["median"] * 100.0 * 3.0
    claim = (abs(dmed) > band and abs(dmean) > band and (dmed > 0) == (dmean > 0))
    print(f"OFF 3-sigma band: +/-{band:.1f}%  ->  "
          + ("EFFECT CLAIMABLE" if claim else
             "NO EFFECT CLAIMABLE — differences are inside noise / statistics disagree"))
    out.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())
