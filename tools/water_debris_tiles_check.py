"""WaterCore.md 27.21 - the debris water tiles kept while the water under them stands: the cost, the exactness check, the red control.

The 27.20 limits pass found 64 sleeping 5 x 5 m ponds cutting the frame rate 601 -> 131 fps with the GPU idle; the CPU split
(tools/water_cost_split.py + the update scopes) put it in DebrisRuntime::beginFrame: every chunk tile under ANY volume box was rebuilt
every frame (1,024 columns x every field). Now a tile under volumes is kept while its signature (the fields' source + version)
stands. On WaterBench_Creek (port 8114, Release):
  COST    0 / 1 / 16 / 64 sleeping ponds (5 x 5 m, 0.3 m): the frame and the 'Debris Begin' scope (/api/debug/frame_pacing).
  CHECK   16 ponds, the check ON (every kept tile also rebuilt and compared byte for byte): wood floats dropped into 4 ponds, stones in
          4 others, 12 s; then 2 ponds destroyed and 1 created, 4 s. kept_mismatch must be 0 with kept_checked > 0.
  RED     the same with the signature blind to the fields' versions (TEST ONLY): moving water keeps stale tiles - kept_mismatch > 0.

    python tools/water_debris_tiles_check.py [--json out.json]
"""
import argparse, json, pathlib, statistics, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from water_bench import Api

EV = pathlib.Path(__file__).resolve().parent.parent / "docs" / "evidence" / "water_core_h1"
H, TOP = 1.0 / 9.0, 16
SLOTS = [(2 + i * 6, j * 6) for j in range(4) for i in range(20)]


def destroy_all(api):
    for v in api.debug("water_av_list").get("volumes", []): api.debug("water_av_destroy", {"id": v["id"]})


def pond(api, x, z):
    box = {"x1": x, "y1": TOP, "z1": z, "x2": x + 4, "y2": TOP, "z2": z + 4}
    vid = api.debug("water_av_create", {**box, "cellSize": H, "backend": "gpu", "auto_sleep": False})["volume"]["id"]
    api.debug("place_water_box", {"target": "core", **box, "mass": 0.3})
    return vid


def wait_asleep(api, limit=30):
    t0 = time.time()
    while time.time() - t0 < limit and any(not v.get("asleep") for v in api.debug("water_av_list")["volumes"]): time.sleep(0.5)


def scopes(api):
    d = api.get("/api/debug/frame_pacing?frames=120")
    return {s.get("path") or s.get("name"): s["median_ms"] for s in d.get("scopes", [])}


def cost(api):
    out = []
    for n in (0, 1, 16, 64):
        destroy_all(api)
        for (x, z) in SLOTS[:n]: pond(api, x, z)
        wait_asleep(api); time.sleep(3.0)
        sc = scopes(api); t = api.debug("water_av_debris_tiles", {})
        row = {"ponds": n, "frame_ms": sc.get("Frame"), "debris_begin_ms": sc.get("Debris Begin"), "update_ms": sc.get("Update"),
               "fps": api.get("/api/debug/engine_timing").get("fps"), "volume_tiles": t.get("volume_tiles"), "kept": t.get("kept")}
        out.append(row); print(" ", row, flush=True)
    return out


def scenario(api, red):
    destroy_all(api)
    ids = [pond(api, x, z) for (x, z) in SLOTS[:16]]
    wait_asleep(api); time.sleep(1.0)
    api.debug("water_av_debris_tiles", {"check": True, "reset": True, "red": red})
    time.sleep(2.0)
    for k in range(4):                 # wood floats into ponds 0-3
        x, z = SLOTS[k]
        for j in range(3): api.debug("spawn_gpu_particle", {"x": x + 1.5 + j, "y": TOP + 1.2, "z": z + 2.5, "material": "Wood", "scale": 1.0 / 3.0, "lifetime": 600.0})
    for k in range(4, 8):              # stones into ponds 4-7
        x, z = SLOTS[k]
        api.debug("water_av_impulse", {"x": x + 2.5, "y": TOP + 0.2, "z": z + 2.5, "radius": 1 / 3, "strength": 4.0, "dx": 0.0, "dy": -1.0, "dz": 0.0})
    time.sleep(12.0)
    api.debug("water_av_destroy", {"id": ids[8]}); api.debug("water_av_destroy", {"id": ids[9]})
    pond(api, *SLOTS[20])
    time.sleep(4.0)
    t = api.debug("water_av_debris_tiles", {"check": False, "red": False})
    row = {"red": red, "kept_checked": t.get("kept_checked"), "kept_mismatch": t.get("kept_mismatch"), "volume_tiles": t.get("volume_tiles")}
    print(" ", row, flush=True)
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8114")
    ap.add_argument("--json", default=str(EV / "debris_tiles_check.json"))
    a = ap.parse_args()
    api = Api(a.url); api.wait_status(300)
    api.debug("water_av_realtime", {"on": True})
    api.post("/api/camera", {"mode": "free", "position": {"x": 62.0, "y": 70.0, "z": 60.0}, "yaw": -90.0, "pitch": -55.0})
    time.sleep(2.0)
    res = {"cost": cost(api), "check": scenario(api, False), "red": scenario(api, True)}
    destroy_all(api)
    res["pass"] = res["check"]["kept_mismatch"] == 0 and res["check"]["kept_checked"] > 0 and res["red"]["kept_mismatch"] > 0
    print("PASS" if res["pass"] else "FAIL", flush=True)
    pathlib.Path(a.json).write_text(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
