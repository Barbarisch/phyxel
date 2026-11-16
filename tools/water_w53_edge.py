"""WaterCore.md §54.4 (W5.3) - the river's ripples at a reach's edge, live (L4).

WaterBench_RiverCarve (port 8113), the shipped game.json (realtime and follow on, the river ripple layer on). The owner on
w52b_clip: "the line between ripples and not is very obvious". The camera hovers 8 m over the river 11 m upstream of the point where
the river leaves the follow reach's box, looking down the river at that edge. Four stills (gdigrab, the window): the river layer ON
and OFF (off = the reach's own layer, the record unrippled - the engine before 54.4), each as drawn and as the ripple-height debug
view (water_render_core debug 7: +-2 cm = black / white, read with the tonemap curve 0 - CLAUDE.md's rule for a debug view).
Prediction (written before the run): ON - the ripples run on across the edge; OFF - the ripple height stops at the box edge (grey
past it). The ripple layer's state (water_ripples river) and the follow's box are written with the stills.

    python tools/water_w53_edge.py [--tag w53_edge]
Writes docs/evidence/water_core_m4/<tag>_{on,off}_{look,ripple}.png and <tag>.json.
"""
import argparse, json, math, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_w52b_follow as w52b

EV = w52b.EV
SITE = w52b.SITE


def grab(window, crop, path):
    subprocess.run([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-i", f"title={window}",
                    "-frames:v", "1", "-vf", f"crop={crop}", str(path)], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--tag", default="w53_edge")
    ap.add_argument("--window", default="WaterBench_RiverCarve")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--settle", type=float, default=10.0)
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    out = {"bench": "WaterBench_RiverCarve", "river_default": api.debug("water_ripples", {"shape": False}).get("river")}
    camera_set(api, SITE["cam"]); time.sleep(12.0)
    path = w52b.trace(api, back_m=20.0, ahead_m=100.0)
    # stand over the river 20 m down the trace, wait for the follow's reach there
    i0 = min(len(path) - 1, 20)
    p = path[i0]
    d0 = (path[min(i0 + 2, len(path) - 1)][0] - path[i0 - 2][0], path[min(i0 + 2, len(path) - 1)][1] - path[i0 - 2][1])
    n = math.hypot(*d0) or 1.0
    camera_set(api, {"x": p[0], "y": SITE["level"] + 8.0, "z": p[1], "yaw": math.degrees(math.atan2(d0[1], d0[0])), "pitch": -39.0})
    f = {}
    for _ in range(40):
        time.sleep(0.5)
        f = w52b.follow(api)
        if f.get("reach_id"): break
    if not f.get("reach_id"): raise SystemExit(f"no follow reach: {f}")
    # RIG (not shipped): the follow's margin 2 m for the stills (shipped 8) - a new reach is centred on its point's 8 m tile, so
    # aiming 11 m inside a box at margin 8 chased the reach downstream (the second run: 3 swaps, then off the trace); restored below
    out["follow_margin_rig"] = w52b.follow(api, margin_m=2.0).get("margin_m")
    # aim at the CURRENT box's downstream edge from 11 m inside it (the follow keeps a reach while its river point - under the camera -
    # is >= 8 m inside: the first run stood 7.6 m inside and the follow swapped), until the reach holds still for 3 s
    for attempt in range(6):
        lo, hi = f.get("min"), f.get("max")
        inside = lambda q: lo[0] <= q[0] < hi[0] + 1 and lo[2] <= q[1] < hi[2] + 1
        k = next((j for j in range(1, len(path)) if inside(path[j - 1]) and not inside(path[j])), None)
        if k is None: raise SystemExit(f"the trace never leaves the reach's box {lo} {hi}")
        c = path[k]
        dx, dz = path[min(k + 2, len(path) - 1)][0] - path[k - 2][0], path[min(k + 2, len(path) - 1)][1] - path[k - 2][1]
        n = math.hypot(dx, dz) or 1.0; dx, dz = dx / n, dz / n
        pose = {"x": c[0] - 11.0 * dx, "y": SITE["level"] + 8.0, "z": c[1] - 11.0 * dz, "yaw": math.degrees(math.atan2(dz, dx)), "pitch": -36.0}
        camera_set(api, pose)
        rid = f.get("reach_id"); time.sleep(3.0)
        f = w52b.follow(api)
        if f.get("reach_id") == rid and f.get("min") == lo: break
        print("the follow moved its reach - aiming again at", f.get("min"), f.get("max"), flush=True)
    out.update({"box": {"min": lo, "max": hi}, "crossing": [round(c[0], 2), round(c[1], 2)], "dir": [round(dx, 3), round(dz, 3)], "pose": pose, "aims": attempt + 1})
    tm0 = api.post("/api/debug/tonemap", {})
    shots = {}
    for state in ("on", "off"):
        api.debug("water_ripples", {"shape": False, "river": {"on": state == "on"}})
        time.sleep(a.settle)   # the layer's spin-up (2 s, 54.4) with margin
        f2 = w52b.follow(api)
        if f2.get("reach_id") != f.get("reach_id"): print("!! the follow swapped its reach during the stills:", f2.get("reach_id"), flush=True)
        out[f"river_{state}"] = api.debug("water_ripples", {"shape": False}).get("river")
        out[f"follow_{state}"] = {k2: f2.get(k2) for k2 in ("reach_id", "min", "max", "makes")}
        p1 = EV / f"{a.tag}_{state}_look.png"; grab(a.window, a.crop, p1)
        api.debug("water_render_core", {"debug": 7}); api.post("/api/debug/tonemap", {"curve": 0, "exposure": 1.0}); time.sleep(1.0)   # the raw tap: no x8 exposure (the first run read white)
        p2 = EV / f"{a.tag}_{state}_ripple.png"; grab(a.window, a.crop, p2)
        api.debug("water_render_core", {"debug": 0}); api.post("/api/debug/tonemap", {"curve": tm0.get("curve", 1), "exposure": tm0.get("exposure", 8.0)})
        shots[state] = [str(p1.name), str(p2.name)]
        print(state, out[f"river_{state}"], flush=True)
    api.debug("water_ripples", {"shape": False, "river": {"on": True}})
    out["follow_margin_restored"] = w52b.follow(api, margin_m=8.0).get("margin_m")
    out["shots"] = shots; out["tonemap_restored"] = api.post("/api/debug/tonemap", {})
    (EV / f"{a.tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"{a.tag}.json", shots)


if __name__ == "__main__":
    main()
