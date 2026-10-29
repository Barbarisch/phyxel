"""WaterCore L2 (43.2 / 43.3 item 8) - the stress: eight ponds (the old draw-index limit) along a line, four ripple slots, the
water's viewer flown past them 50 times with the reach on.

WaterBench_Creek (port 8114, Release). Eight 4 x 4 m GPU ponds, 0.67 m of water, auto-sleep off, at x 6 + 7k (k = 0..7), z 10-13,
inside the walled lake of 27.24. The camera is fixed; only the water's viewer moves (water_locality test_viewer), along z = 11.5
from x -30 to x 90 and back, 3 m a step. A 1/3 m Stone drops (1 m, lifetime 4 s) into the pond nearest the viewer every 0.3 s,
so the layers near it are awake and step on the ripple GPU (else the device states are vacuous). Passes: 20 at ripple_m 21 (fade 8),
10 at ripple_m 0, 10 at 24, 10 at 21 with fade 21 (= ripple_m: the fade from the viewer out).

Asserted at EVERY step (water_ripples {shape: false}, read after the viewer moved and two frames ran):
  - slots_held <= 4, and slot_list names distinct volumes;
  - a pond within ripple_m with no slot only while all four slots are held (waiting > 0 then);
  - a pond holding a slot lies within ripple_m + 4 m (the drop hysteresis);
  - ripple_m 0: no slot held, ever;
  - gpu_states <= 8 (four live + four freed but in flight at most).
At the end, the viewer 300 m away for 2 s: slots_held 0 and gpu_states 0 (every dropped layer's device state freed).

    python tools/water_l2_stress.py [--passes-scale 1.0] [--tag l2_stress]
Writes docs/evidence/water_core_m4/<tag>.json.
"""
import argparse, json, math, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_m4"
H, TOP = 1.0 / 9.0, 16
PONDS = [(6 + 7 * k, 10, 6 + 7 * k + 3, 13) for k in range(8)]
VZ = 11.5
# --big: eight 12 x 12 m ponds in two rows of four (11,664 lattice cells each: over kRippleGpuMinCells, so their layers step on the
# ripple GPU and the device states are tested live), the viewer between the rows
BIG = [(4 + 15 * (k % 4), 2 if k < 4 else 16, 4 + 15 * (k % 4) + 11, 13 if k < 4 else 27) for k in range(8)]


