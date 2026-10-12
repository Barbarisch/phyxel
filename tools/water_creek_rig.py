"""WaterCore.md 25.18 H1 step 1 - the creek bed comparison on WaterBench_Small (chunk 7: x 224-255, z 0-31).

A 30 m x 2 m channel along +x (x 225-254, interior z 13-14, stone walls z 12 and 15 up to y 21), its bed a 1-in-50 line
from 19.0 m floored to 1/3 m subcube steps (the owner's creek-bed rule, 25.19): y 17 full cubes, then subcube layers of
cube y 18. A column-water region over it (`water_columns`), a spring at the head, an outfall at the end; for each bed
rule (voxel / hydraulic / plane) the creek runs from dry for --seconds while a clip records the same camera, and the
region's numbers are polled: out/in, mid depth and speed (vs Manning), hidden columns.

    python tools/water_creek_rig.py [--build] [--q 0.02] [--seconds 90] [--rules voxel hydraulic plane]
Writes docs/evidence/water_core_h1/creek_<rule>_q<Q>.{json,mp4}.
"""
import argparse, json, math, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set, world_job

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
X0, X1, ZW0, ZW1 = 225, 254, 12, 15          # channel x range, wall z lines (interior z 13-14)
SLOPE, N = 1.0 / 50.0, 0.035
LINE_Y0 = 19.0                                # the bed's smooth line at x = X0


def line(x):
    return LINE_Y0 - SLOPE * (x - X0)


def build(api):
    # slab (y 9-16) and the channel's base (y 17, full cubes) across the channel; walls up to y 21; the bed's subcubes
    world_job(api, "/api/world/clear", {"x1": 224, "y1": 17, "z1": 10, "x2": 255, "y2": 25, "z2": 17})
    world_job(api, "/api/world/fill", {"x1": 224, "y1": 9, "z1": 10, "x2": 255, "y2": 17, "z2": 17, "material": "Stone"})
    for zw in (ZW0, ZW1):
        world_job(api, "/api/world/fill", {"x1": X0, "y1": 18, "z1": zw, "x2": X1, "y2": 21, "z2": zw, "material": "Stone"})
    placed = 0
    for x in range(X0, X1 + 1):
        top = math.floor(line(x + 0.5) * 3 + 1e-4) / 3.0   # the subcube-floored line at the cube's centre
        layers = int(round((top - 18.0) * 3))              # subcube layers of cube y 18 (0-3)
        for z in range(ZW0 + 1, ZW1):
            for sy in range(layers):
                for sx in range(3):
                    for sz in range(3):
                        api.post("/api/world/subcube", {"x": x, "y": 18, "z": z, "sx": sx, "sy": sy, "sz": sz, "material": "Stone"})
                        placed += 1
    print(f"built: {placed} subcubes placed")


def verify(api):
    """The world, not the API: the walls stand to y 21 and the bed's subcube steps are where the line puts them. A rig built
    in memory is gone after a restart (2026-10-11: three runs on empty ground) - refuse to measure without it."""
    def top(x, z):
        return api.get(f"/api/world/terrain_height?x={x}&z={z}&min_y=0&max_y=64").get("surface_y")
    bad = []
    for x in (X0, (X0 + X1) // 2, X1):
        for zw in (ZW0, ZW1):
            if top(x, zw) != 21: bad.append(f"wall ({x},{zw}) top {top(x, zw)} != 21")
        if top(x, ZW0 + 1) not in (17, 18): bad.append(f"bed ({x},{ZW0 + 1}) top {top(x, ZW0 + 1)} not 17/18")
    return bad


def region(api):
    r = api.debug("water_columns", {"create": {"x1": X0, "y1": 18, "z1": ZW0, "x2": X1, "y2": 21, "z2": ZW1}, "manning_n": N})
    if "error" in r: raise SystemExit(r)
    return r["id"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8111")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--q", type=float, default=0.02)
    ap.add_argument("--seconds", type=float, default=90.0)
    ap.add_argument("--rules", nargs="+", default=["voxel", "hydraulic", "plane"])
    args = ap.parse_args()
    ff = shutil.which("ffmpeg")
    api = Api(args.url); api.wait_status(60)
    EV.mkdir(parents=True, exist_ok=True)
    if args.build: build(api)
    bad = verify(api)
    if bad: raise SystemExit("the rig is not in the world (run with --build): " + "; ".join(bad))
    api.debug("water_av_realtime", {"on": True})
    for old in api.debug("water_columns", {}).get("regions", []): api.debug("water_columns", {"id": old["id"], "destroy": True})
    q = args.q
    width = 2.0
    dn = (q / width * N / math.sqrt(SLOPE)) ** 0.6
    for rule in args.rules:
        rid = region(api)
        api.debug("water_columns", {"id": rid, "plane": {"y0": LINE_Y0 + SLOPE * X0, "slope": SLOPE, "dir_deg": 0.0}})
        api.debug("water_columns", {"id": rid, "bed": rule})
        for k in range(6):
            zc = ZW0 + 1 + (k + 0.5) / 3.0
            api.debug("water_columns", {"id": rid, "outfall": {"x": X1 + 1 - 1.0 / 6.0, "z": zc}})
        camera_set(api, {"x": X0 - 4.0, "y": 22.5, "z": 14.0, "yaw": 0.0, "pitch": -24.0})
        time.sleep(2.0)
        tag = f"creek_{rule}_q{q:g}"
        mp4 = EV / f"{tag}.mp4"
        rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.seconds),
                                "-i", "title=WaterBench_Small", "-c:v", "libx264", "-crf", "24", "-pix_fmt", "yuv420p", str(mp4)]) if ff else None
        t0 = time.time()
        for k in range(6):
            zc = ZW0 + 1 + (k + 0.5) / 3.0
            api.debug("water_columns", {"id": rid, "spring": {"x": X0 + 1.0 / 6.0, "z": zc, "rate": q / 6.0}})
        polls = []
        while time.time() - t0 < args.seconds:
            reg = next(g for g in api.debug("water_columns", {}).get("regions", []) if g["id"] == rid)
            polls.append({"t": round(time.time() - t0, 2), **{k: reg[k] for k in ("in_m3", "out_m3", "stored_m3", "ledger_error_m3", "step_ms", "wet_columns")}, **reg["stats"]})
            time.sleep(0.5)
        if rec: rec.wait()
        last = polls[-1]
        w = [p for p in polls if p["t"] >= args.seconds - 20.0]
        out_rate = (w[-1]["out_m3"] - w[0]["out_m3"]) / max(1e-6, w[-1]["t"] - w[0]["t"])
        first_out = next((p["t"] for p in polls if p["out_m3"] > 1e-4), None)
        summary = {"rule": rule, "q": q, "manning_depth": dn, "out_over_in_last20s": out_rate / q, "mid_depth_m": last["mid_depth_m"],
                   "mid_speed_ms": last["mid_speed_ms"], "hidden": last["hidden"], "wet": last["wet"], "worst_hidden_m": last["worst_hidden_m"],
                   "first_outflow_s": first_out, "ledger_error_m3": last["ledger_error_m3"], "step_ms_max": max(p["step_ms"] for p in polls)}
        (EV / f"{tag}.json").write_text(json.dumps({"summary": summary, "polls": polls}, indent=1))
        print(json.dumps(summary))
        api.debug("water_columns", {"id": rid, "destroy": True})


if __name__ == "__main__":
    main()
