"""F2.17 gate (§4.6 item 10): the face pool on the gate region (engine-generated: /api/debug/voxfound_preview Perlin
s7 flora, chunks 254..257 x 0..1 x 2..5, {"editable":true}; LOD off). Today's world hidden, wind 0, GI probes off.
1. Upload cost: 60 random edits (15 each of 1 voxel, 1 m, 2 m, 4 m) with the pool ON (partial copies), then 20 with
   it OFF (today's host-visible buffer + whole re-uploads), each read back from the next frames' stats.
2. Exactness, at forest_floor_open and overview_south: 20 visible edits (partial uploads) = I, I2 (control); pool OFF
   (the same image uploaded whole, host-visible) = P; pool ON again + replace_all (a fresh layout of a full rebuild)
   = F. Pixels compared inside the 3D viewport only (the status bar's FPS digits are not the scene), and the drawn
   counts (faces_drawn, cluster_faces_drawn) compared.
3. Churn: 200 more random edits on the pool; then the memory (image faces vs live) and I3 vs a fresh layout F3.
usage: voxfound_f217_gate.py poses.json"""
import json, math, os, random, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "..", "..", "phyxel.log")
LOG0 = os.path.getsize(LOG) if os.path.exists(LOG) else 0
VIEW = (245, 47, 1195, 670)   # the 3D viewport inside the editor's 1600 x 900 frame (x0, y0, x1, y1)
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


def px(a, b, view=True):
    if view:
        x0, y0, x1, y1 = VIEW
        a, b = a[y0:y1, x0:x1], b[y0:y1, x0:x1]
    return int((np.max(np.abs(a - b), axis=2) > 8).sum())


