"""WaterCore.md 25.24 - the simple creek bench (owner, 2026-10-12): "a mostly flat world without flora or fauna. you just
carve out a simple mostly straight creak so we have a much more simple testing point".

WaterBench_Creek (port 8114), an empty world: a flat 128 x 64 m slab (dirt, a grass top layer; the ground's top at y 16) and
one straight creek carved into it - x 10-109 (100 m), z 31-32 (2 m wide), its bed a 1:200 line from 15.67 m floored to the
1/9 m grid (26.15, the owner's choice (b): gravel subcubes for whole thirds, gravel microcubes for the rest - drops of 1/9 m
every ~22 m; the 1/3 m carve of 25.22 is `--build --grid 3`), so it starts 1/3 m below the grass and ends ~0.9 m below. Pond
water flows over it (the only water model since WaterCore 26.27), a spring at the head, an outfall at the foot.

    python tools/water_creek_bench.py --build                 # author + save the ground and the creek (once)
    python tools/water_creek_bench.py --run [--q 0.1]          # start the creek and LEAVE IT RUNNING (no clip)
    python tools/water_creek_bench.py --run --q 0.2 --record 120   # ... and record a clip + numbers
Writes docs/evidence/water_core_h1/creekbench_pond_g9_q<Q>.{json,mp4} when recording (no _g9 on the --grid 3 carve).
(--pond is still accepted and does nothing: pond water is the only mode since WaterCore 26.27.)

The creek (WaterCore.md 26.13 P3b): ONE sparse pond-core volume over the creek (x 10-109, y 14-17, z 29-34 at 1/9 m), Manning n
at the bed, springs = pumps across the head's 1/9 m cells, outfalls across the foot's; the bed is the carved gravel itself
(occupancy). The numbers: mid-reach depth from the drawn surface (water_av_column) minus the carve, mean speed by continuity
(q / width depth at steady flow - out/in reports how steady), the surface speed, in / out / stored and the ledger from the
volume's record, its GPU ms.
"""
import argparse, json, math, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
X0, X1, Z0, Z1 = 10, 109, 31, 32            # the creek's voxels
LINE0, SLOPE, N = 15.67, 1.0 / 200.0, 0.035  # the bed line at x = X0 (m), its slope, Manning n (stony bed)


GRID, TAG = 9, "_g9"                                  # the carve's grid per metre (26.15: 9; the 25.22 carve was 3)


def line(x):
    return LINE0 - SLOPE * (x - X0)


def build(api):
    world_job(api, "/api/world/clear", {"x1": 0, "y1": 9, "z1": 0, "x2": 127, "y2": 31, "z2": 63})
    world_job(api, "/api/world/fill", {"x1": 0, "y1": 9, "z1": 0, "x2": 127, "y2": 14, "z2": 63, "material": "Dirt"})
    world_job(api, "/api/world/fill", {"x1": 0, "y1": 15, "z1": 0, "x2": 127, "y2": 15, "z2": 63, "material": "Grass"})
    world_job(api, "/api/world/clear", {"x1": X0, "y1": 15, "z1": Z0, "x2": X1, "y2": 15, "z2": Z1})   # the creek's top voxel
    subs, micros = [], []
    for x in range(X0, X1 + 1):
        nine = int(round((bed_top(x) - 15.0) * 9))             # micro layers of voxel 15 under the bed (0-8)
        layers, rem = nine // 3, nine % 3                      # whole subcube layers, then micro layers in the next
        for z in range(Z0, Z1 + 1):
            for sx in range(3):
                for sz in range(3):
                    for sy in range(layers):
                        subs.append({"x": x, "y": 15, "z": z, "sx": sx, "sy": sy, "sz": sz, "material": "Gravel"})
                    for my in range(rem):
                        for mx in range(3):
                            for mz in range(3):
                                micros.append({"x": x, "y": 15, "z": z, "sx": sx, "sy": layers, "sz": sz, "mx": mx, "my": my, "mz": mz, "material": "Gravel"})
    for i in range(0, len(subs), 500): api.post("/api/world/subcubes/batch", {"subcubes": subs[i:i + 500]})
    for i in range(0, len(micros), 500): api.post("/api/world/microcubes/batch", {"microcubes": micros[i:i + 500]})
    r = api.post("/api/world/save", {})
    print(f"built: slab + creek on the 1/{GRID} m grid, {len(subs)} gravel subcubes + {len(micros)} microcubes; save: {str(r)[:120]}")


