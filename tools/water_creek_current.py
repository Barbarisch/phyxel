"""WaterCore.md 25.26 K - measure and tune the current on the simple creek bench (WaterBench_Creek, 25.24; the creek running at
steady flow - `python tools/water_creek_bench.py --run` first and give it ~2 minutes).

  profile : the creek's centre line, x 11-108, n stations - depth, speed, Froude, beside each station's Manning prediction
  floats  : wood blocks dropped at the head; each wet body's own velocity in x 25-65 m against the water's surface speed at
            its column, in simulation time (K-T2, 26.25)
  tune    : the volume's Manning n changed live (water_av_manning), the profile again once steady - the speed and depth
            ratios against Manning's (K-T3)
Pond water is the only model since WaterCore 26.27 (--pond is accepted and does nothing).

    python tools/water_creek_current.py [--q 0.2] [--floats 4] [--float-seconds 150] [--tune-n 0.05] [--settle 150]
Writes docs/evidence/water_core_h1/current_<part>_pond.json, current_floats_pond.mp4.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
ZC = 32.0   # the creek's centre line (voxels z 31-32)


# 26.24 P3d-1: the gauges on POND water (water_creek_bench.py --run; the only water model since WaterCore 26.27). Depth =
# the water in each voxel column (mass, 26.20 - not the drawn top), the mean speed by continuity (q / width depth: steady flow), the surface speed from the volume's
# field, Manning's speed and depth on the bed's overall 1:200 for the same unit discharge. Tune: water_av_manning {id, n}.
SLOPE, WIDTH, G = 1.0 / 200.0, 2.0, 9.81


def volume_id(api):
    vols = api.debug("water_av_list").get("volumes", [])
    if not vols: raise SystemExit("start the creek first: python tools/water_creek_bench.py --run, and let it settle")
    return vols[0]["id"], vols[0].get("manning_n", 0.0)


def profile_pond(api, q, n_bed, n=40):
    xs = [11.0 + (108.0 - 11.0) * k / (n - 1) for k in range(n)]
    pts = [[x, z + 0.5] for x in xs for z in (31, 32)]
    cols = api.debug("water_av_column", {"points": pts, "mass": True}).get("columns", [])
    st = []
    for k, x in enumerate(xs):
        pair = cols[2 * k:2 * k + 2]
        wet = [c for c in pair if c.get("surface_y") is not None and c.get("mass_depth", 0.0) > 1e-3]
        d = sum(c["mass_depth"] for c in wet) / len(wet) if wet else 0.0
        us = sum(c.get("u", 0.0) for c in wet) / len(wet) if wet else 0.0
        v = q / (WIDTH * d) if d > 1e-3 else 0.0
        st.append({"x": x, "wet": bool(wet), "depth_m": d, "speed_ms": v, "surface_speed_ms": us,
                   "manning_speed_ms": (d ** (2.0 / 3.0)) * SLOPE ** 0.5 / n_bed if d > 0 else 0.0,
                   "manning_depth_m": (q / WIDTH * n_bed / SLOPE ** 0.5) ** 0.6, "froude": v / (G * d) ** 0.5 if d > 1e-3 else 0.0})
    wet = [s for s in st if s["wet"]]
    mid = [s for s in wet if 25.0 <= s["x"] <= 85.0]
    mean = lambda k, rows: sum(r[k] for r in rows) / max(1, len(rows))
    return {"model": "pond", "q": q, "n": n_bed, "stations": st, "mid_speed_ms": mean("speed_ms", mid), "mid_surface_speed_ms": mean("surface_speed_ms", mid),
            "mid_depth_m": mean("depth_m", mid), "mid_manning_speed_ms": mean("manning_speed_ms", mid),
            "mid_manning_depth_m": mean("manning_depth_m", mid), "mid_froude": mean("froude", mid), "wet_stations": len(wet)}


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
    ap.add_argument("--pond", action="store_true", help="accepted, pond water is the only mode since WaterCore 26.27")
    ap.add_argument("--q", type=float, default=0.2, help="the creek's discharge (m^3/s), as started")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(60)
    EV.mkdir(parents=True, exist_ok=True)
    api.debug("water_coupling", {"enabled": True, "displace": True})
    return main_pond(api, args)


def floats_pond(api, count, seconds, size, tag):
    """26.25: the floats measured in SIMULATION time - each wet body's own reported velocity beside the water's surface speed at
    its column, sampled ~1 s apart while it is in x 25-65. The wall-clock crossing time (the old column-water floats()) read the
    frame rate when the engine ran slow (the debris clamps its step: at 7 FPS its clock ran at ~half the wall's)."""
    camera_set(api, {"x": 20.0, "y": 19.0, "z": 36.0, "yaw": -15.0, "pitch": -22.0}); time.sleep(1.5)
    ff = shutil.which("ffmpeg")
    rec = subprocess.Popen([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-t", str(min(seconds, 60)), "-i", "title=WaterBench_Creek",
                            "-vf", "crop=960:630:240:42", "-c:v", "libx264", "-crf", "30", "-pix_fmt", "yuv420p", str(EV / f"current_floats{tag}.mp4")]) if ff else None
    api.debug("clear_dynamics"); time.sleep(1.0)
    for k in range(count):
        api.debug("spawn_gpu_particle", {"x": 14.0 + 1.5 * k, "y": 16.4, "z": ZC - 0.5 + 0.3 * (k % 3), "material": "Wood", "scale": size, "lifetime": 900.0})
    t0 = time.time(); samples = []; fps = []
    while time.time() - t0 < seconds:
        r = api.debug("water_coupling", {})
        bodies = [b for b in (r.get("wet_body_list") or []) if 25.0 <= b["centre"][0] <= 65.0]
        if bodies:
            cols = api.debug("water_av_column", {"points": [[b["centre"][0], b["centre"][2]] for b in bodies]}).get("columns", [])
            for b, c in zip(bodies, cols):
                if c.get("surface_y") is not None: samples.append({"x": b["centre"][0], "vx": b["velocity"][0], "u_water": c.get("u", 0.0)})
        fps.append(api.get("/api/debug/engine_timing").get("fps", 0.0))
        time.sleep(1.0)
    if rec: rec.wait()
    mean = lambda k: sum(x[k] for x in samples) / max(1, len(samples))
    fps.sort()
    return {"samples": samples, "n": len(samples), "float_mean_vx": mean("vx"), "water_mean_u": mean("u_water"),
            "ratio": mean("vx") / max(1e-6, mean("u_water")), "fps_median": fps[len(fps) // 2] if fps else None}


def main_pond(api, args):
    vid, n0 = volume_id(api)
    tag = "_pond"
    base = profile_pond(api, args.q, n0)
    if "profile" in args.parts: print(f"K-T1 profile (pond, n {n0:.3f}, q {args.q}): mid mean speed {base['mid_speed_ms']:.3f} m/s (Manning {base['mid_manning_speed_ms']:.3f}), "
          f"surface {base['mid_surface_speed_ms']:.3f} m/s, mid depth {base['mid_depth_m']:.4f} m (Manning on the 1:200 line {base['mid_manning_depth_m']:.4f}), "
          f"Froude {base['mid_froude']:.2f}, wet stations {base['wet_stations']}")
    (EV / f"current_profile{tag}.json").write_text(json.dumps(base, indent=1))
    if "floats" in args.parts:
        fl = floats_pond(api, args.floats, args.float_seconds, args.float_size, tag)
        print(f"K-T2 floats (simulation time, {fl['n']} samples in x 25-65): float {fl['float_mean_vx']:.3f} m/s vs the water at their columns "
              f"{fl['water_mean_u']:.3f} m/s = {fl['ratio']:.3f}; FPS median {fl['fps_median']}")
        (EV / f"current_floats{tag}.json").write_text(json.dumps({**fl, "water_mid_speed_ms": base["mid_speed_ms"], "water_mid_surface_speed_ms": base["mid_surface_speed_ms"]}, indent=1))
    if "tune" not in args.parts: return
    r = api.debug("water_av_manning", {"id": vid, "n": args.tune_n})
    if "error" in r: raise SystemExit(r)
    time.sleep(args.settle)
    tuned = profile_pond(api, args.q, args.tune_n)
    rs, rd = tuned["mid_speed_ms"] / max(1e-6, base["mid_speed_ms"]), tuned["mid_depth_m"] / max(1e-6, base["mid_depth_m"])
    print(f"K-T3 tune n {n0:.3f} -> {args.tune_n}: mid mean speed {tuned['mid_speed_ms']:.3f} m/s (x {rs:.3f}; Manning x {(n0 / args.tune_n) ** 0.6:.3f}), "
          f"depth {tuned['mid_depth_m']:.4f} m (x {rd:.3f}; Manning x {(args.tune_n / n0) ** 0.6:.3f})")
    (EV / f"current_tuned{tag}.json").write_text(json.dumps({"n_from": n0, "n_to": args.tune_n, "speed_ratio": rs, "depth_ratio": rd,
                                                            "manning_speed_ratio": (n0 / args.tune_n) ** 0.6, "manning_depth_ratio": (args.tune_n / n0) ** 0.6, **tuned}, indent=1))
    api.debug("water_av_manning", {"id": vid, "n": n0})


if __name__ == "__main__":
    main()