def stats():
    s = call("POST", P, {"stats": True})
    return {"pool": s.get("face_pool"), "upload_ms": s.get("upload_ms_last"), "faces_drawn": s.get("faces_drawn"),
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
rng = random.Random(217)


def random_edit(sz):
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


def run_edit(e, mode, cls):
    res = call("POST", P, {"edit": e})
    time.sleep(0.3)
    st = stats()
    return {"mode": mode, "class": cls, "edit": e, "voxels_changed": res.get("voxels_changed"),
            "chunks": len(res.get("chunks_rebuilt", [])), "assemble_ms": res.get("assemble_ms"),
            "rebuild_ms": res.get("rebuild_ms"), "verify": res.get("verify"), "error": res.get("error"), **st}


# 1. upload cost
log = [run_edit(random_edit(8), "pool_first", "1m")]   # the first edit lays the pool out (a full upload)
print("first", json.dumps(log[0]["pool"]), flush=True)
for cls, sz in SIZES:
    for k in range(15):
        e = random_edit(sz)
        if k % 5 == 0:
            e["verify"] = True
        log.append(run_edit(e, "pool", cls))
    print("pool", cls, "done", flush=True)
call("POST", P, {"face_pool": False})
time.sleep(1.0)
for cls, sz in SIZES:
    for k in range(5):
        log.append(run_edit(random_edit(sz), "off", cls))
call("POST", P, {"face_pool": True})
time.sleep(1.0)
print("off done", flush=True)


def q(v, p):
    v = sorted(v)
    return round(v[min(len(v) - 1, int(p * len(v)))], 3) if v else None


summary = {}
for mode in ("pool", "off"):
    for cls, _ in SIZES:
        rows = [x for x in log if x["mode"] == mode and x["class"] == cls and x["voxels_changed"]]
        if not rows:
            continue
        up = [sum(x["upload_ms"]) for x in rows if x["upload_ms"]]
        s = {"n": len(rows), "upload_ms": {"median": q(up, 0.5), "p90": q(up, 0.9), "max": q(up, 1.0)},
             "assemble_ms": {"median": q([x["assemble_ms"] for x in rows], 0.5)},
             "kinds": sorted({(x["pool"] or {}).get("last_kind", "-") for x in rows}),
             "face_bytes_median": q([(x["pool"] or {}).get("last_face_bytes", 0) for x in rows], 0.5),
             "cluster_slots_median": q([(x["pool"] or {}).get("last_cluster_slots", 0) for x in rows], 0.5),
             "record_ms_median": q([(x["pool"] or {}).get("last_record_ms", 0) for x in rows], 0.5),
             "verify_mismatches": sum((v.get("face_mismatches", 0) + v.get("sprig_mismatches", 0) +
                                       v.get("connectivity_mismatches", 0)) for v in
                                      [x["verify"] for x in rows if x["verify"]]),
             "errors": [x["error"] for x in rows if x["error"]][:3]}
        summary[f"{mode}_{cls}"] = s
        print(mode, cls, json.dumps(s), flush=True)
out["summary"] = summary
out["first_edit"] = log[0]


# 2. exactness at two poses
def pose_check(name, tag):
    x, y, z, yaw, pitch = poses[name]
    call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
    fx, fy, fz = (math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(pitch)),
                  math.sin(math.radians(yaw)) * math.cos(math.radians(pitch)))
    B = shot(f"f217_{tag}_B.png", 6.0)
    kinds = []
    for k in range(20):
        d = 6 + k * 0.8 if name == "forest_floor_open" else 30 + k * 4
        c = [int((x + fx * d) * 8), int((y + fy * d) * 8) - 8, int((z + fz * d) * 8)]
        call("POST", P, {"edit": {"material": "air" if k % 2 == 0 else "Stone", "sphere": {"c": c, "r": 6 + k % 5}}})
        time.sleep(0.2)
        kinds.append((stats()["pool"] or {}).get("last_kind"))
    I = shot(f"f217_{tag}_I.png", 4.0)
    sI = stats()
    I2 = shot(f"f217_{tag}_I2.png")
    call("POST", P, {"face_pool": False})
    Pp = shot(f"f217_{tag}_P.png", 4.0)
    sP = stats()
    call("POST", P, {"face_pool": True})
    rep = call("POST", P, {"edit": {"replace_all": True}})
    F = shot(f"f217_{tag}_F.png", 4.0)
    sF = stats()
    res = {"edit_upload_kinds": kinds, "I_vs_P": px(I, Pp), "I_vs_F": px(I, F), "I_vs_I2": px(I, I2),
           "B_vs_I": px(B, I), "I_vs_P_full_frame": px(I, Pp, False),
           "drawn_I": {k: sI[k] for k in ("faces_drawn", "cluster_faces_drawn", "clusters_drawn")},
           "drawn_P": {k: sP[k] for k in ("faces_drawn", "cluster_faces_drawn", "clusters_drawn")},
           "drawn_F": {k: sF[k] for k in ("faces_drawn", "cluster_faces_drawn", "clusters_drawn")},
           "replace_all": rep}
    print("pixels", name, json.dumps({k: v for k, v in res.items() if k != "replace_all"}), flush=True)
    return res


out["pixels"] = {"forest_floor_open": pose_check("forest_floor_open", "ffo"),
                 "overview_south": pose_check("overview_south", "ovs")}

# 3. churn
kinds = {}
for k in range(200):
    cls, sz = SIZES[k % 4]
    call("POST", P, {"edit": random_edit(sz)})
    if k % 10 == 9:
        time.sleep(0.2)
        kd = (stats()["pool"] or {}).get("last_kind")
        kinds[kd] = kinds.get(kd, 0) + 1
time.sleep(0.5)
st = stats()
out["churn"] = {"sampled_kinds": kinds, "pool": st["pool"]}
x, y, z, yaw, pitch = poses["overview_south"]
call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
I3 = shot("f217_ovs_I3.png", 5.0)
s3 = stats()
call("POST", P, {"edit": {"replace_all": True}})
F3 = shot("f217_ovs_F3.png", 4.0)
sF3 = stats()
out["churn"].update({"I3_vs_F3": px(I3, F3), "drawn_I3": s3["cluster_faces_drawn"], "drawn_F3": sF3["cluster_faces_drawn"],
                     "image_over_live": round(st["pool"]["image_faces"] / max(1, st["pool"]["live_faces"]), 3)})
print("churn", json.dumps(out["churn"]), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.05})
call("POST", "/api/debug/gi", {"enabled": True})
with open(os.path.join(HERE, "voxfound_f217_gate_edits.jsonl"), "w") as f:
    for row in log:
        f.write(json.dumps(row) + "\n")
json.dump(out, open(os.path.join(HERE, "voxfound_f217_gate.json"), "w"), indent=1)
