"""F3.5 -- the F3 exit gate (VoxelFoundationPlan §9b). Gate region (engine-generated: /api/debug/voxfound_preview
Perlin s7 flora, chunks 254..257 x 0..1 x 2..5, {"editable":true}); today's world hidden, wind 0, GI off.
1,000 verified brush edits (sizes 1/2/4/8, half aimed by picks from the 5 poses, half on chunk borders / edges /
corners) in 10 batches; after each batch save -> rebuild with load_saved -> every chunk hash equal; editing continues
on the reloaded region. At the end: pixels vs replace_all at two poses, a byte-identical re-save. Stops at the first
mismatch.
Second run (F3.5a): brush size and kind are drawn independently (the first run chose them by edit % 4 and edit % 2,
which tied sizes 1 / 4 to picks and 2 / 8 to borders); outputs carry the tag.
usage: voxfound_f35_gate.py poses.json [tag]"""
import json, math, os, random, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "..", "..", "phyxel.log")
LOG0 = os.path.getsize(LOG) if os.path.exists(LOG) else 0
VIEW = (245, 47, 1195, 670)
P = "/api/debug/voxfound_preview"
REGION = {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "editable": True,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
LO = (254 * 256, 0, 2 * 256)
HI = (258 * 256 - 1, 2 * 256 - 1, 6 * 256 - 1)


def device_lost():
    with open(LOG, "rb") as f:
        f.seek(LOG0)
        return b"VkResult=-4" in f.read()


def call(m, p, b=None):
    if device_lost():
        raise SystemExit("DEVICE LOST (phyxel.log) before " + p)
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=900) as resp:
        return json.loads(resp.read().decode())


def shot(name, settle=4.0):
    time.sleep(settle)
    d = os.path.join(HERE, name)
    shutil.copy(call("GET", "/api/screenshot")["path"], d)
    return np.asarray(Image.open(d).convert("RGB")).astype(np.int16)


def px(a, b):
    x0, y0, x1, y1 = VIEW
    return int((np.max(np.abs(a[y0:y1, x0:x1] - b[y0:y1, x0:x1]), axis=2) > 8).sum())


def build(load):
    t = time.time()
    r = call("POST", P, dict(REGION, load_saved=load))
    call("POST", P, {"hide_world": True})
    call("POST", P, {"show_preview": True, "face_pool": True})
    return r, round(time.time() - t, 1)


def hashes():
    return {tuple(c["chunk"]): c["hash"] for c in call("POST", P, {"store_hash": True})["chunks"]}


def fwd(yaw, pitch):
    return [math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(pitch)),
            math.sin(math.radians(yaw)) * math.cos(math.radians(pitch))]


poses = json.load(open(sys.argv[1]))
TAG = sys.argv[2] if len(sys.argv) > 2 else ""
names = list(poses.keys())
rng = random.Random(3501 if not TAG else 3521)
out = {"batches": []}
build(False)
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
out["cleared"] = call("POST", P, {"saved_clear": True})["cleared"]
r0, _ = build(False)
out["store_bytes_build"] = r0.get("edit_store_bytes")
MATS = ["Stone", "Wood", "Leaf", "Dirt"]
log = []
stop = None


