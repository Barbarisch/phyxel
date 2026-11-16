"""F3.2b gate (VoxelFoundationPlan §9b): brushes and picking on the gate region (engine-generated:
/api/debug/voxfound_preview Perlin s7 flora, chunks 254..257 x 0..1 x 2..5, {"editable":true}; LOD off; face pool on).
Today's world hidden, wind 0, GI probes off (as F2.16-F3.2a).
1. F3.1's edit generator (seed 216, the same 200 edits as F3.1 / F3.2a; boxes now through applyBox, covered blocks
   written whole), every edit verified.
2. Brushes aimed by picking: from each of the 5 poses, rays through a spread of view directions; pick (size s), then
   break the brush cell holding the hit (air) or place Stone in the brush cell in front of the hit face, alternating;
   40 per size (1, 2, 4, 8 voxels), every edit verified. Per pick: hit voxel solid, the voxel in front of the entry face
   air; pick ms and steps.
3. Pixels at forest_floor_open and overview_south: 20 visible brushes aimed by picks = I, I2 (control), replace_all = F.
usage: voxfound_f32b_gate.py poses.json"""
import json, math, os, random, shutil, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "..", "..", "phyxel.log")
LOG0 = os.path.getsize(LOG) if os.path.exists(LOG) else 0
VIEW = (245, 47, 1195, 670)
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
    return {"upload_ms": s.get("upload_ms_last"), "cluster_faces_drawn": s.get("cluster_faces_drawn"),
            "clusters_drawn": s.get("clusters_drawn")}


def fwd(yaw, pitch):
    return [math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(pitch)),
            math.sin(math.radians(yaw)) * math.cos(math.radians(pitch))]


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


KEYS = ("voxels_changed", "rows_meshed", "plan_ms", "rows_wall_ms", "cluster_ms", "sprig_ms", "connect_ms",
        "connect_floods", "set_ms", "collapse_ms", "blocks_filled", "edit_store_bytes", "rebuild_ms", "assemble_ms",
        "assemble_bytes", "brush_size")


def run(e, mode, cls, extra=None):
    e["verify"] = True
    res = call("POST", P, {"edit": e})
    time.sleep(0.3)
    st = stats()
    row = {"mode": mode, "class": cls, "edit": e, "chunks": len(res.get("chunks_rebuilt", [])),
           "verify": res.get("verify"), "error": res.get("error"), "upload_ms": st["upload_ms"]}
    row.update({k: res.get(k) for k in KEYS})
    if extra:
        row.update(extra)
    return row


log = []
call("POST", P, {"edit": {"material": "Stone", "lo": [65607, 196, 885], "hi": [65614, 203, 892]}})   # pool layout
for cls, sz in SIZES:
    for k in range(50):
        log.append(run(random_edit(sz), "generator", cls))
    print("generator", cls, "done", flush=True)

# 2. brushes aimed by picks
prng = random.Random(3207)
picks = []
names = list(poses.keys())
for size in (1, 2, 4, 8):
    n = 0
    tries = 0
    while n < 40 and tries < 400:
        tries += 1
        name = names[tries % len(names)]
        x, y, z, yaw, pitch = poses[name]
        d = fwd(yaw + prng.uniform(-25, 25), pitch + prng.uniform(-15, 10))
        pk = call("POST", P, {"pick": {"origin": [x, y, z], "dir": d, "max_m": 96, "size": size}})
        if not pk.get("hit"):
            picks.append({"pose": name, "hit": False, "pick_ms": pk.get("pick_ms"), "steps": pk.get("steps")})
            continue
        picks.append({"pose": name, "hit": True, "pick_ms": pk["pick_ms"], "steps": pk["steps"],
                      "normal": pk["normal"], "front_material": pk["front_material"], "distance_m": pk["distance_m"],
                      "material": pk["material"]})
        place = n % 2 == 1
        cell = pk["place_cell"] if place else pk["break_cell"]
        e = {"material": "Stone" if place else "air", "brush": {"at": cell, "size": size}}
        log.append(run(e, "brush", f"s{size}", {"pick_ms": pk["pick_ms"], "place": place, "pose": name}))
        n += 1
    print("brush size", size, "done", n, "edits", tries, "rays", flush=True)
with open(os.path.join(HERE, "voxfound_f32b_gate_edits.jsonl"), "w") as f:
    for row in log:
        f.write(json.dumps(row) + "\n")
