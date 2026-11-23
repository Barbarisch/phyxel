"""F8.1 live (docs/evidence/voxfound_f8_prediction.txt S1-S3): 1,000 edit / break cycles across chunk borders on the
booted switched world (project VoxFoundBoot). Modes: run (the 1,000 + checks + undo + save_world) / reload (after a
relaunch: the boot's hashes vs the run's final hashes). Edits through the preview edit API (the routed brush path),
blasts through POST /api/damage/apply. Every artifact: WorldGenerator Perlin s7 + flora + those edits."""
import json, os, random, statistics, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
B = "/api/debug/voxfound_bodies"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
STATE = os.path.join(EVID, "voxfound_f81_state.json")
MATS = ["Stone", "Dirt", "Bricks", "Glass"]


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def hashes():
    return {",".join(map(str, c["chunk"])): c["hash_by_name"] for c in call("POST", P, {"store_hash": True})["chunks"]}


def cube_solid(v):
    q = call("GET", f"/api/world/voxel?x={v[0] >> 3}&y={v[1] >> 3}&z={v[2] >> 3}")
    return (q.get("voxfound") or {}).get("solid_voxels", 0)


def count_ok(before, after, changed, air):
    d = after - before
    return d == -changed if air else 0 <= d <= changed


def column_top(vx, vz):
    return call("POST", P, {"column_top": [[vx // 8, vz // 8]]})["column_top"][0][1]


def wait_boot():
    for _ in range(150):
        b = call("POST", P, {"boot_status": True})
        if b.get("report"):
            return b["report"]
        time.sleep(2)
    return {}


def run(out, rg):
    out["checker_red"] = {"flags_off_by_one": not count_ok(512, 0, 511, True)}   # the checker can fail
    rnd = random.Random(81)
    vlo = [rg["from"][0] * 256, rg["from"][2] * 256]
    vhi = [rg["to"][0] * 256 + 255, rg["to"][2] * 256 + 255]
    bad = {"count": 0, "verify": 0, "reads": 0, "physics": 0, "blast_window": 0}
    ms, wall, recorded, tail = [], [], [], {}
    straddle = 0
    for i in range(1000):
        if i >= 970:
            tail[i] = hashes()
        if rnd.random() < 0.4:   # straddle a chunk border in x or z
            axis = rnd.randint(0, 1)
            edge = rnd.randint(1, (vhi[axis] - vlo[axis]) // 256) * 256 + vlo[axis] + rnd.randint(-8, 7)
            vx = edge if axis == 0 else rnd.randint(vlo[0] + 16, vhi[0] - 16)
            vz = edge if axis == 1 else rnd.randint(vlo[1] + 16, vhi[1] - 16)
            straddle += 1
        else:
            vx, vz = rnd.randint(vlo[0] + 16, vhi[0] - 16), rnd.randint(vlo[1] + 16, vhi[1] - 16)
        vy = column_top(vx, vz) * 8 + rnd.randint(-8, 8)
        size = rnd.choice([1, 2, 4, 8])
        air = rnd.random() < 0.5
        at = [vx, vy, vz]
        cell = [a - (a % size) for a in at]
        before = cube_solid(cell)
        t0 = time.time()
        r = call("POST", P, {"edit": {"brush": {"at": at, "size": size}, "material": "air" if air else rnd.choice(MATS),
                                      "verify": i % 25 == 0}})
        wall.append((time.time() - t0) * 1000)
        ms.append((r.get("rebuild_ms") or 0) + (r.get("assemble_ms") or 0))
        if not count_ok(before, cube_solid(cell), r.get("voxels_changed", -1), air):
            bad["count"] += 1
        if r.get("undo") == "recorded":
            recorded.append(i)
        v = r.get("verify")
        if v:
            bad["verify"] += v.get("face_mismatches", 0) + v.get("sprig_mismatches", 0) + v.get("connectivity_mismatches", 0)
        if i % 100 == 99:
            rc = call("POST", P, {"reads_check": {"n": 512}}).get("reads_check", {})
            bad["reads"] += rc.get("checked", 0) - rc.get("equal_to_region_rule", 0)
            pts = []
            for _ in range(400):
                px, pz = rnd.randint(vlo[0], vhi[0]), rnd.randint(vlo[1], vhi[1])
                py = column_top(px, pz) * 8 + rnd.randint(-12, 12)
                pts.append([(px + 0.5) / 8, (py + 0.5) / 8, (pz + 0.5) / 8])
            pp = call("POST", P, {"phys_probe": pts})
            bad["physics"] += pp.get("probes", 0) - pp.get("agree", 0)
        if i % 100 == 50 and i < 950:   # a blast, its pieces baked back, the window vs the lab grid
            bx, bz = rnd.randint(vlo[0] + 64, vhi[0] - 64), rnd.randint(vlo[1] + 64, vhi[1] - 64)
            t = column_top(bx, bz)
            call("POST", "/api/damage/apply", {"x": bx / 8, "y": t + 0.5, "z": bz / 8, "radius": 1.5, "energy": 300.0})
            time.sleep(3.0)
            call("POST", B, {"run": False})
            w = call("POST", B, {"bake_now": {"budget": 0, "keep_m": 0.0, "rest_ticks": 0}, "status": True,
                                 "pen": False, "world_check": True}).get("world") or {}
            bad["blast_window"] += w.get("check_mismatched_voxels", 0) or 0
        if i % 200 == 199:
            print(f"[{i + 1}] {bad}", flush=True)
    out["S1"] = {"edits": 1000, "straddling": straddle, "mismatches": bad, "recorded": len(recorded)}
    s = sorted(ms)
    out["S3"] = {"median_ms": round(statistics.median(ms), 3), "p99_ms": round(s[int(0.99 * len(s)) - 1], 3),
                 "max_ms": round(s[-1], 3), "http_median_ms": round(statistics.median(wall), 2)}
    target = recorded[-20] if len(recorded) >= 20 else None
    for _ in range(20):
        call("POST", "/api/undo", {})
    h = hashes()
    if target is not None and target in tail:
        out["S2_undo20_equal"] = [sum(h[k] == v for k, v in tail[target].items()), len(h)]
    else:
        out["S2_undo20_equal"] = "the 20th-last recorded edit (%s) is before the hashed tail" % target
    out["save"] = (call("POST", "/api/world/save", {}).get("voxfound") or {}).get("saved_chunks")
    json.dump({"final": h}, open(STATE, "w"))


def main():
    mode = sys.argv[1]
    out = {"mode": mode, "boot": wait_boot().get("booted")}
    rg = call("POST", P, {"stream_status": True})["region"]
    if mode == "reload":
        prev = json.load(open(STATE))["final"]
        h = hashes()
        out["S2_reload_equal"] = [sum(h.get(k) == v for k, v in prev.items()), len(prev)]
    else:
        run(out, rg)
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f81_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
