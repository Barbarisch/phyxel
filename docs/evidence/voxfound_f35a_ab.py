"""F3.5a A/B -- F3.5's border handling ({"legacy_border":true}) vs F3.5a's, the same seeded edits, ABBA order, one
engine session. Gate region (engine-generated: /api/debug/voxfound_preview Perlin s7 flora, chunks 254..257 x 0..1 x
2..5, {"editable":true}), rebuilt before each run so every run starts from the generator's state; today's world hidden.
N size-8 brushes per run, interleaved interior / chunk face / edge / corner, place (Stone / Dirt) or break; every edit
verified (a full FaceBuilderGpu rebuild of every re-planned chunk). Stops at the first mismatch.
usage: voxfound_f35a_ab.py out_prefix [n] [order e.g. ABBA] [A switch, default legacy_border; F3.5b: legacy_connect]"""
import json, random, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REGION = {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "editable": True,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
LO = (254 * 256, 0, 2 * 256)
HI = (258 * 256 - 1, 2 * 256 - 1, 6 * 256 - 1)
KINDS = ["interior", "face", "edge", "corner"]


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=900) as resp:
        return json.loads(resp.read().decode())


out_prefix = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 120
ORDER = sys.argv[3] if len(sys.argv) > 3 else "ABBA"
ASWITCH = sys.argv[4] if len(sys.argv) > 4 else "legacy_border"
rng = random.Random(3511)


def point(k):
    p = []
    for a in range(3):
        c = rng.randint(LO[a] // 256, HI[a] // 256)
        p.append(c * 256 + rng.randint(16, 239))
    p[1] = rng.randint(30 * 8, 50 * 8)
    if 256 - 16 <= p[1] <= 256 + 15:
        p[1] = 256 - 20
    for a in rng.sample([0, 1, 2], k):
        if a == 1:
            p[1] = 256 + rng.randint(-8, 7)
        else:
            p[a] = rng.randint(LO[a] // 256 + 1, HI[a] // 256) * 256 + rng.randint(-8, 7)
    return p


EDITS = []
for i in range(N):
    k = i % 4
    place = rng.random() < 0.5
    EDITS.append({"kind": KINDS[k], "at": point(k), "material": rng.choice(["Stone", "Dirt"]) if place else "air"})

FIELDS = ["rebuild_ms", "assemble_ms", "plan_ms", "rows_wall_ms", "cluster_ms", "sprig_ms", "connect_ms",
          "assemble_bytes", "voxels_changed", "chunks_changed", "connect_floods", "connect_bytes"]
runs = []
stop = None
for r, mode in enumerate(ORDER):
    t = time.time()
    call("POST", P, REGION)
    call("POST", P, {"hide_world": True})
    call("POST", P, {"show_preview": True, "face_pool": True})
    rows = []
    for i, e in enumerate(EDITS):
        res = call("POST", P, {"edit": {"material": e["material"], "brush": {"at": e["at"], "size": 8}, "verify": True,
                                        ASWITCH: mode == "A"}})
        v = res.get("verify") or {}
        mism = v.get("face_mismatches", 0) + v.get("sprig_mismatches", 0) + v.get("connectivity_mismatches", 0)
        row = {"run": r, "mode": mode, "i": i, "kind": e["kind"], "chunks": len(res.get("chunks_rebuilt", [])),
               "mismatches": mism, "error": res.get("error"), "verified": bool(v)}
        for f in FIELDS:
            row[f] = res.get(f)
        row["ready_ms"] = (res.get("rebuild_ms") or 0) + (res.get("assemble_ms") or 0)
        rows.append(row)
        if mism or res.get("error") or (res.get("voxels_changed") and not v):
            stop = {"run": r, "edit": i, "row": row, "verify": v}
            break
    runs.append(rows)
    print("run", r, mode, "edits", len(rows), "mismatches", sum(x["mismatches"] for x in rows), "wall_s",
          round(time.time() - t, 1), flush=True)
    if stop:
        break
with open(out_prefix + ".jsonl", "w") as f:
    for rows in runs:
        for row in rows:
            f.write(json.dumps(row) + "\n")


def med(v):
    v = sorted(x for x in v if x is not None)
    return round(v[len(v) // 2], 3) if v else None


summ = {"stop": stop, "mismatches": sum(x["mismatches"] for rows in runs for x in rows),
        "edits_verified": sum(1 for rows in runs for x in rows if x["verified"]), "by": {}}
for mode in "AB":
    for k in KINDS:
        rs = [x for rows in runs for x in rows if x["mode"] == mode and x["kind"] == k and x["voxels_changed"]]
        d = {"n": len(rs), "chunks_replanned": med([x["chunks"] for x in rs]), "ready_ms": med([x["ready_ms"] for x in rs]),
             "ready_p90": round(sorted(x["ready_ms"] for x in rs)[int(0.9 * len(rs))], 3) if rs else None}
        for f in FIELDS:
            d[f] = med([x[f] for x in rs])
        summ["by"][f"{mode}_{k}"] = d
json.dump(summ, open(out_prefix + ".json", "w"), indent=1)
print(json.dumps(summ["by"], indent=0), "\nmismatches", summ["mismatches"], "verified", summ["edits_verified"], "stop",
      json.dumps(stop), flush=True)
