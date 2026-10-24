"""The pond count ladder (owner, 2026-10-23: the limits pass) - the 2 ms budget covers ALL water: how many small ponds fit?

WaterBench_Creek (port 8114, Release, the GPU otherwise idle). N ponds of 5 x 5 m, --depth deep, each its own sparse GPU volume at
1/9 m (the engine's defaults: bricks, sweeps, partial wake; auto-sleep OFF so the volume stays), laid on the slab at a 6 m pitch
(x 2-121, z 0-23 - clear of the creeks at z 29+), walls = the volumes' box faces (no shore - as the 26.29 ladder). The camera sees
all of them from above (a worst case for the surface pass, which is costed by what is seen since 26.30). Per N:
  ASLEEP    every pond placed flat and asleep
  ONE       a stone-sized impulse in one pond (the others asleep)
  ALL       the same impulse in every pond at once
Read: the GPU's frame and WaterSurface pass (/api/debug/gpu_timing), the sum of the volumes' solver ms (gpu_device_ms), fps.

    python tools/water_pond_count.py [--counts 1,4,16,32,64] [--depth 0.3]
Writes docs/evidence/water_core_h1/pond_count.json.
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
H, TOP, SIDE, PITCH = 1.0 / 9.0, 16, 5, 6


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})


def passes(api, frames=240):
    d = api.get(f"/api/debug/gpu_timing?frames={frames}")
    out = {"frame_gpu_ms": (d.get("gpu_frame_ms") or {}).get("median_ms")}
    for sc in d.get("scopes", []):
        if sc.get("name") == "WaterSurface": out["surface_ms"] = round(sc.get("median_ms", 0.0), 4)
    return out


def phase(api, ids, seconds):
    rows, t0 = [], time.time()
    while time.time() - t0 < seconds:
        vs = [v for v in api.debug("water_av_list").get("volumes", []) if v["id"] in ids]
        rows.append({"solver_sum_ms": sum(v.get("gpu_device_ms") or 0.0 for v in vs), "awake": sum(1 for v in vs if not v.get("asleep")),
                     "fps": api.get("/api/debug/engine_timing").get("fps")})
        time.sleep(0.5)
    awake = [r for r in rows if r["awake"]]
    out = {**passes(api), "fps": statistics.median(r["fps"] for r in rows), "awake_max": max(r["awake"] for r in rows),
           "solver_sum_ms_median": statistics.median(r["solver_sum_ms"] for r in (awake or rows)), "solver_sum_ms_max": max(r["solver_sum_ms"] for r in rows)}
    out["water_ms"] = out["solver_sum_ms_median"] + (out.get("surface_ms") or 0.0)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--counts", default="1,4,16,32,64")
    ap.add_argument("--depth", type=float, default=0.3)
    ap.add_argument("--strength", type=float, default=4.0)
    ap.add_argument("--out", default="pond_count.json")
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    destroy_all(api)
    api.post("/api/camera", {"mode": "free", "position": {"x": 62.0, "y": 70.0, "z": 60.0}, "yaw": -90.0, "pitch": -55.0})
    time.sleep(2.0)
    res = {"control": {**passes(api), "fps": api.get("/api/debug/engine_timing").get("fps")}, "rungs": [], "args": vars(a)}
    print("control:", res["control"], flush=True)
    slots = [(2 + i * PITCH, j * PITCH) for j in range(4) for i in range(20)]
    for n in [int(x) for x in a.counts.split(",")]:
        destroy_all(api)
        ids, centres = [], []
        for (x, z) in slots[:n]:
            box = {"x1": x, "y1": TOP, "z1": z, "x2": x + SIDE - 1, "y2": TOP, "z2": z + SIDE - 1}
            r = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False})
            if "error" in r: print("create:", r["error"]); break
            ids.append(r["volume"]["id"]); centres.append((x + SIDE / 2, z + SIDE / 2))
            api.debug("place_water_box", {"target": "core", **box, "mass": a.depth})
        t0 = time.time()
        while time.time() - t0 < 30 and any(not v.get("asleep") for v in api.debug("water_av_list")["volumes"] if v["id"] in ids): time.sleep(0.5)
        rung = {"n": len(ids), "seconds_to_all_asleep": round(time.time() - t0, 1)}
        rung["asleep"] = phase(api, set(ids), 5.0)
        kick = lambda c: api.debug("water_av_impulse", {"x": c[0], "y": TOP + a.depth - 0.1, "z": c[1], "radius": 1 / 3, "strength": a.strength, "dx": 0.0, "dy": -1.0, "dz": 0.0})
        kick(centres[0]); rung["one"] = phase(api, set(ids), 4.0)
        time.sleep(8.0)
        for c in centres: kick(c)
        rung["all"] = phase(api, set(ids), 4.0)
        res["rungs"].append(rung)
        print(json.dumps(rung), flush=True)
        EV.mkdir(parents=True, exist_ok=True); (EV / a.out).write_text(json.dumps(res, indent=1))
    destroy_all(api)
    print("wrote", EV / a.out)


if __name__ == "__main__":
    main()