with open(os.path.join(HERE, "voxfound_f32b_gate_picks.jsonl"), "w") as f:
    for row in picks:
        f.write(json.dumps(row) + "\n")


def q(v, p):
    v = sorted(v)
    return round(v[min(len(v) - 1, int(p * len(v)))], 3) if v else None


summary = {}
for mode, classes in (("generator", [c for c, _ in SIZES]), ("brush", ["s1", "s2", "s4", "s8"])):
    for cls in classes:
        rows = [x for x in log if x["mode"] == mode and x["class"] == cls and x["voxels_changed"]]
        if not rows:
            continue
        s = {"n": len(rows), "n_all": len([x for x in log if x["mode"] == mode and x["class"] == cls]),
             "mismatches": sum(v["face_mismatches"] + v["sprig_mismatches"] + v["connectivity_mismatches"]
                               for v in [x["verify"] for x in rows] if v),
             "verified": sum(1 for x in rows if x["verify"]),
             "errors": [x["error"] for x in rows if x["error"]][:3],
             "blocks_filled": sum(x["blocks_filled"] or 0 for x in rows)}
        for kk in ("set_ms", "collapse_ms", "rows_wall_ms", "connect_ms", "rebuild_ms", "assemble_ms"):
            v = [x[kk] for x in rows if x[kk] is not None]
            s[kk] = {"median": q(v, 0.5), "p90": q(v, 0.9)}
        ready = [x["rebuild_ms"] + x["assemble_ms"] for x in rows]
        s["ready_ms"] = {"median": q(ready, 0.5), "p90": q(ready, 0.9), "max": q(ready, 1.0)}
        summary[f"{mode}_{cls}"] = s
        print(mode, cls, json.dumps(s), flush=True)
hits = [p for p in picks if p["hit"]]
out["picks"] = {"rays": len(picks), "hits": len(hits),
                "front_not_air": sum(1 for p in hits if p["front_material"] != 0 and any(p["normal"])),
                "from_inside": sum(1 for p in hits if not any(p["normal"])),
                "pick_ms": {"median": q([p["pick_ms"] for p in picks], 0.5), "p90": q([p["pick_ms"] for p in picks], 0.9),
                            "max": q([p["pick_ms"] for p in picks], 1.0)},
                "steps": {"median": q([p["steps"] for p in picks], 0.5), "max": q([p["steps"] for p in picks], 1.0)},
                "distance_m": {"median": q([p["distance_m"] for p in hits], 0.5), "max": q([p["distance_m"] for p in hits], 1.0)}}
print("picks", json.dumps(out["picks"]), flush=True)
out["summary"] = summary


def pose_check(name, tag):
    x, y, z, yaw, pitch = poses[name]
    call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
    B = shot(f"f32b_{tag}_B.png", 6.0)
    done = 0
    for k in range(60):
        if done >= 20:
            break
        d = fwd(yaw + (k % 7 - 3) * 4.0, pitch + (k // 7 - 3) * 3.0)
        size = (1, 2, 4, 8)[k % 4]
        pk = call("POST", P, {"pick": {"origin": [x, y, z], "dir": d, "max_m": 96, "size": size}})
        if not pk.get("hit"):
            continue
        place = k % 2 == 1
        call("POST", P, {"edit": {"material": "Stone" if place else "air",
                                  "brush": {"at": pk["place_cell"] if place else pk["break_cell"], "size": size}}})
        done += 1
        time.sleep(0.2)
    I = shot(f"f32b_{tag}_I.png", 4.0)
    sI = stats()
    I2 = shot(f"f32b_{tag}_I2.png")
    call("POST", P, {"edit": {"replace_all": True}})
    F = shot(f"f32b_{tag}_F.png", 4.0)
    sF = stats()
    res = {"edits": done, "I_vs_F": px(I, F), "I_vs_I2": px(I, I2), "B_vs_I": px(B, I),
           "drawn_I": [sI["cluster_faces_drawn"], sI["clusters_drawn"]],
           "drawn_F": [sF["cluster_faces_drawn"], sF["clusters_drawn"]]}
    print("pixels", name, json.dumps(res), flush=True)
    return res


out["pixels"] = {"forest_floor_open": pose_check("forest_floor_open", "ffo"),
                 "overview_south": pose_check("overview_south", "ovs")}
out["edit_store_bytes_end"] = log[-1]["edit_store_bytes"] if log else None
call("POST", "/api/debug/foliage", {"windStrength": 0.05})
call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "voxfound_f32b_gate.json"), "w"), indent=1)
print("end", flush=True)
