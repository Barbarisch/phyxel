"""WaterCore 48.4 (A4) - wading's whole-frame cost, live, on WaterBench_Small's S7 channel (0.8 m of water, 1/9 m cells, GPU).
Each window is tools/water_a3_cost.py's: fps, the CPU frame, the GPU frame, the water's host CPU and its parts (the A3 timers:
bodies = the wet-body loops, now with the characters' limbs; solids = the raster), the water's GPU per call.

    python tools/water_wade_cost.py [--url http://127.0.0.1:8111] [--out docs/evidence/water_core_m4/a4_cost.json]

Windows (the player wades with the forward key held, as in water_wade_s7.py):
  stand_on     the player standing in the water, characters on (fed, not shoving: the gate's stroke has ended)
  wade_on      the player wading the channel, characters on (shipped)
  wade_off     the same walk, characters off (the cost without the feed; the wading law still runs - it is the character's)
  crowd_on     the worst case: 12 NPCs patrolling the wet channel (two lanes), characters on
  crowd_off    the same crowd, characters off
Not measured: a crowd larger than the channel holds, fauna in a creek, many far slept ponds woken by waders (each wake is the
L4-paced wake already measured in 42)."""
import argparse, json, pathlib, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api
from water_a3_cost import window
import water_wade_s7 as s7


def wade_window(api, label, seconds=7.0):
    api.post("/api/input/inject", {"release_all": True})
    api.post("/api/entity/move", {"id": "player", "position": s7.START})
    api.post("/api/camera", {"control_scheme": "fps", "yaw": 0, "pitch": -20})
    time.sleep(1.5)
    api.post("/api/input/inject", {"keys": ["MoveForward"], "hold": seconds + 2.0})
    time.sleep(1.0)   # into the stride
    r = window(api, seconds, label)
    api.post("/api/input/inject", {"release_all": True})
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--out")
    ap.add_argument("--crowd-rounds", type=int, default=1)
    ap.add_argument("--only-crowd", action="store_true")
    ap.add_argument("--crowd-budgets", default="32", help="displace_budget values for the crowd_on window (the tier knob); 32 = shipped")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status()
    if not s7.ps(api).get("success"):
        api.post("/api/entity/spawn", {"type": "animated", "position": s7.START, "id": "player"})
    res = []
    if not a.only_crowd: res += single(api)
    res += crowd_windows(api, a)
    out = {"windows": res}
    for r in res:
        print(f"{r['label']:16s} fps {r['fps_mean']:7.1f}  cpu {r['cpu_frame_ms_mean']:.3f} ms  gpu {r['gpu_frame_ms_median']:.3f} ms  "
              f"app water {r['app_water_cpu_ms'].get('total')} (update {r['app_water_cpu_ms'].get('update')}, solids {r['app_water_cpu_ms'].get('solids')})  frame max {r['frame_ms_max']}", flush=True)
    if a.out: pathlib.Path(a.out).write_text(json.dumps(out, indent=1), encoding="utf-8")


def single(api):
    res = []
    api.debug("water_coupling", {"characters": True})
    s7.fill(api)
    api.post("/api/entity/move", {"id": "player", "position": s7.START})
    time.sleep(3.0)
    res.append(window(api, 6.0, "stand_on"))
    res.append(wade_window(api, "wade_on"))
    api.debug("water_coupling", {"characters": False})
    s7.fill(api)
    res.append(wade_window(api, "wade_off"))
    return res


def crowd_windows(api, a):
    res = []
    # the worst case: a crowd of waders. The player stands on the slab, out of the water.
    api.post("/api/entity/move", {"id": "player", "position": {"x": 70.0, "y": 17.0, "z": 20.0}})
    names = []
    for i in range(12):
        z = 14.5 if i % 2 == 0 else 15.5
        x0 = 67.0 + 0.8 * (i // 2)
        n = f"wader{i}"
        r = api.post("/api/npc/spawn", {"name": n, "position": {"x": x0, "y": 16.0, "z": z}, "behavior": "patrol", "walkSpeed": 2.0,
                                        "waitTime": 0.2, "waypoints": [{"x": 67.0, "y": 16.0, "z": z}, {"x": 76.5, "y": 16.0, "z": z}]})
        if r.get("success", True) and "error" not in r: names.append(n)
    crowd = None
    # interleaved rounds (the machine's load drifts within a session: windows minutes apart are not comparable)
    for rnd in range(a.crowd_rounds):
        for b in [int(x) for x in a.crowd_budgets.split(",")] + [None]:
            if b is None: api.debug("water_coupling", {"characters": False, "displace_budget": 32})
            else: api.debug("water_coupling", {"characters": True, "displace_budget": b})
            s7.fill(api)
            time.sleep(3.0)
            res.append(window(api, 8.0, ("crowd_off" if b is None else f"crowd_on_b{b}") + f"_r{rnd}"))
            if b is not None: crowd = api.debug("water_coupling", {}).get("characters")
    api.debug("water_coupling", {"characters": True})
    for n in names: api.post("/api/npc/remove", {"name": n})
    return res

if __name__ == "__main__":
    main()
