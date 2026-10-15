"""WaterCore 21.3 C-T1: how much of the pond's floor shows through the water. A rested Small-bench pond
(1/3 m cells) with one stone resting on its floor, the motion-check pose, the same frame drawn with the
in-scatter LIT (21.3) and LEGACY (the pre-21.3 constant glow = the control, which must reproduce the
measured 0.039). Pond pixels = where the frame with water differs from the frame with the water mesh off.

Per mode, over the pond pixels (shipped tone map unless stated):
  shaded / floor        mean sRGB of the water vs the floor seen with the water off
  body_lin_g            the body tap (debug 2) at exposure 1 + tone map off, G channel, linear
  detail_ratio          spatial std of the shaded pond / std of the dry floor (G) - how much of the floor's
                        texture and the stone survive the water (1 = all, 0 = an opaque sheet)
Predictions (21.6 C-T1, written before building): lit - body_lin_g within 10 % of 0.83 x the floor's
linear G (0.0095 -> ~0.0079); detail_ratio >= 0.7. Legacy - body_lin_g ~0.039, detail_ratio well below.

Usage: python tools/water_clarity_probe.py [--url http://127.0.0.1:8111] [--tag s0]"""
import argparse, json, shutil, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import numpy as np
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("--url", default="http://127.0.0.1:8111")
ap.add_argument("--tag", default="s0")
args = ap.parse_args()
EV = Path(__file__).resolve().parents[1] / "docs" / "evidence" / "water_core_e"
OUT = EV / f"{args.tag}_clarity"; OUT.mkdir(exist_ok=True)
api = Api(args.url); api.wait_status(300)
for v in api.debug("water_av_list").get("volumes", []):
    api.debug("water_av_destroy", {"id": v["id"]})
api.debug("water_av_create", {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16, "cellSize": 1.0 / 3.0,
                              "transport": "eulerian", "backend": "auto", "auto_sleep": False})
api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
api.debug("water_av_realtime", {"on": True})
api.debug("water_coupling", {"enabled": True, "solids": True})
api.debug("spawn_gpu_particle", {"x": 101.5, "y": 17.5, "z": 13.5, "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 120.0})
POSE = {"x": 109.5, "y": 20.5, "z": 13.5, "yaw": 180, "pitch": -38}
camera_set(api, POSE)
time.sleep(10)


def shot(name):
    time.sleep(0.8)
    p = Path(api.get("/api/screenshot")["path"])
    if not p.is_absolute(): p = Path(__file__).resolve().parents[1] / p
    dst = OUT / f"{name}.png"; shutil.copy(p, dst)
    return np.asarray(Image.open(dst).convert("RGB")).astype(np.float32)


def lin(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


api.debug("tonemap", {"curve": 1, "exposure": 8.0})
api.debug("water_render_core", {"mode": "off"}); off = shot("water_off")
api.debug("tonemap", {"curve": 0, "exposure": 8.0}); off_c0 = shot("water_off_curve0")   # tone map off: lin(sRGB)/8 = the floor's radiance
res = {"pose": POSE}
mask = None
for mode in ("legacy", "lit"):
    api.debug("tonemap", {"curve": 1, "exposure": 8.0})
    echo = api.debug("water_render_core", {"mode": "mesh", "debug": 0, "scatter": mode})
    on = shot(f"{mode}_shaded")
    if mask is None:
        mask = np.abs(on - off).sum(axis=2) > 6   # the pond, from the legacy frame (the glow marks every water pixel)
    api.debug("tonemap", {"curve": 0, "exposure": 1.0})
    api.debug("water_render_core", {"debug": 2}); body = shot(f"{mode}_body")
    api.debug("water_render_core", {"debug": 0})
    floor_lin_g = float(lin(off_c0[..., 1][mask]).mean()) / 8.0   # the floor's linear G before the x8 exposure
    res[mode] = {"echo_scatter": echo.get("scatter"),
                 "shaded": on[mask].mean(0).round(1).tolist(), "floor": off[mask].mean(0).round(1).tolist(),
                 "body_lin_g": round(float(lin(body[..., 1][mask]).mean()), 5),
                 "floor_lin_g": round(floor_lin_g, 5),
                 "detail_ratio": round(float(on[..., 1][mask].std() / max(off[..., 1][mask].std(), 1e-3)), 3)}
api.debug("tonemap", {"curve": 1, "exposure": 8.0})
api.debug("water_render_core", {"scatter": "lit"})
res["pond_pixels"] = int(mask.sum())
lit, leg = res["lit"], res["legacy"]
res["verdict"] = {
    "lit_body_within_10pct_of_0.83_floor": abs(lit["body_lin_g"] - 0.83 * lit["floor_lin_g"]) <= 0.10 * 0.83 * lit["floor_lin_g"],
    "lit_detail_ratio_ge_0.7": lit["detail_ratio"] >= 0.7,
    "legacy_reproduces_0.039": abs(leg["body_lin_g"] - 0.039) <= 0.004,
}
(EV / f"{args.tag}_clarity.json").write_text(json.dumps(res, indent=1), encoding="utf-8")
print(json.dumps(res, indent=1))
