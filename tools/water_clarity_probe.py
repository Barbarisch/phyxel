"""Why does the Small pond read milky? Debug taps of water_common.glsl on a rested pond with a stone on its
floor, at the motion-check pose, tone map off (curve 0). Pond pixels = where water on differs from water off."""
import sys, time, json, shutil
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set
import numpy as np
from PIL import Image

OUT = Path(__file__).resolve().parents[1] / "docs" / "evidence" / "water_core_e" / "clarity"
OUT.mkdir(exist_ok=True)
api = Api("http://127.0.0.1:8111"); api.wait_status(120)
for v in api.debug("water_av_list").get("volumes", []):
    api.debug("water_av_destroy", {"id": v["id"]})
api.debug("water_av_create", {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16, "cellSize": 1.0 / 3.0,
                              "transport": "eulerian", "backend": "auto", "auto_sleep": False})
api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
api.debug("water_av_realtime", {"on": True})
api.debug("water_coupling", {"enabled": True, "solids": True})
api.debug("spawn_gpu_particle", {"x": 101.5, "y": 17.5, "z": 13.5, "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 60.0})
camera_set(api, {"x": 109.5, "y": 20.5, "z": 13.5, "yaw": 180, "pitch": -38})
time.sleep(8)
print("look", api.debug("water_look", {}))
print("tonemap", api.debug("tonemap", {"curve": 0}))

def shot(name):
    time.sleep(0.6)
    p = Path(api.get("/api/screenshot")["path"])
    if not p.is_absolute(): p = Path(r"G:/Github/phyxel") / p
    dst = OUT / f"{name}.png"; shutil.copy(p, dst)
    return np.asarray(Image.open(dst).convert("RGB")).astype(np.float32)

api.debug("water_render_core", {"mode": "off"}); off = shot("water_off")
api.debug("water_render_core", {"mode": "mesh", "debug": 0}); on = shot("shaded")
mask = np.abs(on - off).sum(axis=2) > 6
print("pond pixels", int(mask.sum()))
res = {"off": off[mask].mean(0).round(1).tolist(), "shaded": on[mask].mean(0).round(1).tolist()}
api.debug("tonemap", {"curve": 0, "exposure": 1.0})
for d, name in [(2, "body"), (3, "reflection"), (4, "thickness"), (5, "fresnel")]:
    api.debug("water_render_core", {"debug": d}); img = shot(name)
    res[name] = img[mask].mean(0).round(1).tolist()
    res[name + "_std"] = img[mask].std(0).round(1).tolist()
api.debug("water_render_core", {"debug": 0})
api.debug("tonemap", {"curve": 1, "exposure": 8.0})
print(json.dumps(res, indent=1))
