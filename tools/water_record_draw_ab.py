"""§27 W1a (the owner's choice A, 2026-10-20): does a pond look the same once it sleeps into the record? Same pose, same frame
crop: two frames while the pond is awake (the noise floor: what two awake frames differ by), one after it slept into the record
with the record drawn by the pond surface, and the control - the record not drawn (today's behaviour in the bench: the pond
vanishes, 27.8). Prediction written before the run: awake-vs-recorded within 2x the awake-vs-awake difference; awake-vs-off far
above it (the metric sees a missing pond).

    python tools/water_record_draw_ab.py [--tag w1a_draw] [--build-step]
Writes docs/evidence/water_core_h1/<tag>_{awake1,awake2,recorded,off}.png and <tag>.json.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
CROP = (243, 60, 243 + 954, 60 + 560)   # the viewport inside the editor window (gdigrab of the window title)


def grab(path):
    subprocess.run([shutil.which("ffmpeg"), "-y", "-loglevel", "error", "-f", "gdigrab", "-i", "title=WaterBench_Creek", "-frames:v", "1", str(path)], check=True)
    return np.asarray(Image.open(path).convert("RGB").crop(CROP), dtype=np.float32)


def diff(a, b):
    d = np.abs(a - b).max(axis=2)
    return {"mean": float(d.mean()), "px_over_16_pct": float(100.0 * (d > 16).mean()), "px_over_48_pct": float(100.0 * (d > 48).mean())}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--tag", default="w1a_draw")
    ap.add_argument("--build-step", action="store_true", help="author rig C's microcube step (x 2-7, z 10-15) first")
    args = ap.parse_args()
    api = Api(args.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    if args.build_step:
        items = [{"x": vx, "y": 16, "z": vz, "sx": k // 3, "sy": 0, "sz": j // 3, "mx": k % 3, "my": 0, "mz": j % 3, "material": "Stone"}
                 for vx in range(2, 8) for vz in range(10, 16) for k in range(5) for j in range(9)]
        print("rig C step:", api.post("/api/world/microcubes/batch", {"microcubes": items}))
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})
    box = {"x1": 2, "y1": 16, "z1": 0, "x2": 13, "y2": 17, "z2": 15}   # 12 x 16 m, rig C's step in its far corner
    print("record draw:", api.debug("water_av_record_draw", {"on": True, "radius": 48}))
    camera_set(api, {"x": 8.0, "y": 18.6, "z": 22.0, "yaw": -90.0, "pitch": -28.0})
    vid = api.debug("water_av_create", {**box, "cellSize": 1 / 9, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
    api.debug("place_water_box", {"target": "core", **{**box, "y2": 16}, "mass": 1.0})
    time.sleep(5.0)
    out = {"box": box}
    # the noise floor taken at the SAME gap as the recorded frame (~2.5 s): the grass sways in the wind, so two frames 0.5 s apart
    # under-read it (the first run: 2.07 mean at 0.5 s)
    a1 = grab(EV / f"{args.tag}_awake1.png"); time.sleep(2.5); a2 = grab(EV / f"{args.tag}_awake2.png")
    v = next(x for x in api.debug("water_av_list")["volumes"] if x["id"] == vid)
    out["awake"] = {k: v.get(k) for k in ("asleep", "awake_columns", "frozen_columns")}
    s = api.debug("water_av_sleep", {"id": vid})
    if not s.get("success"): out["sleep_unforced"] = s; s = api.debug("water_av_sleep", {"id": vid, "force": True})
    out["sleep"] = {k: s.get(k) for k in ("success", "forced", "columns", "mass_written", "spread_mm")}
    time.sleep(2.0)
    out["record_draw"] = api.debug("water_av_record_draw", {})
    rec = grab(EV / f"{args.tag}_recorded.png")
    api.debug("water_av_record_draw", {"on": False}); time.sleep(1.0)
    off = grab(EV / f"{args.tag}_off.png")
    api.debug("water_av_record_draw", {"on": True})
    out["awake_vs_awake"] = diff(a1, a2); out["awake_vs_recorded"] = diff(a1, rec); out["awake_vs_off"] = diff(a1, off)
    (EV / f"{args.tag}.json").write_text(json.dumps(out, indent=1))
    print(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
