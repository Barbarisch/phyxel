"""WaterCore.md 26.30 - where the drawn surface's cost goes (owner, 2026-10-18: "start with A" - the surface costed by what is
seen and by distance, not by area).

26.29 measured the WaterSurface pass at ~8.8 us per m^2 of pond, the same asleep or awake and ~90 % of it with the camera turned
away. This splits it, on resting ponds (asleep: the solver at 0) of three sizes, the camera FACING the pond and turned AWAY:
  surface_pass_ms   the GPU's WaterSurface scope (/api/debug/gpu_timing)
  frame_gpu_ms      the whole frame on the GPU
  cpu_frame_ms      the engine's CPU frame (engine_timing cpuFrameTime)
  mesh_build_ms     the CPU mesher, every frame (water_render_core)
  mesh_vertices     what is drawn (48 B each; with tiles: the drawn tiles' vertices + the droplets)
  tiles             26.30: the tile record (tiles, rebuilt, kept, drawn, culled, uploaded bytes) - --tiles on|off, --cull on|off
Control: no pond. Rig as tools/water_pond_ladder.py (WaterBench_Creek, Release, idle GPU, box-walled pond on the slab, 1 m deep).

    python tools/water_surface_split.py [--sizes 10x10,20x20,32x28] [--tag before]
Writes docs/evidence/water_core_h1/surface_split_<tag>.json.
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
FACING = {"position": {"x": 22.0, "y": 32.0, "z": 48.0}, "yaw": -90.0, "pitch": -35.0}
AWAY = {"position": {"x": 22.0, "y": 32.0, "z": 48.0}, "yaw": 90.0, "pitch": -35.0}


def sample(api, seconds=3.0):
    time.sleep(1.5)   # the timing history holds frames from before the change
    cpu, build, verts = [], [], []
    t0 = time.time()
    while time.time() - t0 < seconds:
        cpu.append(api.get("/api/debug/engine_timing").get("cpuFrameTime"))
        rc = api.debug("water_render_core", {})
        build.append(rc.get("mesh_build_ms")); verts.append(rc.get("mesh_vertices") + ((rc.get("tiles") or {}).get("drawn_vertices") or 0))
        tiles = rc.get("tiles")
        time.sleep(0.2)
    d = api.get("/api/debug/gpu_timing?frames=120")
    ws = next((s["median_ms"] for s in d["scopes"] if s["name"] == "WaterSurface"), 0.0)
    return {"surface_pass_ms": round(ws, 3), "frame_gpu_ms": round(d["gpu_frame_ms"]["median_ms"], 3),
            "cpu_frame_ms": round(statistics.median(cpu), 3), "mesh_build_ms": round(statistics.median(build), 3),
            "mesh_vertices": max(verts), "fps": round(api.get("/api/debug/engine_timing").get("fps"), 1), "tiles": tiles}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--sizes", default="10x10,20x20,32x28")
    ap.add_argument("--tag", default="before")
    ap.add_argument("--tiles", choices=["on", "off", "keep"], default="keep", help="26.30: the kept tiles (on) or the per-frame whole mesh (off)")
    ap.add_argument("--cull", choices=["on", "off", "keep"], default="keep", help="26.30: the tiles' view test")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(120)
    opts = {k: getattr(args, k) == "on" for k in ("tiles", "cull") if getattr(args, k) != "keep"}
    if opts: print("render:", api.debug("water_render_core", opts).get("tiles"))
    api.debug("water_av_realtime", {"on": True})
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    out = {"control": {}, "ponds": []}
    for name, cam in (("facing", FACING), ("away", AWAY)):
        api.post("/api/camera", cam); out["control"][name] = sample(api)
    print("control", out["control"], flush=True)
    for s in args.sizes.split(","):
        l, w = (int(v) for v in s.split("x"))
        box = {"x1": 2, "y1": 16, "z1": 0, "x2": 2 + l - 1, "y2": 17, "z2": w - 1}
        vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
        api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
        t0 = time.time()
        while time.time() - t0 < 10 and not next(v for v in api.debug("water_av_list")["volumes"] if v["id"] == vid)["asleep"]: time.sleep(0.3)
        row = {"size_m": [l, w]}
        for name, cam in (("facing", FACING), ("away", AWAY)):
            api.post("/api/camera", cam); row[name] = sample(api)
        out["ponds"].append(row)
        print(row, flush=True)
        api.debug("water_av_destroy", {"id": vid})
    EV.mkdir(parents=True, exist_ok=True)
    (EV / f"surface_split_{args.tag}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"surface_split_{args.tag}.json")


if __name__ == "__main__":
    main()
