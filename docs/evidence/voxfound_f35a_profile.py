"""F3.5a diagnosis -- where a border edit's extra cost goes (VoxelFoundationPlan F3.5 owed item). Gate region
(engine-generated: /api/debug/voxfound_preview Perlin s7 flora, chunks 254..257 x 0..1 x 2..5, {"editable":true});
today's world hidden. Size-8 brushes, interleaved: interior (>= 16 voxels from every chunk border), on a chunk face,
an edge, a corner. No verify (it runs after the timed part anyway). Every phase field of the edit response is kept.
usage: voxfound_f35a_profile.py out_prefix [n_per_kind] [extra request json]"""
import json, os, random, sys, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REGION = {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "editable": True,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
LO = (254 * 256, 0, 2 * 256)
HI = (258 * 256 - 1, 2 * 256 - 1, 6 * 256 - 1)
FIELDS = ["rebuild_ms", "assemble_ms", "set_ms", "collapse_ms", "plan_ms", "rows_wall_ms", "faces_ms", "cluster_ms",
          "sprig_ms", "connect_ms", "voxels_changed", "faces_removed", "faces_added", "rows_meshed", "assemble_bytes",
          "connect_floods"]


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=900) as resp:
        return json.loads(resp.read().decode())


out_prefix = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 30
extra = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
rng = random.Random(3601)
call("POST", P, REGION)
call("POST", P, {"hide_world": True})
call("POST", P, {"show_preview": True, "face_pool": True})


def point(k):
    p = [rng.randint(LO[a] // 256 * 256, HI[a]) for a in range(3)]
    p[1] = rng.randint(30 * 8, 50 * 8)
    for a in range(3):   # interior of its chunk first
        base = p[a] // 256 * 256
        p[a] = base + rng.randint(16, 239) if a != 1 else min(max(p[a], base + 16), base + 239)
    for a in rng.sample([0, 1, 2], k):
        if a == 1:
            p[1] = 256 + rng.randint(-8, 7)
        else:
            p[a] = rng.randint(LO[a] // 256 + 1, HI[a] // 256) * 256 + rng.randint(-8, 7)
    return p


rows = []
for i in range(N * 4):
    k = i % 4   # 0 interior, 1 face, 2 edge, 3 corner
    at = point(k)
    # snap like brushCell (size 8 = one block); a border brush straddles only if aligned off the border: force it
    place = rng.random() < 0.5
    res = call("POST", P, {"edit": dict({"material": rng.choice(["Stone", "Dirt"]) if place else "air",
                                         "brush": {"at": at, "size": 8}}, **extra)})
    row = {"kind": ["interior", "face", "edge", "corner"][k], "chunks": len(res.get("chunks_rebuilt", [])),
           "error": res.get("error")}
    for f in FIELDS:
        row[f] = res.get(f)
    rows.append(row)
with open(out_prefix + ".jsonl", "w") as f:
    for r in rows:
        f.write(json.dumps(r) + "\n")


def med(v):
    v = sorted(x for x in v if x is not None)
    return round(v[len(v) // 2], 3) if v else None


summ = {}
for c in sorted({r["chunks"] for r in rows}):
    rs = [r for r in rows if r["chunks"] == c and r["voxels_changed"]]
    summ[c] = {"n": len(rs)}
    for f in FIELDS:
        summ[c][f] = med([r[f] for r in rs])
    summ[c]["other_rebuild_ms"] = med([r["rebuild_ms"] - r["set_ms"] - r["collapse_ms"] - r["faces_ms"] - r["cluster_ms"]
                                       - r["sprig_ms"] - r["connect_ms"] for r in rs])
json.dump(summ, open(out_prefix + ".json", "w"), indent=1)
print(json.dumps(summ, indent=1))
