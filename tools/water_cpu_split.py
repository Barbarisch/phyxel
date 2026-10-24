"""WaterCore.md 26.30 step 3 - where the water's CPU goes each frame, by part, on resting and awake ponds.

water_render_core's tiles record carries water_cpu_ms (26.30 step 3): the whole water block of the frame (total), the
manager's update (with its parts: occupancy, step, droplets, ripples, sleep), the feed to the renderer (feed: the
surface fields etc.), the motion scan (motion); and tiler_ms (the tiles, on the render side). engine_timing's
cpuFrameTime is the whole frame. Control: no pond. `moved` (26.30 step 3): how far a rebuilt tile's columns moved since its snapshot.

    python tools/water_cpu_split.py [--sizes 20x20,32x28] [--depth 1] [--tag rest]
Writes docs/evidence/water_core_h1/cpu_split_<tag>.json.
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
AWAY = {"position": {"x": 22.0, "y": 32.0, "z": 48.0}, "yaw": 90.0, "pitch": -35.0}


def sample(api, vid=None, seconds=3.0):
    time.sleep(1.5)
    rows = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        cpu = api.get("/api/debug/engine_timing").get("cpuFrameTime")
        t = api.debug("water_render_core", {}).get("tiles") or {}
        asleep = None
        if vid is not None:
            asleep = next(v for v in api.debug("water_av_list")["volumes"] if v["id"] == vid)["asleep"]
        rows.append({"cpu": cpu, "tiler": t.get("tiler_ms", 0.0), "rebuilt": t.get("rebuilt", 0), "w": t.get("water_cpu_ms", {}), "asleep": asleep, "moved": t.get("moved"), "upload": t.get("upload_bytes", 0), "build": t.get("build_ms", 0.0), "threads": t.get("threads_used")})
        time.sleep(0.2)
    med = lambda xs: round(statistics.median(xs), 3)
    w = {k: med([r["w"].get(k, 0.0) for r in rows]) for k in ("total", "update", "feed", "motion")}
    w["in_update"] = {k: med([r["w"].get("in_update", {}).get(k, 0.0) for r in rows]) for k in ("occupancy", "step", "droplets", "ripples", "sleep")}
    return {"cpu_frame_ms": med([r["cpu"] for r in rows]), "tiler_ms": med([r["tiler"] for r in rows]), "tile_build_ms": med([r["build"] for r in rows]),
            "threads_used": max((r["threads"] or 0) for r in rows), "rebuilt": max(r["rebuilt"] for r in rows),
            "water_cpu_ms": w, "asleep": [r["asleep"] for r in rows], "upload_bytes": max(r["upload"] for r in rows),
            "moved": [r["moved"] for r in rows if r["moved"] and r["rebuilt"]][-3:]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--sizes", default="20x20,32x28")
    ap.add_argument("--depth", type=int, default=1, help="m of water (the pond's box is depth + 1 m tall)")
    ap.add_argument("--wake", action="store_true", help="drop a stone in before sampling (the awake case)")
    ap.add_argument("--tag", default="rest")
    ap.add_argument("--threads", type=int, default=None, help="26.30 step 3: the tiler's threads (1 = the control, 0 = default)")
    ap.add_argument("--flow-tol", type=float, default=None, help="26.30 step 3: the tiles' flow/foam tolerance (0 = exact, the control)")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(120)
    opts = {}
    if args.threads is not None: opts["tile_threads"] = args.threads
    if args.flow_tol is not None: opts["tile_flow_tolerance"] = args.flow_tol; opts["tile_foam_tolerance"] = args.flow_tol
    rc = api.debug("water_render_core", opts).get("tiles") or {}
    print("tiler:", {k: rc.get(k) for k in ("threads", "flow_tolerance", "foam_tolerance")}, flush=True)
    api.debug("water_av_realtime", {"on": True})
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"], "discard": True})
    api.post("/api/camera", AWAY)
    out = {"control": sample(api), "ponds": [], "depth_m": args.depth, "wake": args.wake, "tiler": {k: rc.get(k) for k in ("threads", "flow_tolerance", "foam_tolerance")}}
    print("control", json.dumps(out["control"]), flush=True)
    for s in args.sizes.split(","):
        l, w = (int(v) for v in s.split("x"))
        box = {"x1": 2, "y1": 16, "z1": 0, "x2": 2 + l - 1, "y2": 16 + args.depth, "z2": w - 1}
        vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
        api.debug("place_water_box", {"target": "core", **{**box, "y2": 16 + args.depth - 1}, "mass": 1.0})
        t0 = time.time()
        while time.time() - t0 < 12 and not next(v for v in api.debug("water_av_list")["volumes"] if v["id"] == vid)["asleep"]: time.sleep(0.3)
        if args.wake:
            api.debug("water_av_impulse", {"x": 2 + l / 2, "y": 16 + args.depth - 0.1, "z": w / 2, "radius": 1.0 / 3.0, "strength": 4.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})   # as water_pond_ladder.py STONE
        row = {"size_m": [l, w], **sample(api, vid)}
        out["ponds"].append(row)
        print(json.dumps(row), flush=True)
        api.debug("water_av_destroy", {"id": vid, "discard": True})
    EV.mkdir(parents=True, exist_ok=True)
    (EV / f"cpu_split_{args.tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"cpu_split_{args.tag}.json")


if __name__ == "__main__":
    main()
