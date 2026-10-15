"""WaterCore.md 25.22 C-T10 - a GENERATED creek (WaterBench_RiverCarve: a regenerated copy of the River bench, creekCarve on,
the old CA water off). Maps the creek-bed columns around --near (`terrain_column`), takes the connected stretch within
--radius of it, puts a column-water region over it (spring at the highest bed column, outfall at the lowest), and runs each
bed rule from dry for --seconds: out/in, mid depth and speed, hidden columns, a clip from a camera above the stretch.

    python tools/water_creek_live.py --near -2140 -1740 [--radius 14] [--q 0.02] [--seconds 150] [--rules generator voxel]
Writes docs/evidence/water_core_h1/gencreek_<rule>.{json,mp4} and gencreek_map.json.
"""
import argparse, json, math, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8113")
    ap.add_argument("--window", default="WaterBench_RiverCarve")
    ap.add_argument("--near", nargs=2, type=int, required=True)
    ap.add_argument("--radius", type=int, default=14)
    ap.add_argument("--q", type=float, default=0.02)
    ap.add_argument("--seconds", type=float, default=150.0)
    ap.add_argument("--rules", nargs="+", default=["generator", "voxel"])
    ap.add_argument("--cam", nargs=5, type=float, default=None, help="x y z yaw pitch for the clip (default: a view of the stretch)")
    ap.add_argument("--tag", default="", help="suffix for the evidence files")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(120)
    EV.mkdir(parents=True, exist_ok=True)
    nx_, nz_ = args.near
    # stream the place in: put the camera over it and wait until the ground answers
    camera_set(api, {"x": nx_, "y": 140.0, "z": nz_ - 30, "yaw": 90.0, "pitch": -45.0})
    for _ in range(120):
        h = api.get(f"/api/world/terrain_height?x={nx_}&z={nz_}&min_y=-64&max_y=1024").get("surface_y")
        if h is not None: break
        time.sleep(1.0)
    else:
        raise SystemExit("the creek's chunks never became resident")
    # map the creek-bed columns in a square around the point (1 m grid)
    R = args.radius + 4
    pts = [[nx_ + i, nz_ + j] for i in range(-R, R + 1) for j in range(-R, R + 1)]
    cols = {}
    for k in range(0, len(pts), 400):
        for c in api.debug("terrain_column", {"points": pts[k:k + 400]}, timeout=120).get("columns", []):
            cols[(c["x"], c["z"])] = c
    creek = {k for k, c in cols.items() if c["creek_bed"]}
    if not creek: raise SystemExit("no creek-bed column near the point")
    seed = min(creek, key=lambda k: (k[0] - nx_) ** 2 + (k[1] - nz_) ** 2)
    comp, stack = {seed}, [seed]
    while stack:   # the connected creek (8-neighbour), limited to the radius
        x, z = stack.pop()
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                n = (x + dx, z + dz)
                if n in creek and n not in comp and (n[0] - nx_) ** 2 + (n[1] - nz_) ** 2 <= args.radius ** 2:
                    comp.add(n); stack.append(n)
    top = max(comp, key=lambda k: cols[k]["bed_height"]); low = min(comp, key=lambda k: cols[k]["bed_height"])
    span = math.dist(top, low)
    slope = (cols[top]["bed_height"] - cols[low]["bed_height"]) / max(span, 1.0)
    width = len(comp) / max(span, 1.0)
    xs = [k[0] for k in comp]; zs = [k[1] for k in comp]
    beds = [cols[k]["bed_height"] for k in comp]
    box = {"x1": min(xs) - 3, "z1": min(zs) - 3, "x2": max(xs) + 3, "z2": max(zs) + 3,
           "y1": int(math.floor(min(beds))) - 1, "y2": int(math.ceil(max(beds))) + 3}
    print(f"creek stretch: {len(comp)} bed columns, from {top} ({cols[top]['bed_height']:.2f}) to {low} ({cols[low]['bed_height']:.2f}), "
          f"{span:.1f} m apart, slope {slope:.4f}, mean width {width:.1f} m; region {box}")
    (EV / "gencreek_map.json").write_text(json.dumps({"near": args.near, "box": box, "slope": slope, "width": width,
                                                     "columns": [cols[k] for k in sorted(comp)]}, indent=1))
    n = 0.035
    dn = ((args.q / max(width, 0.5)) * n / math.sqrt(max(slope, 1e-4))) ** 0.6
    api.debug("water_av_realtime", {"on": True})
    for old in api.debug("water_columns", {}).get("regions", []): api.debug("water_columns", {"id": old["id"], "destroy": True})
    cxm, czm = sum(xs) / len(xs), sum(zs) / len(zs)
    ff = shutil.which("ffmpeg")
    for rule in args.rules:
        r = api.debug("water_columns", {"create": box, "manning_n": n})
        if "error" in r: raise SystemExit(r)
        rid = r["id"]
        api.debug("water_columns", {"id": rid, "bed": rule})
        for i in range(3):
            for j in range(3):
                api.debug("water_columns", {"id": rid, "outfall": {"x": low[0] + (i + 0.5) / 3.0, "z": low[1] + (j + 0.5) / 3.0}})
        # camera: above and to the side of the stretch's middle, looking at it
        cam = {"x": cxm - 9.0, "y": max(beds) + 9.0, "z": czm - 9.0}
        yaw = math.degrees(math.atan2(czm - cam["z"], cxm - cam["x"]))
        pitch = -math.degrees(math.atan2(cam["y"] - (sum(beds) / len(beds)), math.hypot(cxm - cam["x"], czm - cam["z"])))
        if args.cam: cam = {"x": args.cam[0], "y": args.cam[1], "z": args.cam[2]}; yaw, pitch = args.cam[3], args.cam[4]
        camera_set(api, {**cam, "yaw": yaw, "pitch": pitch})
        time.sleep(3.0)
        mp4 = EV / f"gencreek_{rule}{args.tag}.mp4"
        rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(args.seconds),
                                "-i", f"title={args.window}", "-c:v", "libx264", "-crf", "24", "-pix_fmt", "yuv420p", str(mp4)]) if ff else None
        t0 = time.time()
        api.debug("water_columns", {"id": rid, "spring": {"x": top[0] + 0.5, "z": top[1] + 0.5, "rate": args.q}})
        polls = []
        while time.time() - t0 < args.seconds:
            g = next(x for x in api.debug("water_columns", {}).get("regions", []) if x["id"] == rid)
            polls.append({"t": round(time.time() - t0, 2), **{k: g[k] for k in ("in_m3", "out_m3", "stored_m3", "ledger_error_m3", "step_ms", "wet_columns", "bed")}, **g["stats"]})
            time.sleep(0.5)
        if rec: rec.wait()
        w = [p for p in polls if p["t"] >= args.seconds - 20.0]
        out_rate = (w[-1]["out_m3"] - w[0]["out_m3"]) / max(1e-6, w[-1]["t"] - w[0]["t"])
        last = polls[-1]
        summary = {"rule": rule, "bed_reported": last["bed"], "q": args.q, "slope": slope, "width": width, "manning_depth": dn,
                   "out_over_in_last20s": out_rate / args.q, "first_outflow_s": next((p["t"] for p in polls if p["out_m3"] > 1e-4), None),
                   "mid_depth_m": last["mid_depth_m"], "mid_speed_ms": last["mid_speed_ms"], "wet": last["wet"], "hidden": last["hidden"],
                   "worst_hidden_m": last["worst_hidden_m"], "ledger_error_m3": last["ledger_error_m3"], "step_ms_max": max(p["step_ms"] for p in polls)}
        (EV / f"gencreek_{rule}{args.tag}.json").write_text(json.dumps({"summary": summary, "polls": polls}, indent=1))
        print(json.dumps(summary))
        api.debug("water_columns", {"id": rid, "destroy": True})


if __name__ == "__main__":
    main()
