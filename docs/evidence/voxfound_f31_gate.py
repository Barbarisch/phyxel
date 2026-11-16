"""F3.1 gate (VoxelFoundationPlan §9b): edit cost on the gate region (engine-generated: /api/debug/voxfound_preview
Perlin s7 flora, chunks 254..257 x 0..1 x 2..5, {"editable":true}; LOD off; face pool on). Today's world hidden, wind 0,
GI probes off (as F2.16 / F2.17).
1. Cost A/B in one session: F2.16's edit generator (seed 216, 50 edits each of 1 voxel, 1 m, 2 m, 4 m; boxes and
   spheres; a quarter on chunk borders / corners), every edit VERIFIED against a full FaceBuilderGpu rebuild of each
   touched chunk. Edits alternate F3.1 / legacy ({"legacy":true} = F2.17's path) in ABBA order, so both arms see the
   same mix. Per edit: rows / connectivity / assemble ms, floods, bytes written into the Preview, next-frame upload.
   "Request to lists ready" = rebuild_ms + assemble_ms.
2. Pixels at forest_floor_open and overview_south: 20 visible edits (F3.1 path) = I, I2 (control), replace_all (a full
   rebuild + fresh layout of the same voxels) = F. 3D viewport only.
3. Churn: 200 more edits (F3.1), then I3 vs a fresh layout F3 at overview_south.
usage: voxfound_f31_gate.py poses.json"""
import json, math, os, random, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "..", "..", "phyxel.log")
LOG0 = os.path.getsize(LOG) if os.path.exists(LOG) else 0
VIEW = (245, 47, 1195, 670)   # the 3D viewport inside the editor's 1600 x 900 frame (x0, y0, x1, y1), as F2.17
P = "/api/debug/voxfound_preview"


def device_lost():
    with open(LOG, "rb") as f:
        f.seek(LOG0)
        return b"VkResult=-4" in f.read()


def call(m, p, b=None):
    if device_lost():
        raise SystemExit("DEVICE LOST (phyxel.log) before " + p)
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=600) as resp:
        return json.loads(resp.read().decode())


def shot(name, settle=3.0):
    time.sleep(settle)
    d = os.path.join(HERE, name)
    shutil.copy(call("GET", "/api/screenshot")["path"], d)
    return np.asarray(Image.open(d).convert("RGB")).astype(np.int16)


def px(a, b):
    x0, y0, x1, y1 = VIEW
    return int((np.max(np.abs(a[y0:y1, x0:x1] - b[y0:y1, x0:x1]), axis=2) > 8).sum())


def stats():
    s = call("POST", P, {"stats": True})
    return {"pool": s.get("face_pool"), "upload_ms": s.get("upload_ms_last"),
            "cluster_faces_drawn": s.get("cluster_faces_drawn"), "clusters_drawn": s.get("clusters_drawn")}


poses = json.load(open(sys.argv[1]))
out = {}
t = time.time()
r = call("POST", P, {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "editable": True,
                     "region": {"from": [254, 0, 2], "to": [257, 1, 5]}})
