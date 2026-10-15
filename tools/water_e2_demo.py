"""WaterCore E2 (docs/WaterCore.md 19) live on the Small bench pond: debris dropped into the pond.
10 Stone and 10 Wood subcubes (1/3 m) fall 3 m into a 1.5 m deep pond (surface 16.5, floor top 15).
Predictions (written before the run): the surface deviates >= 5 cm within 0.5 s of entry; Stone ends
on the floor (centre y ~ 15.17); Wood floats at its draft (buoyancy 1.429 -> 70 % submerged, centre
y ~ 16.43 +- 0.05); the pond's mass is unchanged; momentum given to the water > 0. Control: the same
drop with coupling OFF -> the surface does not move (debris then cannot see the pond either).
Usage: python tools/water_e2_demo.py [--url http://127.0.0.1:8111] [--backend auto]"""
import argparse, json, math, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set
from water_feel import capture

EV = Path(__file__).resolve().parents[1] / "docs" / "evidence" / "water_core_e"
EV.mkdir(parents=True, exist_ok=True)
ap = argparse.ArgumentParser()
ap.add_argument("--url", default="http://127.0.0.1:8111")
ap.add_argument("--backend", default="auto")
args = ap.parse_args()
api = Api(args.url); api.wait_status(900)
POSE = {"x": 109.5, "y": 20.5, "z": 13.5, "yaw": 180, "pitch": -38}
REST = 16.5

def make_pond():
    for v in api.debug("water_av_list").get("volumes", []):
        api.debug("water_av_destroy", {"id": v["id"]})
    cr = api.debug("water_av_create", {"x1": 99, "y1": 14, "z1": 11, "x2": 104, "y2": 19, "z2": 16, "cellSize": 1.0 / 3.0,
                                       "transport": "eulerian", "backend": args.backend, "auto_sleep": False})
    if "error" in cr: raise SystemExit(f"create: {cr}")
    api.debug("place_water_box", {"x1": 100, "y1": 15, "z1": 12, "x2": 103, "y2": 15, "z2": 15, "mass": 1.0, "target": "core"})
    api.debug("place_water_box", {"x1": 100, "y1": 16, "z1": 12, "x2": 103, "y2": 16, "z2": 15, "mass": 0.5, "target": "core"})
    api.debug("water_av_realtime", {"on": True})
    time.sleep(6)

def surface():
    ys = []
    for z in range(12, 16):
        for x in range(100, 104):
            for dx, dz in ((0.25, 0.25), (0.75, 0.75)):
                y = api.debug("water_av_probe", {"x": x + dx, "y": 16.0, "z": z + dz}).get("surface_y")
                if y is not None: ys.append(y)
    return max(abs(y - REST) for y in ys) if ys else None, len(ys)

def drop(tag, coupling):
    api.debug("water_coupling", {"enabled": coupling})
    make_pond()
    m0 = api.debug("water_av_list")["volumes"][0]["mass"]
    dev0, n = surface()
    camera_set(api, POSE); time.sleep(2)
    capture(api, EV / f"{tag}_before.png")
    c0 = api.debug("water_coupling", {})
    t0 = time.time()
    for i in range(10):
        x, z = 100.35 + (i % 2) * 0.9, 12.4 + (i // 2) * 0.7
        api.debug("spawn_gpu_particle", {"x": x, "y": 19.5, "z": z, "material": "Stone", "scale": 1.0 / 3.0, "lifetime": 20.0})
        api.debug("spawn_gpu_particle", {"x": x + 2.0, "y": 19.5, "z": z, "material": "Wood", "scale": 1.0 / 3.0, "lifetime": 20.0})
    frames = []
    for t in (0.6, 0.9, 1.2, 1.6, 2.5, 4.0, 8.0, 12.0):
        while time.time() - t0 < t: time.sleep(0.02)
        png = capture(api, EV / f"{tag}_t{t:05.2f}.png")
        dev, n = surface()
        frames.append({"t": t, "max_dev_m": round(dev, 4) if dev is not None else None, "columns": n, "png": str(png)})
        print(tag, json.dumps(frames[-1]), flush=True)
    m1 = api.debug("water_av_list")["volumes"][0]["mass"]
    c1 = api.debug("water_coupling", {})
    evs = api.debug("debris_events", {"recent": 512}).get("recent", [])
    last = {}
    for e in evs:   # each piece's LAST sleep inside the pond's columns (a floater may wake and re-sleep)
        x, y, z = e["pos"]
        if e["type"] == "sleep" and 99.5 <= x <= 104.5 and 11.5 <= z <= 16.5:
            last[e["slot"]] = e
    stone = [e["pos"][1] for e in last.values() if e["material"] == "Stone"]
    wood = [e["pos"][1] for e in last.values() if e["material"] == "Wood"]
    res = {"tag": tag, "coupling": coupling, "mass_before": m0, "mass_after": m1, "rest_dev_before": dev0,
           "max_dev_after_entry": max(f["max_dev_m"] or 0 for f in frames[:4]), "frames": frames,
           "exchange": c1.get("debris_exchange"), "exchange_before": c0.get("debris_exchange"), "volume_tiles": c1.get("debris_water_tiles"),
           "stone_sleep_y": sorted(round(y, 3) for y in stone), "wood_sleep_y": sorted(round(y, 3) for y in wood)}
    return res

out = {"pose": POSE, "backend": args.backend}
out["coupled"] = drop("e2_coupled", True)
time.sleep(10)   # the first drop's pieces expire (lifetime 20 s)
out["control"] = drop("e2_control", False)
api.debug("water_coupling", {"enabled": True})
(EV / "e2_demo.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
for k in ("coupled", "control"):
    r = out[k]
    print(k, json.dumps({kk: r[kk] for kk in ("max_dev_after_entry", "mass_before", "mass_after", "stone_sleep_y", "wood_sleep_y", "volume_tiles")}))
    print("  exchange", json.dumps(r["exchange"]))
