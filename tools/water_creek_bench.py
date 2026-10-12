"""WaterCore.md 25.24 - the simple creek bench (owner, 2026-10-12): "a mostly flat world without flora or fauna. you just
carve out a simple mostly straight creak so we have a much more simple testing point".

WaterBench_Creek (port 8114), an empty world: a flat 128 x 64 m slab (dirt, a grass top layer; the ground's top at y 16) and
one straight creek carved into it - x 10-109 (100 m), z 31-32 (2 m wide), its bed a 1:200 line from 15.67 m floored to the
1/3 m grid (the 25.22 rounding-down carve, gravel subcubes), so it starts 1/3 m below the grass and ends 1 m below. A
column-water region over it flows on the carve's line (option (a): the `plane` rule on authored ground), a spring at the
head, an outfall at the foot.

    python tools/water_creek_bench.py --build                 # author + save the ground and the creek (once)
    python tools/water_creek_bench.py --run [--q 0.1]          # start the creek and LEAVE IT RUNNING (no clip)
    python tools/water_creek_bench.py --run --record 90        # ... and record a clip + numbers
Writes docs/evidence/water_core_h1/creekbench_q<Q>.{json,mp4} when recording.
"""
import argparse, json, math, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
X0, X1, Z0, Z1 = 10, 109, 31, 32            # the creek's voxels
LINE0, SLOPE, N = 15.67, 1.0 / 200.0, 0.035  # the bed line at x = X0 (m), its slope, Manning n (stony bed)


def line(x):
    return LINE0 - SLOPE * (x - X0)


def build(api):
    world_job(api, "/api/world/clear", {"x1": 0, "y1": 9, "z1": 0, "x2": 127, "y2": 31, "z2": 63})
    world_job(api, "/api/world/fill", {"x1": 0, "y1": 9, "z1": 0, "x2": 127, "y2": 14, "z2": 63, "material": "Dirt"})
    world_job(api, "/api/world/fill", {"x1": 0, "y1": 15, "z1": 0, "x2": 127, "y2": 15, "z2": 63, "material": "Grass"})
    world_job(api, "/api/world/clear", {"x1": X0, "y1": 15, "z1": Z0, "x2": X1, "y2": 15, "z2": Z1})   # the creek's top voxel
    placed = 0
    for x in range(X0, X1 + 1):
        top = math.floor(line(x + 0.5) * 3 + 1e-4) / 3.0      # rounding DOWN to the 1/3 m grid (25.22)
        layers = int(round((top - 15.0) * 3))                  # subcube layers of voxel 15 (0-2)
        for z in range(Z0, Z1 + 1):
            for sy in range(layers):
                for sx in range(3):
                    for sz in range(3):
                        api.post("/api/world/subcube", {"x": x, "y": 15, "z": z, "sx": sx, "sy": sy, "sz": sz, "material": "Gravel"})
                        placed += 1
    r = api.post("/api/world/save", {})
    print(f"built: slab + creek, {placed} gravel subcubes; save: {str(r)[:120]}")


def verify(api):
    def top(x, z): return api.get(f"/api/world/terrain_height?x={x}&z={z}&min_y=0&max_y=64").get("surface_y")
    bad = [f"ground ({x},{z}) {top(x, z)}" for x, z in ((0, 0), (60, 20), (120, 50)) if top(x, z) != 15]
    for x in (X0, (X0 + X1) // 2, X1):
        t = top(x, Z0)
        if t not in (14, 15): bad.append(f"creek ({x},{Z0}) top {t}")
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--q", type=float, default=0.1)
    ap.add_argument("--record", type=float, default=0.0, help="seconds of clip + polling (0 = just start it and leave it running)")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(120)
    if args.build: build(api)
    bad = verify(api)
    if bad: raise SystemExit("the creek bench is not built (run --build): " + "; ".join(bad))
    if not args.run: return
    api.debug("water_av_realtime", {"on": True})
    for old in api.debug("water_columns", {}).get("regions", []): api.debug("water_columns", {"id": old["id"], "destroy": True})
    r = api.debug("water_columns", {"create": {"x1": X0, "y1": 14, "z1": Z0 - 2, "x2": X1, "y2": 17, "z2": Z1 + 2}, "manning_n": N})
    if "error" in r: raise SystemExit(r)
    rid = r["id"]
    api.debug("water_columns", {"id": rid, "plane": {"y0": LINE0 + SLOPE * X0, "slope": SLOPE, "dir_deg": 0.0}})
    api.debug("water_columns", {"id": rid, "bed": "plane"})
    cols = [Z0 + (k + 0.5) / 3.0 for k in range(3 * (Z1 - Z0 + 1))]
    for z in cols: api.debug("water_columns", {"id": rid, "outfall": {"x": X1 + 1 - 1.0 / 6.0, "z": z}})
    camera_set(api, {"x": X0 + 2.0, "y": 18.2, "z": Z0 + 3.5, "yaw": -12.0, "pitch": -16.0})
    time.sleep(2.0)
    rec, t0 = None, time.time()
    if args.record > 0 and shutil.which("ffmpeg"):
        EV.mkdir(parents=True, exist_ok=True)
        rec = subprocess.Popen([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.record),
                                "-i", "title=WaterBench_Creek", "-vf", "crop=960:630:240:42", "-c:v", "libx264", "-crf", "28",
                                "-pix_fmt", "yuv420p", str(EV / f"creekbench_q{args.q:g}.mp4")])
    for z in cols: api.debug("water_columns", {"id": rid, "spring": {"x": X0 + 1.0 / 6.0, "z": z, "rate": args.q / len(cols)}})
    if args.record <= 0:
        print(f"creek running: region {rid}, {args.q} m^3/s; it keeps running until the engine stops or water_columns destroy")
        return
    polls = []
    while time.time() - t0 < args.record:
        g = next(x for x in api.debug("water_columns", {}).get("regions", []) if x["id"] == rid)
        polls.append({"t": round(time.time() - t0, 2), **{k: g[k] for k in ("in_m3", "out_m3", "stored_m3", "ledger_error_m3", "step_ms", "wet_columns")}, **g["stats"]})
        time.sleep(0.5)
    if rec: rec.wait()
    w = [p for p in polls if p["t"] >= args.record - 20.0]
    width = Z1 - Z0 + 1
    dn = (args.q / width * N / math.sqrt(SLOPE)) ** 0.6
    summary = {"q": args.q, "manning_depth": dn, "manning_speed": args.q / (width * dn),
               "out_over_in_last20s": (w[-1]["out_m3"] - w[0]["out_m3"]) / max(1e-6, w[-1]["t"] - w[0]["t"]) / args.q,
               "first_outflow_s": next((p["t"] for p in polls if p["out_m3"] > 1e-4), None), **{k: polls[-1][k] for k in ("mid_depth_m", "mid_speed_ms", "wet", "hidden", "ledger_error_m3")},
               "step_ms_max": max(p["step_ms"] for p in polls)}
    (EV / f"creekbench_q{args.q:g}.json").write_text(json.dumps({"summary": summary, "polls": polls}, indent=1))
    print(json.dumps(summary))
    print("the creek keeps running")


if __name__ == "__main__":
    main()
