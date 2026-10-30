"""WaterCore A12 (45.1) - the rings' hand-over: does a NEAR pond now sleep when a far one does?

The Small bench (port 8111), Release, shipped settings otherwise (displacement on, the ripple reach on). L5's drop (a 1/3 m Stone and a
1/3 m Wood block, 1 m onto the slept 4 x 4 m pond); the water's viewer NEAR (4 m south - the pond holds a ripple layer) or FAR (200 m
- none). Timed from the drop until the pond is back in the record; the rings' largest height and the hand-over's state sampled.
Conditions: hand-over off (the red: 44.7 measured 59-76 s near, 26-34 s far) and on (--mm, default 1.5).
Prediction (written 2026-10-29): on, near 28-38 s; far unchanged (it holds no rings); the hand-over count rises by one a near drop.

    python tools/water_handover_sleep.py [--runs 2] [--mm 1.5] [--tag handover_sleep]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_float_rest as w
import water_l5_outcome as l5


def one(api, viewer, mm):
    api.debug("water_ripples", {"handover_mm": mm, "shape": False})
    api.debug("water_locality", {"test_viewer": viewer})
    w.AUTO_SLEEP = True; w.PRESSURE = True; w.pond(api)
    if not l5.wait_slept(api, 90): raise SystemExit("did not sleep first")
    h0 = (api.debug("water_ripples", {"shape": False}).get("handover") or {}).get("count", 0)
    for b in l5.BODIES.values():
        api.debug("spawn_gpu_particle", {"x": b["x"], "y": l5.Y0, "z": b["z"], "material": b["material"], "scale": 1 / 3, "lifetime": 150, "spin": 0})
    t0 = time.time(); rows = []; slept = None; vol_quiet = None
    while time.time() - t0 < 120:
        v = api.debug("water_av_list").get("volumes", [])
        rp = api.debug("water_ripples", {})   # shape on: max_abs_m comes from the shape stats (a 4 x 4 m layer: cheap)
        ho = rp.get("handover") or {}
        t = round(time.time() - t0, 1)
        if v and v[0].get("asleep") and vol_quiet is None: vol_quiet = t
        rows.append({"t": t, "volumes": len(v), "asleep": v[0].get("asleep") if v else None, "rings_max_mm": round(1000 * (rp.get("max_abs_m") or 0.0), 3),
                     "rings_awake": rp.get("awake"), "handing_over": ho.get("handing_over")})
        if not v and t > 3: slept = t; break
        time.sleep(1.0)
    ho = api.debug("water_ripples", {"shape": False}).get("handover") or {}
    api.debug("clear_dynamics")
    return {"slept_s": slept, "volume_quiet_s": vol_quiet, "handovers": ho.get("count", 0) - h0, "last_handover_s": ho.get("last_s"), "rows": rows}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--runs", type=int, default=2)
    ap.add_argument("--mm", type=float, default=1.5)
    ap.add_argument("--tag", default="handover_sleep")
    a = ap.parse_args()
    api = Api(a.url); st = api.wait_status(300)
    ho0 = (api.debug("water_ripples", {"shape": False}).get("handover") or {})
    if "mm" not in ho0: raise SystemExit("this build has no hand-over (water_ripples echoes no 'handover')")
    out = {"build": st.get("build_config"), "mm": a.mm, "runs": []}
    try:
        for k in range(a.runs):
            for where, viewer in (("near", l5.NEAR), ("far", l5.FAR)):
                for mm in (0.0, a.mm):
                    r = one(api, viewer, mm); r.update({"where": where, "mm": mm, "k": k})
                    out["runs"].append(r)
                    print(f"{where} hand-over {mm} mm: slept {r['slept_s']} s after the drop (volume quiet at {r['volume_quiet_s']} s), "
                          f"hand-overs {r['handovers']}, last {r['last_handover_s']} s", flush=True)
    finally:
        api.debug("clear_dynamics")
        api.debug("water_ripples", {"handover_mm": ho0.get("mm", 0.0), "shape": False})
        api.debug("water_locality", {"clear_test_viewer": True})
        (w.EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
