"""F2.16 gate (§4.6 item 9): incremental rebuild on the gate region (engine-generated: /api/debug/voxfound_preview Perlin
s7 flora, chunks 254..257 x 0..1 x 2..5, {"editable":true}; LOD off). Today's world hidden, wind 0, GI probes off.
1. 200 random edits (50 each of 1 voxel, 1 m, 2 m, 4 m; boxes and spheres; air and Stone / Dirt / Leaf / Wood; a quarter
   of them centred on a chunk border or corner), every one VERIFIED against a full FaceBuilderGpu rebuild of each
   touched chunk (faces, sprigs, connectivity), rows on the CPU executor (the default). Per edit: plan / rows / clusters / sprigs /
   connectivity / assemble ms and the whole re-upload on the next frame.
2. Device executor A/B: 10 edits per size with {"device":true}, also verified. Then 10 mixed triples on one chunk
   (device at A, CPU at A, device next to A): the device's resident grid must not go stale behind a CPU edit.
3. Pixels at forest_floor_open: B (before), 20 visible edits, I (incremental), I2 (control), F (replace_all = full
   rebuild of every chunk from the same voxels): I vs F, I vs I2, B vs I.
usage: voxfound_f216_gate.py poses.json"""
import json, math, os, random, shutil, statistics, sys, time, urllib.request

import numpy as np
from PIL import Image

API = "http://127.0.0.1:8120"
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "..", "..", "phyxel.log")
LOG0 = os.path.getsize(LOG) if os.path.exists(LOG) else 0


def device_lost():   # phyxel.log is appended across launches: read only what this run wrote
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
    return int((np.max(np.abs(a - b), axis=2) > 8).sum())


poses = json.load(open(sys.argv[1]))
out = {}
t = time.time()
r = call("POST", "/api/debug/voxfound_preview",
         {"type": "Perlin", "seed": 7, "flora": True, "hide_world": False, "editable": True,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}})
