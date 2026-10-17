"""26.21 live A/B on WaterBench_Creek (the creek already running as pond water): the volume correction ON / OFF / ON on the same
running engine - per phase, after a settle wait: the water's GPU device ms and FPS (20 x 1 s), the mean drawn surface and the
water's mass depth over the whole reaches (tools/water_creek_profile.py --stat mean|mass).

    python tools/water_vc_ab.py <q> [settle_s]    # the creek must already run at q (water_creek_bench.py --run --pond --q <q>)

Note: the profile tool rewrites creekbench_pond_g9_q<Q>_{mean,mass}_profile.json each phase (26.20's evidence) - restore them
with git checkout if they should stay the 26.20 "before"."""
import json, statistics, subprocess, sys, time, pathlib
REPO = pathlib.Path(r"G:/Github/phyxel")
sys.path.insert(0, str(REPO / "tools"))
from water_bench import Api

q = float(sys.argv[1]); settle = float(sys.argv[2]) if len(sys.argv) > 2 else 150.0
api = Api("http://127.0.0.1:8114")
rows = []
for phase, t in (("on", 0.5), ("off", 0.0), ("on again", 0.5)):
    echo = api.debug("water_volume_correction", {"time_s": t})
    time.sleep(settle)
    dev, fps = [], []
    for _ in range(20):
        v = api.debug("water_av_list")["volumes"][0]
        dev.append(v.get("gpu_device_ms", 0.0)); fps.append(api.get("/api/debug/engine_timing").get("fps", 0.0))
        time.sleep(1.0)
    prof = {}
    for st in ("mean", "mass"):
        out = subprocess.run([sys.executable, str(REPO / "tools/water_creek_profile.py"), "--q", f"{q:g}", "--stat", st], capture_output=True, text=True, cwd=REPO).stdout.strip().splitlines()
        prof[st] = json.loads(out[-1])["whole_reach_mean_depth"]
    row = {"phase": phase, "echo": echo, "gpu_device_ms_median": statistics.median(dev), "gpu_device_ms_max": max(dev), "fps_median": statistics.median(fps),
           "mean_surface_depth_m": prof["mean"], "mass_depth_m": prof["mass"], "surface_above_water_mm": 1000 * (prof["mean"] - prof["mass"])}
    print(json.dumps(row), flush=True); rows.append(row)
(REPO / f"docs/evidence/water_core_h1/creekbench_pond_g9_q{q:g}_vc_ab.json").write_text(json.dumps(rows, indent=1))