def verify(api):
    def top(x, z): return api.get(f"/api/world/terrain_height?x={x}&z={z}&min_y=0&max_y=64").get("surface_y")
    bad = [f"ground ({x},{z}) {top(x, z)}" for x, z in ((0, 0), (60, 20), (120, 50)) if top(x, z) != 15]
    for x in (X0, (X0 + X1) // 2, X1):
        t = top(x, Z0)
        if t not in (14, 15): bad.append(f"creek ({x},{Z0}) top {t}")
    return bad


def main():
    global TAG
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--q", type=float, default=0.1)
    ap.add_argument("--record", type=float, default=0.0, help="seconds of clip + polling (0 = just start it and leave it running)")
    ap.add_argument("--pond", action="store_true", help="accepted, pond water is the only mode since WaterCore 26.27")
    ap.add_argument("--sweeps", type=int, default=40, help="the pressure sweeps per substep")
    ap.add_argument("--ground-bricks", type=int, default=-1, choices=(-1, 0, 1), help="26.18 ground bricks on/off (-1 = the engine default); tags the outputs _gb<0|1>")
    ap.add_argument("--brick-height", type=int, default=0, help="GPU brick height 2 / 4 / 8 (26.17; 0 = the engine default); tags the outputs _h<H>")
    ap.add_argument("--grid", type=int, default=9, choices=(3, 9), help="the carve's grid per metre (26.15: 9; 25.22: 3) - --build carves it, the numbers read it")
    args = ap.parse_args()
    global GRID; GRID = args.grid; TAG = "" if GRID == 3 else f"_g{GRID}"
    api = Api(args.url); api.wait_status(120)
    if args.build: build(api)
    bad = verify(api)
    if bad: raise SystemExit("the creek bench is not built (run --build): " + "; ".join(bad))
    if not args.run: return
    api.debug("water_av_realtime", {"on": True})
    return run_pond(api, args)


def bed_top(x):
    """the carve's top at voxel x (m): the line at the voxel's centre floored to the carve's grid, as --build carved it"""
    return math.floor(line(x + 0.5) * GRID + 1e-4) / GRID


def run_pond(api, args):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})   # one creek at a time
    h = 1.0 / 9.0
    r = api.debug("water_av_create", {"x1": X0, "y1": 14, "z1": Z0 - 2, "x2": X1, "y2": 17, "z2": Z1 + 2, "cellSize": h, "backend": "gpu",
                                       "sweeps": args.sweeps, "auto_sleep": False, "manning_n": N, "brick_height": args.brick_height, **({"ground_bricks": bool(args.ground_bricks)} if args.ground_bricks >= 0 else {})})
    if "error" in r: raise SystemExit(r)
    vid = r["volume"]["id"]
    print("volume", {k: r["volume"].get(k) for k in ("id", "cells", "backend", "gpu_sweeps", "manning_n", "gpu_bricks", "gpu_brick_capacity", "gpu_brick_height")})
    cells_z = [Z0 + (k + 0.5) * h for k in range(int(round((Z1 - Z0 + 1) / h)))]   # the creek's 1/9 m cells across
    for z in cells_z:
        api.debug("water_av_source", {"id": vid, "x": X1 + 1 - h / 2, "y": bed_top(X1) + h / 2, "z": z, "outfall": True})
    camera_set(api, {"x": X0 + 2.0, "y": 18.2, "z": Z0 + 3.5, "yaw": -12.0, "pitch": -16.0})
    time.sleep(2.0)
    rec, t0 = None, time.time()
    name = f"creekbench_pond{TAG}_q{args.q:g}" + (f"_h{args.brick_height}" if args.brick_height else "") + (f"_gb{args.ground_bricks}" if args.ground_bricks >= 0 else "")
    if args.record > 0 and shutil.which("ffmpeg"):
        EV.mkdir(parents=True, exist_ok=True)
        rec = subprocess.Popen([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.record),
                                "-i", "title=WaterBench_Creek", "-vf", "crop=960:630:240:42", "-c:v", "libx264", "-crf", "28",
                                "-pix_fmt", "yuv420p", str(EV / f"{name}.mp4")])
    for z in cells_z:
        api.debug("water_av_source", {"id": vid, "x": X0 + h / 2, "y": bed_top(X0) + h / 2, "z": z, "rate": args.q / len(cells_z)})
    if args.record <= 0:
        print(f"creek running as pond water: volume {vid}, {args.q} m^3/s; it keeps running until the engine stops or water_av_destroy")
        return
    mid = [(x + 0.5, z + 0.5) for x in range(55, 66, 2) for z in (Z0, Z1)]   # the mid-reach's voxel columns
    polls = []
    while time.time() - t0 < args.record:
        v = next(x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid)
        cols = api.debug("water_av_column", {"points": [list(p) for p in mid]}).get("columns", [])
        wet = [c for c in cols if c.get("surface_y") is not None]
        depth = sum(c["surface_y"] - bed_top(c["x"]) for c in wet) / len(wet) if wet else 0.0
        us = sum(c["u"] for c in wet) / len(wet) if wet else 0.0
        et = api.get("/api/debug/engine_timing")   # 26.17 (GET only): the frame, beside the water's own time
        polls.append({"t": round(time.time() - t0, 2), "fps": et.get("fps", 0.0), "brick_height": v.get("gpu_brick_height", 0), "ground_bricks": v.get("gpu_ground_bricks", 0), "in_m3": v.get("source_in_m3", 0.0), "out_m3": v.get("outfall_out_m3", 0.0), "stored_m3": v["mass"],
                      "ledger_error_m3": v["mass"] - (v.get("source_in_m3", 0.0) - v.get("outfall_out_m3", 0.0)), "gpu_ms": v.get("gpu_ms_last_call", 0.0), "gpu_device_ms": v.get("gpu_device_ms", 0.0), "gpu_host_ms": v.get("gpu_host_ms", 0.0), "gpu_wait_ms": v.get("gpu_wait_ms", 0.0), "gpu_ticks": v.get("gpu_ticks", 0),
                      "bricks": v.get("gpu_bricks"), "mid_depth_m": depth, "mid_surface_speed_ms": us, "mid_wet_columns": len(wet)})
        time.sleep(0.5)
    if rec: rec.wait()
    w = [p for p in polls if p["t"] >= args.record - 20.0]
    width = Z1 - Z0 + 1
    dn = (args.q / width * N / math.sqrt(SLOPE)) ** 0.6
    last = polls[-1]
    summary = {"q": args.q, "model": "pond", "manning_depth": dn, "manning_speed": args.q / (width * dn),
               "out_over_in_last20s": (w[-1]["out_m3"] - w[0]["out_m3"]) / max(1e-6, w[-1]["t"] - w[0]["t"]) / args.q,
               "first_outflow_s": next((p["t"] for p in polls if p["out_m3"] > 1e-4), None),
               "mid_depth_m": last["mid_depth_m"], "mid_speed_by_continuity_ms": args.q / (width * max(last["mid_depth_m"], 1e-6)),
               "mid_surface_speed_ms": last["mid_surface_speed_ms"], "ledger_error_m3": last["ledger_error_m3"],
               "gpu_ms_median": sorted(p["gpu_ms"] for p in polls)[len(polls) // 2], "gpu_ms_max": max(p["gpu_ms"] for p in polls), "gpu_device_ms_median": sorted(p["gpu_device_ms"] for p in polls)[len(polls) // 2], "gpu_host_ms_median": sorted(p["gpu_host_ms"] for p in polls)[len(polls) // 2], "gpu_wait_ms_median": sorted(p["gpu_wait_ms"] for p in polls)[len(polls) // 2], "gpu_ticks_median": sorted(p["gpu_ticks"] for p in polls)[len(polls) // 2], "bricks": last["bricks"]}
    tail = [p for p in polls if p["t"] >= args.record - 60.0]   # 26.17: the cost at steady flow (the last minute)
    med = lambda k: sorted(p[k] for p in tail)[len(tail) // 2]
    summary.update({"brick_height": last["brick_height"], "stored_cells_last60s": med("bricks") * 64 * max(1, last["brick_height"]),
                    "gpu_device_ms_median_last60s": med("gpu_device_ms"), "fps_median_last60s": med("fps"), "ground_bricks_last60s": med("ground_bricks")})
    (EV / f"{name}.json").write_text(json.dumps({"summary": summary, "polls": polls}, indent=1))
    print(json.dumps(summary))
    print("the creek keeps running")


if __name__ == "__main__":
    main()
