"""WaterCore.md 25.26 K - measure and tune the current on the simple creek bench (WaterBench_Creek, 25.24; the creek running at
steady flow - `python tools/water_creek_bench.py --run` first and give it ~2 minutes).

  profile : the creek's centre line, x 11-108, n stations - depth, speed, Froude, beside each station's Manning prediction
  floats  : wood blocks dropped at the head, tracked through the GPU debris position log (CSV); each float's drift speed over
            x 25-85 m against the column velocity along its path (K-T2)
  tune    : `manning_n` changed live, the profile again once steady - the speed ratio against Manning's (K-T3)

    python tools/water_creek_current.py [--floats 4] [--float-seconds 150] [--tune-n 0.05] [--settle 150]
Writes docs/evidence/water_core_h1/current_<part>.json, current_floats.csv, current_floats.mp4.
"""
import argparse, csv, json, math, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
ZC = 32.0   # the creek's centre line (voxels z 31-32)


def region_id(api):
    regs = api.debug("water_columns", {}).get("regions", [])
    if not regs or regs[0]["wet_columns"] < 200: raise SystemExit("start the creek first: python tools/water_creek_bench.py --run, and let it settle")
    return regs[0]["id"]


def profile(api, rid, n=40):
    st = api.debug("water_columns", {"id": rid, "profile": {"x0": 11.0, "z0": ZC, "x1": 108.0, "z1": ZC, "n": n}})["profile"]
    wet = [s for s in st if s["wet"]]
    mid = [s for s in wet if 25.0 <= s["x"] <= 85.0]
    mean = lambda k, rows: sum(r[k] for r in rows) / max(1, len(rows))
    return {"stations": st, "mid_speed_ms": mean("speed_ms", mid), "mid_depth_m": mean("depth_m", mid),
            "mid_manning_speed_ms": mean("manning_speed_ms", mid), "mid_manning_depth_m": mean("manning_depth_m", mid),
            "mid_froude": mean("froude", mid), "wet_stations": len(wet)}