def border_point(size):
    p = [rng.randint(LO[a] + 16, HI[a] - 16) for a in range(3)]
    p[1] = rng.randint(30 * 8, 50 * 8)   # around the ground / canopy (the y = 256 chunk border is 32 m)
    k = rng.randint(1, 3)
    for a in rng.sample([0, 1, 2], k):
        if a == 1:
            p[1] = 256 + rng.randint(-size, size)
        else:
            m = rng.randint(LO[a] // 256 + 1, HI[a] // 256) * 256
            p[a] = m + rng.randint(-size, size)
    return p, k


for batch in range(10):
    edits = 0
    while edits < 100:
        size = rng.choice((1, 2, 4, 8)) if TAG else (1, 2, 4, 8)[edits % 4]
        place = rng.random() < 0.5
        mat = rng.choice(MATS) if place else "air"
        if (rng.random() < 0.5) if TAG else edits % 2 == 0:
            x, y, z, yaw, pitch = poses[names[rng.randrange(len(names))]]
            pk = call("POST", P, {"pick": {"origin": [x, y, z], "dir": fwd(yaw + rng.uniform(-25, 25), pitch + rng.uniform(-15, 10)),
                                           "max_m": 96, "size": size}})
            if not pk.get("hit"):
                continue
            at, kind = (pk["place_cell"] if place else pk["break_cell"]), "picked"
        else:
            at, k = border_point(size)
            kind = f"border{k}"
        res = call("POST", P, {"edit": {"material": mat, "brush": {"at": at, "size": size}, "verify": True}})
        v = res.get("verify") or {}
        mism = v.get("face_mismatches", 0) + v.get("sprig_mismatches", 0) + v.get("connectivity_mismatches", 0)
        row = {"batch": batch, "size": size, "kind": kind, "place": place, "voxels_changed": res.get("voxels_changed"),
               "chunks": len(res.get("chunks_rebuilt", [])), "chunks_changed": res.get("chunks_changed"),
               "assemble_ms": res.get("assemble_ms"), "plan_ms": res.get("plan_ms"), "connect_ms": res.get("connect_ms"),
               "mismatches": mism, "error": res.get("error"),
               "ready_ms": (res.get("rebuild_ms") or 0) + (res.get("assemble_ms") or 0),
               "edit_store_bytes": res.get("edit_store_bytes")}
        log.append(row)
        edits += 1
        if mism or res.get("error") or not v:
            stop = {"at": row, "verify": v}
            break
    if stop:
        break
    t = time.time()
    s = call("POST", P, {"save": True})
    save_s = round(time.time() - t, 2)
    last_save = {tuple(c["chunk"]): c["blob_hash"] for c in s.get("chunks", [])}
    H = hashes()
    r, wall = build(True)
    H2 = hashes()
    equal = sum(1 for c in H if H2[c] == H[c])
    b = {"batch": batch, "saved_chunks": s.get("saved_chunks"), "blob_bytes": s.get("bytes"), "save_s": save_s,
         "reload_wall_s": wall, "loaded": (r.get("load_saved") or {}).get("loaded_chunks"),
         "load_rebuild_ms": (r.get("load_saved") or {}).get("load_rebuild_ms"), "hashes_equal": equal,
         "chunks": len(H), "store_bytes_after_reload": r.get("edit_store_bytes"),
         "store_bytes_before_save": log[-1]["edit_store_bytes"]}
    out["batches"].append(b)
    print("batch", json.dumps(b), flush=True)
    if equal != len(H):
        stop = {"batch": batch, "unequal": [list(c) for c in H if H2[c] != H[c]]}
        break
with open(os.path.join(HERE, f"voxfound_f35_gate{TAG}_edits.jsonl"), "w") as f:
    for row in log:
        f.write(json.dumps(row) + "\n")
out["stop"] = stop
out["edits"] = len(log)
out["mismatches"] = sum(r["mismatches"] for r in log)


def q(v, p):
    v = sorted(v)
    return round(v[min(len(v) - 1, int(p * len(v)))], 3) if v else None


out["cost"] = {}
for size in (1, 2, 4, 8):
    rows = [r for r in log if r["size"] == size and r["voxels_changed"]]
    first = [r["ready_ms"] for r in rows if r["batch"] < 3]
    last = [r["ready_ms"] for r in rows if r["batch"] >= 7]
    out["cost"][f"s{size}"] = {"n": len(rows), "median": q([r["ready_ms"] for r in rows], 0.5),
                               "p90": q([r["ready_ms"] for r in rows], 0.9), "max": q([r["ready_ms"] for r in rows], 1.0),
                               "median_batches_0_2": q(first, 0.5), "median_batches_7_9": q(last, 0.5)}
out["kinds"] = {k: sum(1 for r in log if r["kind"] == k) for k in sorted({r["kind"] for r in log})}
out["cost_by_size_kind"] = {}
for size in (1, 2, 4, 8):
    for k in sorted({r["kind"] for r in log}):
        rows = [r["ready_ms"] for r in log if r["size"] == size and r["kind"] == k and r["voxels_changed"]]
        out["cost_by_size_kind"][f"s{size}_{k}"] = {"n": len(rows), "median": q(rows, 0.5), "p90": q(rows, 0.9)}
print("cost", json.dumps(out["cost"]), "kinds", json.dumps(out["kinds"]), "mismatches", out["mismatches"], "stop", json.dumps(stop),
      flush=True)
if not stop:
    pix = {}
    for name in ("forest_floor_open", "overview_south"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        L = shot(f"f35{TAG}_{name[:3]}_L.png", 6.0)
        L2 = shot(f"f35{TAG}_{name[:3]}_L2.png", 2.0)
        pix[name] = (L, L2)
    call("POST", P, {"edit": {"replace_all": True}})
    out["pixels"] = {}
    for name in pix:
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        F = shot(f"f35{TAG}_{name[:3]}_F.png", 6.0)
        out["pixels"][name] = {"L_vs_F": px(pix[name][0], F), "L_vs_L2": px(pix[name][0], pix[name][1])}
    s1 = call("POST", P, {"save": True})   # the reloaded region saved again = the save it was loaded from
    h1 = {tuple(c["chunk"]): c["blob_hash"] for c in s1.get("chunks", [])}
    out["resave"] = {"chunks": len(h1), "identical_to_last_batch_save": sum(1 for c in h1 if last_save.get(c) == h1[c]),
                     "last_batch_saved": len(last_save)}
    print("pixels", json.dumps(out["pixels"]), "resave", json.dumps(out["resave"]), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.05})
call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, f"voxfound_f35_gate{TAG}.json"), "w"), indent=1)
print("end", flush=True)
