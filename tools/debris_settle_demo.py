#!/usr/bin/env python3
"""Watch the debris-settling scenarios in REAL TIME in the DebrisLab project.

Unlike debris_settle_bench.py (which freezes and steps the solver for exact measurement),
this leaves physics running at its normal 60 Hz so you see exactly what a player would.
The settle probe still runs, so each scenario ends with a one-line verdict.

  python tools/debris_settle_demo.py                 # all scenarios, 12 s each
  python tools/debris_settle_demo.py crater blast    # just these
  python tools/debris_settle_demo.py crater --seconds 20 --loop 3

Engine must be running the DebrisLab project (see debris_settle_bench.py --build-lab).
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import debris_settle_bench as b   # noqa: E402

# Camera per scenario: (dx, dy, dz) from the site's min corner, pitch. Yaw -90 looks toward -Z.
VIEWS = {
    "drop_layer":     ((4.5, 7.0, 17.0), -24.0),
    "drop_pile":      ((3.0, 9.0, 20.0), -22.0),
    "packed":         ((3.0, 8.0, 18.0), -22.0),
    "crater":         ((3.0, 7.0, 14.0), -30.0),
    "crater_subcube": ((2.0, 4.5, 9.0), -30.0),
    "blast":          ((3.5, 6.0, 15.0), -24.0),
}


def play(api, name, seconds):
    chunk, setup, floor_y = b.SCENARIOS[name]
    x, z = b.site_x(chunk), b.SITE_Z
    api.post("/api/debug/clear_dynamics", {})
    b.physics(api, frozen=False)
    (dx, dy, dz), pitch = VIEWS[name]
    api.post("/api/camera", {"position": {"x": x + dx, "y": b.GROUND + dy, "z": z + dz},
                             "yaw": -90.0, "pitch": pitch})
    print(f"\n>>> {name}: get ready...", flush=True)
    time.sleep(2.0)
    api.post("/api/debug/settle_probe", {"op": "start", "floor_y": floor_y})
    desc = setup(api, x, z)
    print(f"    {desc}", flush=True)
    t0 = time.time()
    while time.time() - t0 < seconds:
        time.sleep(1.0)
        st = api.post("/api/debug/settle_probe", {"op": "status", "series_last": 1})
        row = (st.get("series") or [{}])[-1]
        print(f"    {time.time() - t0:4.0f}s  awake {row.get('awake', 0):5d} / {row.get('active', 0):5d}"
              f"   fastest {row.get('max_speed', 0):5.2f} m/s", flush=True)
    st = api.post("/api/debug/settle_probe", {"op": "stop"})
    s = st["summary"]
    print(f"    verdict: {s['verdict']}   all asleep at {s['time_all_asleep_s']} s   "
          f"rebounds/body after impacts {s['rebounds']['per_body_after_mean']:.2f}   "
          f"force-frozen {s['sleep']['forced']}/{s['sleep']['clean'] + s['sleep']['forced']}",
          flush=True)
    if name == "blast":
        time.sleep(2.0)
        api.post("/api/debug/clear_dynamics", {})
        b.restore_blast_site(api)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenarios", nargs="*", default=[], help=" | ".join(b.SCENARIOS))
    ap.add_argument("--seconds", type=float, default=12.0, help="real seconds to watch each")
    ap.add_argument("--loop", type=int, default=1)
    ap.add_argument("--url", default=os.environ.get("PHYXEL_API_URL", "http://localhost:8090"))
    a = ap.parse_args()
    bad = [n for n in a.scenarios if n not in b.SCENARIOS]
    if bad:
        ap.error(f"unknown scenario(s) {bad}; choose from {list(b.SCENARIOS)}")
    api = b.Api(a.url)
    api.wait_loop()
    b.physics(api, frozen=False)
    for _ in range(a.loop):
        for n in (a.scenarios or list(b.SCENARIOS)):
            play(api, n, a.seconds)


if __name__ == "__main__":
    main()