def floats(api, rid, count, seconds, size):
    csv_path = EV / "current_floats.csv"
    if csv_path.exists(): csv_path.unlink()
    camera_set(api, {"x": 20.0, "y": 19.0, "z": 36.0, "yaw": -15.0, "pitch": -22.0}); time.sleep(1.5)
    ff = shutil.which("ffmpeg")
    rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(min(seconds, 60)), "-i", "title=WaterBench_Creek",
                            "-vf", "crop=960:630:240:42", "-c:v", "libx264", "-crf", "30", "-pix_fmt", "yuv420p", str(EV / "current_floats.mp4")]) if ff else None
    api.debug("clear_dynamics")   # earlier floats (grounded or jammed) out of the creek
    time.sleep(1.0)
    api.debug("debris_position_log", {"path": str(csv_path)})
    for k in range(count):
        api.debug("spawn_gpu_particle", {"x": 14.0 + 1.5 * k, "y": 16.4, "z": ZC - 0.5 + 0.3 * (k % 3), "material": "Wood", "scale": size, "lifetime": 900.0})
    time.sleep(seconds)
    api.debug("debris_position_log", {"stop": True})
    if rec: rec.wait()
    # parse. TIME BASE: the log writes a frame only when its GPU readback is ready (about one frame in three), so the F rows'
    # dt sum to a third of the run (measured 2026-10-12: 50 s of dt in a 150 s run); logged frames are evenly spaced in
    # wall time, so a frame's time is its index / count x the run's wall duration
    frames = []
    with open(csv_path, newline="") as fh:
        for row in csv.reader(fh):
            if row and row[0] == "F": frames.append(int(row[1]))
    t_of = {f: i / max(1, len(frames) - 1) * seconds for i, f in enumerate(frames)}; t = 0.0; tracks = {}
    with open(csv_path, newline="") as fh:
        for row in csv.reader(fh):
            if not row or row[0].startswith("#"): continue
            if row[0] == "F": t = t_of.get(int(row[1]), t)
            elif row[0] == "P":
                frame, slot = int(row[1]), int(row[2]); x, y, z = float(row[3]), float(row[4]), float(row[5])
                if 30.0 <= z <= 34.0 and 14.0 <= y <= 18.0: tracks.setdefault(slot, []).append((t_of.get(frame, t), x))
    out = []
    for slot, pts in tracks.items():
        pts.sort()
        def cross(xv):
            return next((tt for tt, xx in pts if xx >= xv), None)
        t25, t65 = cross(25.0), cross(65.0)   # 40 m: a float at ~0.4 m/s crosses it inside a 150 s window
        if t25 is not None and t65 is not None and t65 > t25:
            out.append({"slot": slot, "drift_speed_ms": 40.0 / (t65 - t25), "t25": t25, "t65": t65, "x_last": pts[-1][1]})
        else:
            out.append({"slot": slot, "drift_speed_ms": None, "x_last": pts[-1][1], "note": "did not cross x 25-65 inside the window"})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--floats", type=int, default=4)
    # a float must FLOAT: a wood cube of side s draws ~0.6 s; on the 11 cm creek a 1/3 m block (20 cm draft) grounds and is
    # shoved along the bed - measured 2026-10-12 - so the default float is 1/9 m (7 cm draft)
    ap.add_argument("--float-size", type=float, default=1.0 / 9.0)
    ap.add_argument("--parts", nargs="+", default=["profile", "floats", "tune"])
    ap.add_argument("--float-seconds", type=float, default=150.0)
    ap.add_argument("--tune-n", type=float, default=0.05)
    ap.add_argument("--settle", type=float, default=150.0)
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(60)
    EV.mkdir(parents=True, exist_ok=True)
    api.debug("water_coupling", {"enabled": True})
    rid = region_id(api)
    n0 = next(g for g in api.debug("water_columns", {})["regions"] if g["id"] == rid)["manning_n"]
    base = profile(api, rid)
    if "profile" in args.parts: print(f"K-T1 profile (n {n0}): mid speed {base['mid_speed_ms']:.3f} m/s (Manning {base['mid_manning_speed_ms']:.3f}), "
          f"mid depth {base['mid_depth_m']:.4f} m (Manning {base['mid_manning_depth_m']:.4f}), Froude {base['mid_froude']:.2f}, wet stations {base['wet_stations']}")
    (EV / "current_profile.json").write_text(json.dumps(base, indent=1))
    if "floats" not in args.parts and "tune" not in args.parts: return
    fl = floats(api, rid, args.floats, args.float_seconds, args.float_size) if "floats" in args.parts else []
    speeds = [f["drift_speed_ms"] for f in fl if f.get("drift_speed_ms")]
    print(f"K-T2 floats: {len(speeds)} of {len(fl)} crossed x 25-65; drift speeds {[round(s, 3) for s in speeds]} vs the water's {base['mid_speed_ms']:.3f} m/s")
    (EV / "current_floats.json").write_text(json.dumps({"floats": fl, "water_mid_speed_ms": base["mid_speed_ms"]}, indent=1))
    if "tune" not in args.parts: return
    api.debug("water_columns", {"id": rid, "manning_n": args.tune_n})
    time.sleep(args.settle)
    tuned = profile(api, rid)
    ratio = tuned["mid_speed_ms"] / max(1e-6, base["mid_speed_ms"])
    print(f"K-T3 tune n {n0} -> {args.tune_n}: mid speed {tuned['mid_speed_ms']:.3f} m/s, ratio {ratio:.3f} (Manning {(n0 / args.tune_n) ** 0.6:.3f})")
    (EV / "current_tuned.json").write_text(json.dumps({"n_from": n0, "n_to": args.tune_n, "ratio": ratio, "manning_ratio": (n0 / args.tune_n) ** 0.6, **tuned}, indent=1))
    api.debug("water_columns", {"id": rid, "manning_n": n0})


if __name__ == "__main__":
    main()
