"""F8.2 live (docs/evidence/voxfound_f8_prediction.txt R1-R3): 100 breaks in quick succession on the booted switched
world (project VoxFoundBoot). Modes: control (the current code) / green (the fix). Blasts through POST
/api/damage/apply (DamageSystem -> the F7.5 world rig); the census through voxfound_preview {"census":true}.
Every artifact: WorldGenerator Perlin s7 + flora + those blasts."""
import json, os, random, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
B = "/api/debug/voxfound_bodies"
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


def census():
    return call("POST", P, {"census": True})["census"]


def diff(a, b):
    keys = set(a) | set(b)
    return {k: b.get(k, 0) - a.get(k, 0) for k in sorted(keys) if b.get(k, 0) != a.get(k, 0)}


def bake():
    time.sleep(5.0)
    call("POST", B, {"run": False})
    baked = (call("POST", B, {"bake_now": {"budget": 0, "keep_m": 0.0, "rest_ticks": 0, "awake": True}, "status": True,
                              "pen": False}).get("rest") or {}).get("baked")
    settle = call("POST", P, {"settle_rubble": True})   # F8.2: the rig's rubble as whole voxels (code since run 3)
    return {"baked": baked, "settle": settle}


def ground_points(rnd, rg, n):
    lo = [rg["from"][0] * 32 + 6, rg["from"][2] * 32 + 6]
    hi = [rg["to"][0] * 32 + 26, rg["to"][2] * 32 + 26]
    pts = []
    while len(pts) < n:
        x, z = rnd.randint(lo[0], hi[0]), rnd.randint(lo[1], hi[1])
        t = call("POST", P, {"column_top": [[x, z]]})["column_top"][0][1]
        if t <= 34:   # ground, not canopy
            pts.append((x + 0.5, t + 0.5, z + 0.5))
    return pts


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    rg = call("POST", P, {"stream_status": True})["region"]
    rnd = random.Random(82)
    c0 = census()
    (x, y, z), = ground_points(rnd, rg, 1)
    r = call("POST", "/api/damage/apply", {"x": x, "y": y, "z": z, "radius": 2.0, "energy": 400.0})
    out["R1_blast"] = {k: r.get(k) for k in ("broken", "debris")}
    out["R1_baked"] = bake()
    c1 = census()
    out["R1_census_diff"] = diff(c0, c1)
    pts = ground_points(rnd, rg, 100)
    walls, broken = [], 0
    for (x, y, z) in pts:
        t0 = time.time()
        r = call("POST", "/api/damage/apply", {"x": x, "y": y, "z": z, "radius": 1.5, "energy": 300.0})
        walls.append(round(time.time() - t0, 3))
        broken += r.get("broken", 0) or 0
        time.sleep(0.2)
    out["R2_blasts"] = {"count": len(pts), "voxels_broken_sum": broken}
    out["R2_baked"] = bake()
    c2 = census()
    out["R2_census_diff"] = diff(c1, c2)
    out["R2_lost_total"] = -sum(v for v in out["R2_census_diff"].values())
    walls.sort()
    out["R3"] = {"apply_median_s": walls[len(walls) // 2], "apply_max_s": walls[-1], "over_0p5": sum(w > 0.5 for w in walls)}
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f82_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
