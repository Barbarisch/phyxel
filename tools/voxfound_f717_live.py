"""F7.17 live (docs/evidence/voxfound_f7_prediction.txt K1-K3): the water sim's solids on the switched world.
Project VoxFoundWater (a copy of WaterBench_Coast + the F7.16 boot). Modes: control / red (water_sync_stub) / green.
The pit: cleared through the engine's voxel API (clear_region, routed to the switched world); water through
/api/debug/place_water. Every artifact: WorldGenerator Perlin s20260804 + flora (the region) + the CA water sim."""
import json, os, random, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def probe(x, y, z):
    return call("POST", "/api/debug/water_probe", {"x": x, "y": y, "z": z})


def region_cube(x, y, z):
    return (call("GET", f"/api/world/voxel?x={x}&y={y}&z={z}").get("voxfound") or {}).get("solid_voxels")


def wait_async(r):
    aid = r.get("async_id")
    for _ in range(600):
        if not aid:
            return r
        s = call("GET", f"/api/async/{aid}")
        if s.get("status") == "complete" or "error" in s:
            return s
        time.sleep(0.1)


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(120):
        b = call("POST", P, {"boot_status": True})
        if b.get("report"):
            break
        time.sleep(2)
    out["boot"] = (b.get("report") or {}).get("booted")
    if mode == "red":   # the stub set after the boot's own resync: the edits' resyncs are what the red removes
        out["stub"] = call("POST", P, {"water_sync_stub": True})
    st = call("POST", "/api/debug/water_stats", {})
    o, d = st["origin"], st["dims"]
    rg = call("POST", P, {"stream_status": True})["region"]
    lo = [max(o["x"], rg["from"][0] * 32), max(o["z"], rg["from"][2] * 32)]
    hi = [min(o["x"] + d["x"] - 1, rg["to"][0] * 32 + 31), min(o["z"] + d["z"] - 1, rg["to"][2] * 32 + 31)]
    out["sim"] = {"origin": o, "dims": d, "overlap_xz": [lo, hi]}
    rnd = random.Random(17)
    k1 = {"checked": 0, "mismatch": 0, "samples": []}
    for _ in range(1500):
        x, z = rnd.randint(lo[0], hi[0]), rnd.randint(lo[1], hi[1])
        y = rnd.randint(o["y"], o["y"] + d["y"] - 1)
        n = region_cube(x, y, z)
        if n not in (0, 512):
            continue
        s = probe(x, y, z).get("solid")
        k1["checked"] += 1
        if s != (n == 512):
            k1["mismatch"] += 1
            if len(k1["samples"]) < 5:
                k1["samples"].append([x, y, z, n, s])
        if k1["checked"] >= 600:
            break
    out["K1"] = k1
    pit = None   # a land column (top 18..30) whose 5 x 5 neighbourhood is at least as high
    for _ in range(400):
        x, z = rnd.randint(lo[0] + 3, hi[0] - 6), rnd.randint(lo[1] + 3, hi[1] - 6)
        t = call("POST", P, {"column_top": [[x + 1, z + 1]]})["column_top"][0][1]
        if 18 <= t <= 30:
            ring = [call("POST", P, {"column_top": [[x + dx, z + dz]]})["column_top"][0][1]
                    for dx in (-1, 3) for dz in (-1, 3)]
            if min(ring) >= t:
                pit = (x, z, t)
                break
    out["pit"] = pit
    if pit:
        x, z, t = pit
        out["clear"] = wait_async(call("POST", "/api/world/clear", {"x1": x, "y1": t - 2, "z1": z, "x2": x + 2, "y2": t,
                                                                   "z2": z + 2})).get("removed")
        cells = [(x + i, t - 2 + j, z + k) for i in range(3) for j in range(3) for k in range(3)]
        out["K2_open"] = [sum(1 for c in cells if probe(*c).get("solid") is False), len(cells)]
        out["place"] = call("POST", "/api/debug/place_water", {"x": x + 1.5, "y": t + 0.5, "z": z + 1.5, "amount": 9.0})
        time.sleep(5.0)
        masses = [probe(*c).get("mass", 0.0) or 0.0 for c in cells]
        bottom = [probe(x + i, t - 2, z + k).get("mass", 0.0) or 0.0 for i in range(3) for k in range(3)]
        out["K3"] = {"pit_mass": round(sum(masses), 3), "bottom_mass": round(sum(bottom), 3), "placed": 9.0}
    if mode == "red":
        call("POST", P, {"water_sync_stub": False})
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f717_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