out["build"] = {k: r.get(k) for k in ("faces", "sprigs", "edit_store_bytes", "build_wall_ms", "error")}
out["build"]["wall_s"] = round(time.time() - t, 1)
print("build", json.dumps(out["build"]), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
call("POST", "/api/debug/voxfound_preview", {"hide_world": True})
call("POST", "/api/debug/voxfound_preview", {"show_preview": True})

LO = (254 * 256, 0, 2 * 256)
HI = (258 * 256 - 1, 2 * 256 - 1, 6 * 256 - 1)
MATS = ["air", "Stone", "air", "Dirt", "Leaf", "air", "Wood"]
SIZES = [("voxel", 1), ("1m", 8), ("2m", 16), ("4m", 32)]
rng = random.Random(216)


def random_edit(sz, device=False):
    c = [rng.randint(LO[0], HI[0]), rng.randint(10 * 8, 45 * 8), rng.randint(LO[2], HI[2])]
    if rng.random() < 0.25:   # on a chunk border / corner
        for a in rng.sample([0, 1, 2], rng.randint(1, 3)):
            k = round(c[a] / 256) * 256
            c[a] = min(max(k, LO[a] + sz), HI[a] - sz) if a != 1 else 256
    mat = rng.choice(MATS)
    if rng.random() < 0.6 or sz == 1:
        e = {"material": mat, "lo": [c[0] - sz // 2, c[1] - sz // 2, c[2] - sz // 2],
             "hi": [c[0] - sz // 2 + sz - 1, c[1] - sz // 2 + sz - 1, c[2] - sz // 2 + sz - 1]}
    else:
        e = {"material": mat, "sphere": {"c": c, "r": sz / 2}}
    e["verify"] = True
    if device:
        e["device"] = True
    return e


KEYS = ("voxels_changed", "faces_removed", "faces_added", "rows_meshed", "sprigs_added", "plan_ms", "rows_wall_ms",
        "rows_gpu_ms", "cluster_ms", "sprig_ms", "connect_ms", "set_ms", "rebuild_ms", "assemble_ms", "upload_bytes",
        "resident_misses", "rows_on_device", "rows_error")
log = []
for mode in ("cpu", "device"):
    for cls, sz in SIZES:
        for k in range(50 if mode == "cpu" else 10):
            e = random_edit(sz, device=(mode == "device"))
            res = call("POST", "/api/debug/voxfound_preview", {"edit": e})
            time.sleep(0.2)
            st = call("POST", "/api/debug/voxfound_preview", {"stats": True})
            row = {"mode": mode, "class": cls, "edit": e, "chunks": len(res.get("chunks_rebuilt", [])),
                   "verify": res.get("verify"), "upload_ms": st.get("upload_ms_last"), "error": res.get("error")}
            row.update({kk: res.get(kk) for kk in KEYS})
            log.append(row)
        print(mode, cls, "done", flush=True)
# mixed executors on one chunk (the stale-resident case): device edit at A, CPU edit at A, device edit next to A --
# the third one's faces read A's voxels, which only the CPU executor changed (12 face mismatches before the fix)
for k in range(10):
    c = [rng.randint(LO[0] + 64, HI[0] - 64), rng.randint(20 * 8, 40 * 8), rng.randint(LO[2] + 64, HI[2] - 64)]
    box = lambda dx: {"lo": [c[0] + dx, c[1], c[2]], "hi": [c[0] + dx + 7, c[1] + 7, c[2] + 7]}
    for e in ({"material": "Stone", **box(0), "device": True}, {"material": "air", **box(0)},
              {"material": "Stone", **box(8), "device": True}):
        e["verify"] = True
        res = call("POST", "/api/debug/voxfound_preview", {"edit": e})
        row = {"mode": "mixed", "class": "1m", "edit": e, "chunks": len(res.get("chunks_rebuilt", [])),
               "verify": res.get("verify"), "upload_ms": None, "error": res.get("error")}
        row.update({kk: res.get(kk) for kk in KEYS})
        log.append(row)
print("mixed done", flush=True)
with open(os.path.join(HERE, "voxfound_f216_gate_edits.jsonl"), "w") as f:
    for row in log:
        f.write(json.dumps(row) + "\n")


def q(v, p):
    v = sorted(v)
    return round(v[min(len(v) - 1, int(p * len(v)))], 3) if v else None


summary = {}
for mode in ("cpu", "device", "mixed"):
    for cls, _ in SIZES:
        rows = [x for x in log if x["mode"] == mode and x["class"] == cls and x["voxels_changed"]]
        if not rows:
            continue
        s = {"edits_changing_voxels": len(rows),
             "face_mismatches": sum(x["verify"]["face_mismatches"] for x in rows if x["verify"]),
             "sprig_mismatches": sum(x["verify"]["sprig_mismatches"] for x in rows if x["verify"]),
             "connectivity_mismatches": sum(x["verify"]["connectivity_mismatches"] for x in rows if x["verify"]),
             "on_device": sum(1 for x in rows if x["rows_on_device"]),
             "multi_chunk": sum(1 for x in rows if x["chunks"] > 1), "max_chunks": max(x["chunks"] for x in rows),
             "errors": [x["error"] or x["rows_error"] for x in rows if x["error"] or x["rows_error"]][:3]}
        for kk in ("rows_meshed", "plan_ms", "rows_wall_ms", "rows_gpu_ms", "cluster_ms", "sprig_ms", "connect_ms",
                   "set_ms", "rebuild_ms", "assemble_ms", "upload_bytes"):
            v = [x[kk] for x in rows if x[kk] is not None]
            s[kk] = {"median": q(v, 0.5), "p90": q(v, 0.9), "max": q(v, 1.0)}
        up = [sum(x["upload_ms"]) for x in rows if x["upload_ms"]]
        s["reupload_ms"] = {"median": q(up, 0.5), "p90": q(up, 0.9), "max": q(up, 1.0)}
        summary[f"{mode}_{cls}"] = s
        print(mode, cls, json.dumps(s), flush=True)
out["summary"] = summary

# 3. pixels at forest_floor_open
x, y, z, yaw, pitch = poses["forest_floor_open"]
call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
B = shot("f216_ffo_B.png", 6.0)
fx, fy, fz = (math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(pitch)),
              math.sin(math.radians(yaw)) * math.cos(math.radians(pitch)))
vis = []
for k in range(20):
    d = 6 + k * 0.8
    c = [int((x + fx * d) * 8), int((y + fy * d) * 8) - 8, int((z + fz * d) * 8)]
    mat = "air" if k % 2 == 0 else "Stone"
    res = call("POST", "/api/debug/voxfound_preview",
               {"edit": {"material": mat, "sphere": {"c": c, "r": 6 + k % 5}, "verify": True}})
    vis.append({kk: res.get(kk) for kk in ("voxels_changed", "verify", "rebuild_ms", "rows_wall_ms")})
I = shot("f216_ffo_I.png", 4.0)
I2 = shot("f216_ffo_I2.png")
rep = call("POST", "/api/debug/voxfound_preview", {"edit": {"replace_all": True}})
F = shot("f216_ffo_F.png", 4.0)
F2 = shot("f216_ffo_F2.png")
out["pixels"] = {"visible_edits": vis, "replace_all": rep, "I_vs_F_gt8": px(I, F), "I_vs_I2_gt8": px(I, I2),
                 "F_vs_F2_gt8": px(F, F2), "B_vs_I_gt8": px(B, I)}
print("pixels", json.dumps({k: v for k, v in out["pixels"].items() if k != "visible_edits"}), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.05})
call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "voxfound_f216_gate.json"), "w"), indent=1)