def box_d(v, p):
    x1, z1, x2, z2 = p
    dx = min(max(v[0], x1), x2 + 1) - v[0]; dz = min(max(v[1], z1), z2 + 1) - v[1]
    return math.hypot(dx, dz)


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8114)
    ap.add_argument("--passes-scale", type=float, default=1.0)
    ap.add_argument("--tag", default="l2_stress")
    ap.add_argument("--big", action="store_true", help="eight 12 x 12 m ponds (GPU-stepped layers) in two rows")
    a = ap.parse_args()
    global PONDS, VZ
    if a.big: PONDS, VZ = BIG, 14.5
    api = Api(f"http://127.0.0.1:{a.port}"); st = api.wait_status(300)
    loc0 = api.debug("water_locality", {})
    api.debug("water_av_realtime", {"on": True})
    api.debug("water_ripples", {"enabled": True, "shape": False})
    api.post("/api/camera", {"mode": "free", "position": {"x": 34.0, "y": 30.0, "z": -12.0}, "yaw": 90, "pitch": -40})
    api.debug("clear_dynamics"); destroy_all(api)
    ids = {}
    for k, (x1, z1, x2, z2) in enumerate(PONDS):
        b = {"x1": x1, "y1": TOP, "z1": z1, "x2": x2, "y2": TOP + 1, "z2": z2}
        r = api.debug("water_av_create", {**b, "cellSize": H, "backend": "gpu", "auto_sleep": False})
        if "error" in r: raise SystemExit(f"pond {k}: {r['error']}")
        ids[r["volume"]["id"]] = k
        api.debug("place_water_box", {"target": "core", **b, "y2": TOP, "mass": 0.67})
    time.sleep(6.0)
    plan = [(21.0, 8.0, 20), (0.0, 0.0, 10), (24.0, 8.0, 10), (21.0, 21.0, 10)]
    xs = [x for x in range(-30, 91, 3)]
    res = {"build": st.get("build_config"), "ponds": PONDS, "plan": plan, "phases": [], "violations": []}
    steps, last_drop, maxima = 0, 0.0, {"slots_held": 0, "gpu_states": 0, "waiting": 0}
    try:
        for (rm, fade, passes) in plan:
            passes = max(1, int(round(passes * a.passes_scale)))
            api.debug("water_locality", {"ripple_reach": True, "ripple_m": rm, "ripple_fade_m": fade})
            ph = {"ripple_m": rm, "fade_m": fade, "passes": passes, "steps": 0, "max_slots": 0, "max_waiting": 0, "max_states": 0}
            for p in range(passes):
                for x in (xs if p % 2 == 0 else xs[::-1]):
                    v = (float(x), VZ)
                    api.debug("water_locality", {"test_viewer": [v[0], 18.0, v[1]]})
                    if time.time() - last_drop > 0.3:   # a stone into the nearest pond, if one is within 25 m
                        k = min(range(8), key=lambda i: box_d(v, PONDS[i]))
                        if box_d(v, PONDS[k]) < 25.0:
                            x1, z1, x2, z2 = PONDS[k]
                            api.debug("spawn_gpu_particle", {"x": (x1 + x2 + 1) / 2, "y": TOP + 0.67 + 1.17, "z": (z1 + z2 + 1) / 2,
                                                             "material": "Stone", "scale": 1 / 3, "lifetime": 4.0, "spin": 0})
                            last_drop = time.time()
                    time.sleep(0.04)   # two frames or more: the viewer is read once a frame
                    s = api.debug("water_ripples", {"shape": False})
                    steps += 1; ph["steps"] += 1
                    held, wait, states = s.get("slots_held", 0), s.get("waiting", 0), s.get("gpu_states", 0)
                    sl = [e["volume"] for e in s.get("slot_list", [])]
                    ph["max_slots"] = max(ph["max_slots"], held); ph["max_waiting"] = max(ph["max_waiting"], wait); ph["max_states"] = max(ph["max_states"], states)
                    bad = []
                    if held > 4: bad.append(f"{held} slots held")
                    if len(set(sl)) != len(sl): bad.append(f"a volume holds two slots {sl}")
                    if states > 8: bad.append(f"{states} device states")
                    if rm <= 0.0 and held: bad.append("ripple_m 0 holds a slot")
                    # the viewer moved before this frame's slots were given, so allow one frame of travel (3 m) on both tests
                    for vid in sl:
                        k = ids.get(vid)
                        if k is not None and box_d(v, PONDS[k]) > rm + 4.0 + 3.0: bad.append(f"pond {k} holds a slot {box_d(v, PONDS[k]):.1f} m away")
                    within = [k for k in range(8) if box_d(v, PONDS[k]) <= rm - 3.0]
                    held_k = {ids.get(vid) for vid in sl}
                    if rm > 0 and any(k not in held_k for k in within) and held < 4: bad.append(f"pond within reach without a slot, {held} held")
                    if bad: res["violations"].append({"step": steps, "viewer": v, "ripple_m": rm, "what": bad})
            res["phases"].append(ph)
            print(f"ripple_m {rm} fade {fade}: {passes} passes, {ph['steps']} steps; max slots {ph['max_slots']}, waiting {ph['max_waiting']}, "
                  f"device states {ph['max_states']}; violations so far {len(res['violations'])}", flush=True)
        api.debug("water_locality", {"test_viewer": [300.0, 18.0, VZ]})
        time.sleep(2.0)
        end = api.debug("water_ripples", {"shape": False})
        res["end"] = {k: end.get(k) for k in ("slots_held", "gpu_states", "made", "dropped", "waiting")}
        print("far away at the end:", res["end"], flush=True)
        if end.get("slots_held") or end.get("gpu_states"): res["violations"].append({"step": "end", "what": [f"left {end}"]})
    finally:
        api.debug("water_locality", {"clear_test_viewer": True, "ripple_reach": loc0.get("ripple_reach", False),
                                     "ripple_m": loc0.get("ripple_m", 21.0), "ripple_fade_m": loc0.get("ripple_fade_m", 8.0)})
        api.debug("clear_dynamics"); destroy_all(api)
        res["steps"] = steps
        (EV / f"{a.tag}.json").write_text(json.dumps(res, indent=1), encoding="utf-8")
    print(f"{steps} steps, {len(res['violations'])} violations", flush=True)
    for x in res["violations"][:10]: print("  ", x)


if __name__ == "__main__":
    main()
