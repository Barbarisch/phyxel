"""WaterCore §55.2 (W6.1b) live (L4): the old sea sheet deleted - the same poses before (the old build) and after (the sheet gone).

The two benches whose game.json turns the sheet on (water.enabled true): WaterBench_Coast (8109) and WaterBench_River (8110).
Per pose: a still (gdigrab) and whole-frame CPU / GPU / fps (median of --hold s after --settle s). Per session: the sheet's grid
rebuilds read from the engine log (`Span water grid: ...` lines - each a vkDeviceWaitIdle, `idle wait N ms`) since the run started.
Prediction (written before the run): the stills before vs after differ only where wind moves grass and leaves (step 0: Coast's
sheet on vs off 0.0004-2.1 % of pixels off by > 8, the blade band); before: grid rebuilds > 0 with idle waits of several ms each;
after: 0 rebuilds; whole frame after <= before within the run-to-run spread (the sheet's clipmap draw and the stalls gone).

    python tools/water_w61b_sheet.py --bench coast|river --state before|after
    python tools/water_w61b_sheet.py --compare            # pixel diffs before vs after, per pose
Writes docs/evidence/water_core_m4/w61b_<bench>_<state>{.json,_<pose>.png} and w61b_compare.json.
"""
import argparse, json, pathlib, re, shutil, statistics, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

ROOT = pathlib.Path(__file__).resolve().parent.parent
EV = ROOT / "docs" / "evidence" / "water_core_m4"
LOG = ROOT / "phyxel.log"
BENCHES = {
    "coast": {"url": "http://127.0.0.1:8109", "window": "WaterBench_Coast", "poses": {
        "spawn": {"x": 165.0, "y": 50.0, "z": 600.0, "yaw": 90.0, "pitch": -28.0},
        "shore": {"x": 165.0, "y": 18.0, "z": 668.0, "yaw": 90.0, "pitch": -8.0},
        "high": {"x": 165.0, "y": 136.0, "z": 600.0, "yaw": 90.0, "pitch": -12.0}}},
    "river": {"url": "http://127.0.0.1:8110", "window": "WaterBench_River", "poses": {
        "spawn": {"x": -1728.0, "y": 115.0, "z": -1320.0, "yaw": 90.0, "pitch": -28.0},      # the shipped camera: along the trunk
        "bank": {"x": -1716.0, "y": 40.0, "z": -1180.0, "yaw": 90.0, "pitch": -20.0}}},     # over the player's bank, near water
}
GRID = re.compile(r"Span water grid: .*idle wait ([0-9.]+) ms")


def grab(window, crop, path):
    subprocess.run([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-i", f"title={window}",
                    "-frames:v", "1", "-vf", f"crop={crop}", str(path)], check=True)


def run(a):
    b = BENCHES[a.bench]
    logStart = LOG.stat().st_size if LOG.exists() else 0
    api = Api(b["url"]); api.wait_status(600)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    out = {"bench": a.bench, "state": a.state, "status": api.get("/api/status"), "water_stats": api.debug("water_stats", {}), "poses": {}}
    for name, pose in b["poses"].items():
        camera_set(api, pose); time.sleep(a.settle); api.get("/api/debug/engine_timing")
        samples = []
        t0 = time.time()
        while time.time() - t0 < a.hold:
            time.sleep(1.0)
            e = api.get("/api/debug/engine_timing")
            samples.append({"fps": e.get("fps"), "cpu_ms": e.get("cpuFrameTime"), "gpu_ms": e.get("gpu_frame_ms")})
        p = EV / f"w61b_{a.bench}_{a.state}_{name}.png"; grab(b["window"], a.crop, p)
        med = {k: statistics.median([x[k] for x in samples if x[k] is not None]) for k in ("fps", "cpu_ms", "gpu_ms")}
        out["poses"][name] = {"pose": pose, "still": p.name, **med}
        print(a.bench, a.state, name, med, flush=True)
    waits = []
    if LOG.exists():
        with open(LOG, "rb") as f:
            f.seek(logStart)
            for line in f.read().decode("utf-8", "replace").splitlines():
                m = GRID.search(line)
                if m: waits.append(float(m.group(1)))
    out["sheet_grid_rebuilds"] = {"count": len(waits), "idle_wait_ms_total": round(sum(waits), 2),
                                  "idle_wait_ms_max": max(waits) if waits else 0.0}
    print("sheet grid rebuilds", out["sheet_grid_rebuilds"], flush=True)
    (EV / f"w61b_{a.bench}_{a.state}.json").write_text(json.dumps(out, indent=1))
    print("wrote", EV / f"w61b_{a.bench}_{a.state}.json")


def compare():
    from PIL import Image
    import numpy as np
    res = {}
    for bench, b in BENCHES.items():
        for name in b["poses"]:
            pa, pb = EV / f"w61b_{bench}_before_{name}.png", EV / f"w61b_{bench}_after_{name}.png"
            if not (pa.exists() and pb.exists()): continue
            x = np.asarray(Image.open(pa).convert("RGB")).astype(int); y = np.asarray(Image.open(pb).convert("RGB")).astype(int)
            d = np.abs(x - y).max(axis=2)
            res[f"{bench}_{name}"] = {"px_off_gt8": float((d > 8).mean()), "px_off_gt32": float((d > 32).mean())}
            print(bench, name, res[f"{bench}_{name}"])
    (EV / "w61b_compare.json").write_text(json.dumps(res, indent=1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench", choices=list(BENCHES))
    ap.add_argument("--state")   # before | after | before2 | after2 (a second, settled pass)
    ap.add_argument("--compare", action="store_true")
    ap.add_argument("--crop", default="954:560:243:60")
    ap.add_argument("--settle", type=float, default=20.0)
    ap.add_argument("--hold", type=float, default=10.0)
    a = ap.parse_args()
    if a.compare: compare()
    else: run(a)


if __name__ == "__main__":
    main()
