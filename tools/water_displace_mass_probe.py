"""WaterCore A10 (44.7) - where the 0.25-0.5 L a displacing drop loses goes (L5's pond ends short of its 24 m^3 after the sleep with
displacement on; +0.006 mL with it off).

The Small bench (port 8111), Release, L5's drop (a 1/3 m Stone and a 1/3 m Wood block, 1 m onto the slept 4 x 4 m pond, the water's
viewer far - no ripple layer, so it sleeps in ~30 s). Sampled once a second until the pond is back in the record: the volume's mass,
its residue dropped (thin films the solver drops), the droplets in flight, the water ledger's total; at the end the record's mass.
Displacement on, then off (the control), --runs each.

    python tools/water_displace_mass_probe.py [--runs 2] [--tag displace_mass]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
import water_float_rest as w
import water_l5_outcome as l5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--runs", type=int, default=2)
    ap.add_argument("--tag", default="displace_mass")
    a = ap.parse_args()
    api = Api(a.url); st = api.wait_status(300)
    d0 = api.debug("water_coupling", {}).get("displace")
    out = {"build": st.get("build_config"), "runs": []}
    try:
        for k in range(a.runs):
            for disp in (True, False):
                api.debug("water_locality", {"test_viewer": l5.FAR})
                w.AUTO_SLEEP = True; w.PRESSURE = True; w.DISPLACE = disp; w.pond(api)
                if not l5.wait_slept(api, 60): raise SystemExit("did not sleep first")
                m0 = l5.recorded_m3(api)
                dr0 = api.debug("water_droplets", {})
                for b in l5.BODIES.values():
                    api.debug("spawn_gpu_particle", {"x": b["x"], "y": l5.Y0, "z": b["z"], "material": b["material"], "scale": 1 / 3, "lifetime": 120, "spin": 0})
                t0, rows = time.time(), []
                while time.time() - t0 < 90:
                    v = api.debug("water_av_list").get("volumes", [])
                    dr = api.debug("water_droplets", {})
                    lg = api.debug("water_ledger", {})
                    vv = v[0] if v else {}
                    rows.append({"t": round(time.time() - t0, 1), "volume_mass": vv.get("mass"), "residue_dropped_m3": vv.get("residue_dropped_m3"),
                                 "in_flight_m3": dr.get("in_flight_m3"), "relocated_m3": dr.get("relocated_m3", 0) - dr0.get("relocated_m3", 0),
                                 "born_m3": dr.get("born_m3", 0) - dr0.get("born_m3", 0), "ledger": {k2: v2 for k2, v2 in lg.items() if isinstance(v2, (int, float))}})
                    if not v and time.time() - t0 > 3: break
                    time.sleep(1.0)
                m1 = l5.recorded_m3(api)
                last_sleep = api.debug("water_av_list").get("last_auto_sleep")   # A11 (44.8): what the sleep did with the film
                api.debug("clear_dynamics")
                peak = next((r for r in rows if r["volume_mass"] is not None), {})
                last = next((r for r in reversed(rows) if r["volume_mass"] is not None), {})
                run = {"displace": disp, "record_before_m3": m0, "record_after_m3": m1, "delta_l": None if m0 is None or m1 is None else round(1000 * (m1 - m0), 4),
                       "volume_mass_first": peak.get("volume_mass"), "volume_mass_last": last.get("volume_mass"),
                       "residue_dropped_last": last.get("residue_dropped_m3"), "relocated_m3": rows[-1]["relocated_m3"] if rows else None,
                       "born_m3": rows[-1]["born_m3"] if rows else None, "last_auto_sleep": last_sleep, "rows": rows}
                print(f"displace {disp}: record {m0} -> {m1} ({run['delta_l']} L); volume mass first {run['volume_mass_first']} last {run['volume_mass_last']}; "
                      f"residue dropped {run['residue_dropped_last']}; sleep {last_sleep}; droplets born {run['born_m3']:.4f} relocated {run['relocated_m3']:.6f} m^3", flush=True)
                out["runs"].append(run)
    finally:
        api.debug("clear_dynamics")
        api.debug("water_locality", {"clear_test_viewer": True})
        if d0 is not None: api.debug("water_coupling", {"displace": d0})
        (w.EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
