"""WaterCore 40.1 item 4 (L1) - nothing L1 moves may change what is drawn: one window capture of the Small bench pond, slept into the
record (drawn by the record's tiles, the thing `draw_m` reaches) and the same pose every time, compared pixel for pixel.

    python tools/water_l1_pixels.py capture --tag before_a      # on the build before L1 (twice: the control's spread)
    python tools/water_l1_pixels.py capture --tag after
    python tools/water_l1_pixels.py capture --tag draw0 --draw-m 0   # the control: the record not drawn - the compare must see it
    python tools/water_l1_pixels.py compare before_a before_b after draw0
Captures: docs/evidence/water_core_m4/l1_px_<tag>.png (gdigrab: /api/screenshot omits pond water). The compare prints, for each
pair, the mean absolute difference (0-255), the largest, and the share of pixels that differ by more than 8.
"""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import water_float_rest as w

POSE = {"x": 102.0, "y": 18.4, "z": 20.5, "yaw": -90, "pitch": -22}
CROP = "954:560:243:60"


def capture(a):
    ff = shutil.which("ffmpeg")
    if not ff: raise SystemExit("ffmpeg not on PATH")
    api = Api(a.url); api.wait_status(300)
    try: api.debug("editor_panels", {"item_equipper": False, "tool_panels": False})
    except Exception as e: print("editor_panels:", e)
    api.debug("clear_dynamics")
    w.AUTO_SLEEP = True; w.PRESSURE = True; w.DISPLACE = False
    w.pond(api)
    t0 = time.time()
    while api.debug("water_av_list").get("volumes") and time.time() - t0 < 60.0: time.sleep(0.5)
    slept = not api.debug("water_av_list").get("volumes")
    if a.draw_m is not None: api.debug("water_locality", {"draw_m": a.draw_m})   # the metric's control: the record not drawn
    camera_set(api, POSE)
    time.sleep(3.0)
    out = w.EV / f"l1_px_{a.tag}.png"
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "gdigrab", "-i", f"title={a.window}", "-frames:v", "1", "-vf", f"crop={CROP}",
                    str(out)], check=True)
    meta = {"tag": a.tag, "slept": slept, "pose": POSE, "record_draw": api.debug("water_av_record_draw", {})}
    try: meta["locality"] = api.debug("water_locality", {})
    except Exception as e: meta["locality"] = f"none ({e})"
    if a.draw_m is not None: api.debug("water_locality", {"draw_m": 48.0})   # back to the default
    (w.EV / f"l1_px_{a.tag}.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    print("wrote", out, "slept", slept)


def compare(a):
    import numpy as np
    from PIL import Image
    imgs = {t: np.asarray(Image.open(w.EV / f"l1_px_{t}.png").convert("RGB")).astype(np.int16) for t in a.tags}
    rows = []
    for i, t0 in enumerate(a.tags):
        for t1 in a.tags[i + 1:]:
            d = np.abs(imgs[t0] - imgs[t1])
            rows.append({"a": t0, "b": t1, "mean": round(float(d.mean()), 4), "max": int(d.max()),
                         "share_over_8": round(float((d.max(axis=2) > 8).mean()), 6)})
            print(rows[-1])
    (w.EV / "l1_px_compare.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture"); c.add_argument("--tag", required=True)
    c.add_argument("--draw-m", type=float, default=None, help="water_locality draw_m for this capture (the control: 0)")
    c.add_argument("--url", default="http://127.0.0.1:8111"); c.add_argument("--window", default="WaterBench_Small")
    m = sub.add_parser("compare"); m.add_argument("tags", nargs="+")
    a = ap.parse_args()
    capture(a) if a.cmd == "capture" else compare(a)


if __name__ == "__main__":
    main()