out["build"] = {k: r.get(k) for k in ("faces", "sprigs", "edit_store_bytes", "error")}
out["build"]["wall_s"] = round(time.time() - t, 1)
print("build", json.dumps(out["build"]), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
call("POST", P, {"hide_world": True})
call("POST", P, {"show_preview": True, "face_pool": True})

LO = (254 * 256, 0, 2 * 256)
HI = (258 * 256 - 1, 2 * 256 - 1, 6 * 256 - 1)
MATS = ["air", "Stone", "air", "Dirt", "Leaf", "air", "Wood"]
SIZES = [("voxel", 1), ("1m", 8), ("2m", 16), ("4m", 32)]
rng = random.Random(216)


def random_edit(sz):   # F2.16's generator
    c = [rng.randint(LO[0], HI[0]), rng.randint(10 * 8, 45 * 8), rng.randint(LO[2], HI[2])]
    if rng.random() < 0.25:
        for a in rng.sample([0, 1, 2], rng.randint(1, 3)):
            k = round(c[a] / 256) * 256
            c[a] = min(max(k, LO[a] + sz), HI[a] - sz) if a != 1 else 256
    mat = rng.choice(MATS)
    if rng.random() < 0.6 or sz == 1:
        return {"material": mat, "lo": [c[0] - sz // 2, c[1] - sz // 2, c[2] - sz // 2],
                "hi": [c[0] - sz // 2 + sz - 1, c[1] - sz // 2 + sz - 1, c[2] - sz // 2 + sz - 1]}
    return {"material": mat, "sphere": {"c": c, "r": sz / 2}}


KEYS = ("voxels_changed", "faces_removed", "faces_added", "rows_meshed", "plan_ms", "rows_wall_ms", "cluster_ms",
        "sprig_ms", "connect_ms", "connect_floods", "set_ms", "rebuild_ms", "assemble_ms", "assemble_bytes", "legacy")
log = []
first = call("POST", P, {"edit": {"material": "Stone", "lo": [65607, 196, 885], "hi": [65614, 203, 892]}})
print("first (pool layout)", first.get("assemble_ms"), flush=True)
for cls, sz in SIZES:
    for k in range(50):
        legacy = (k % 4) in (1, 2)   # ABBA
        e = random_edit(sz)
        e["verify"] = True
        e["legacy"] = legacy
        res = call("POST", P, {"edit": e})
        time.sleep(0.3)
        st = stats()
        row = {"mode": "legacy" if legacy else "f31", "class": cls, "edit": e,
               "chunks": len(res.get("chunks_rebuilt", [])), "verify": res.get("verify"), "error": res.get("error"),
               "upload_ms": st["upload_ms"], "pool_kind": (st["pool"] or {}).get("last_kind")}
        row.update({kk: res.get(kk) for kk in KEYS})
        log.append(row)
    print(cls, "done", flush=True)
with open(os.path.join(HERE, "voxfound_f31_gate_edits.jsonl"), "w") as f:
    for row in log:
        f.write(json.dumps(row) + "\n")


def q(v, p):
    v = sorted(v)
    return round(v[min(len(v) - 1, int(p * len(v)))], 3) if v else None


summary = {}
for mode in ("f31", "legacy"):
    for cls, _ in SIZES:
        rows = [x for x in log if x["mode"] == mode and x["class"] == cls and x["voxels_changed"]]
        if not rows:
            continue
        s = {"n": len(rows),
             "mismatches": sum(v["face_mismatches"] + v["sprig_mismatches"] + v["connectivity_mismatches"]
                               for v in [x["verify"] for x in rows] if v),
             "verified": sum(1 for x in rows if x["verify"]),
             "multi_chunk": sum(1 for x in rows if x["chunks"] > 1), "max_chunks": max(x["chunks"] for x in rows),
             "floods": sum(x["connect_floods"] or 0 for x in rows),
             "errors": [x["error"] for x in rows if x["error"]][:3]}
        for kk in ("rows_meshed", "plan_ms", "rows_wall_ms", "cluster_ms", "sprig_ms", "connect_ms", "set_ms",
                   "rebuild_ms", "assemble_ms", "assemble_bytes"):
            v = [x[kk] for x in rows if x[kk] is not None]
            s[kk] = {"median": q(v, 0.5), "p90": q(v, 0.9), "max": q(v, 1.0)}
        ready = [x["rebuild_ms"] + x["assemble_ms"] for x in rows]
        s["ready_ms"] = {"median": q(ready, 0.5), "p90": q(ready, 0.9), "max": q(ready, 1.0)}
        up = [sum(x["upload_ms"]) for x in rows if x["upload_ms"]]
        s["upload_ms"] = {"median": q(up, 0.5), "p90": q(up, 0.9)}
        summary[f"{mode}_{cls}"] = s
        print(mode, cls, json.dumps(s), flush=True)
out["summary"] = summary


def pose_check(name, tag):
    x, y, z, yaw, pitch = poses[name]
    call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
    fx, fy, fz = (math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(pitch)),
                  math.sin(math.radians(yaw)) * math.cos(math.radians(pitch)))
    B = shot(f"f31_{tag}_B.png", 6.0)
    for k in range(20):
        d = 6 + k * 0.8 if name == "forest_floor_open" else 30 + k * 4
        c = [int((x + fx * d) * 8), int((y + fy * d) * 8) - 8, int((z + fz * d) * 8)]
        call("POST", P, {"edit": {"material": "air" if k % 2 == 0 else "Stone", "sphere": {"c": c, "r": 6 + k % 5}}})
        time.sleep(0.2)
    I = shot(f"f31_{tag}_I.png", 4.0)
    sI = stats()
    I2 = shot(f"f31_{tag}_I2.png")
    call("POST", P, {"edit": {"replace_all": True}})
    F = shot(f"f31_{tag}_F.png", 4.0)
    sF = stats()
    res = {"I_vs_F": px(I, F), "I_vs_I2": px(I, I2), "B_vs_I": px(B, I),
           "drawn_I": [sI["cluster_faces_drawn"], sI["clusters_drawn"]],
           "drawn_F": [sF["cluster_faces_drawn"], sF["clusters_drawn"]]}
    print("pixels", name, json.dumps(res), flush=True)
    return res


out["pixels"] = {"forest_floor_open": pose_check("forest_floor_open", "ffo"),
                 "overview_south": pose_check("overview_south", "ovs")}
for k in range(200):
    call("POST", P, {"edit": random_edit(SIZES[k % 4][1])})
x, y, z, yaw, pitch = poses["overview_south"]
call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
I3 = shot("f31_ovs_I3.png", 5.0)
s3 = stats()
call("POST", P, {"edit": {"replace_all": True}})
F3 = shot("f31_ovs_F3.png", 4.0)
sF3 = stats()
out["churn"] = {"I3_vs_F3": px(I3, F3), "drawn_I3": s3["cluster_faces_drawn"], "drawn_F3": sF3["cluster_faces_drawn"]}
print("churn", json.dumps(out["churn"]), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.05})
call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "voxfound_f31_gate.json"), "w"), indent=1)
