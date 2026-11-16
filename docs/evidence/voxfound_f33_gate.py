"""F3.3 gate (VoxelFoundationPlan §9b): save / load round trip of the editable gate region through codec v3 and world.db
(engine-generated: /api/debug/voxfound_preview Perlin s7 flora, chunks 254..257 x 0..1 x 2..5, {"editable":true}).
Today's world hidden, wind 0, GI off.
0. Build, clear the region's saved rows; build again with load_saved (control: nothing loaded).
1. 200 edits (F2.16's generator, seed 216) + 40 brushes aimed by picks; per-chunk voxel hashes H0; captures A at
   forest_floor_open and overview_south.
2. Save: only the edited chunks written (count = chunks whose edited flag is set); blob bytes, encode ms.
3. Control: rebuild from the generator WITHOUT load -> the edited chunks' hashes differ from H0, the others equal.
4. Rebuild with load_saved -> every chunk's hash = H0; captures L at the two poses = A (0 px); replace_all (a full
   rebuild of every chunk) = F, L vs F 0 px; save again -> byte-identical blobs (canonical).
usage: voxfound_f33_gate.py poses.json"""
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
    return {"faces": r.get("faces"), "error": r.get("error"), "load_saved": r.get("load_saved"),
            "wall_s": round(time.time() - t, 1)}


def hashes():
    return {tuple(c["chunk"]): (c["hash"], c["edited"]) for c in call("POST", P, {"store_hash": True})["chunks"]}


def fwd(yaw, pitch):
    return [math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(pitch)),
            math.sin(math.radians(yaw)) * math.cos(math.radians(pitch))]


def captures(tag):
    out = {}
    for name in ("forest_floor_open", "overview_south"):
        x, y, z, yaw, pitch = poses[name]
        call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": yaw, "pitch": pitch})
        out[name] = shot(f"f33_{name[:3]}_{tag}.png", 6.0)
    return out


poses = json.load(open(sys.argv[1]))
out = {}
print("build", json.dumps(build(False)), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.0})
call("POST", "/api/debug/gi", {"enabled": False})
clr = call("POST", P, {"saved_clear": True})
out["clear"] = {"cleared": clr["cleared"], "rows_left": clr["saved_rows_total"]}
b0 = build(True)
out["control_load_nothing"] = b0["load_saved"]
print("cleared", json.dumps(out["clear"]), "control load", json.dumps(b0["load_saved"]), flush=True)

LO = (254 * 256, 0, 2 * 256)
HI = (258 * 256 - 1, 2 * 256 - 1, 6 * 256 - 1)
MATS = ["air", "Stone", "air", "Dirt", "Leaf", "air", "Wood"]
rng = random.Random(216)


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


mism = 0
for k in range(200):
    e = random_edit((1, 8, 16, 32)[k // 50])
    if k % 10 == 0:
        e["verify"] = True
    r = call("POST", P, {"edit": e})
    v = r.get("verify") or {}
    mism += v.get("face_mismatches", 0) + v.get("sprig_mismatches", 0) + v.get("connectivity_mismatches", 0)
prng = random.Random(3307)
names = list(poses.keys())
brushes = 0
for k in range(120):
    if brushes >= 40:
        break
    x, y, z, yaw, pitch = poses[names[k % len(names)]]
    size = (1, 2, 4, 8)[k % 4]
    pk = call("POST", P, {"pick": {"origin": [x, y, z], "dir": fwd(yaw + prng.uniform(-25, 25), pitch + prng.uniform(-15, 10)),
                                   "max_m": 96, "size": size}})
    if not pk.get("hit"):
        continue
    place = brushes % 2 == 1
    call("POST", P, {"edit": {"material": "Wood" if place else "air",
                              "brush": {"at": pk["place_cell"] if place else pk["break_cell"], "size": size}}})
    brushes += 1
out["edits"] = {"generator": 200, "brushes": brushes, "verify_mismatches": mism}
H0 = hashes()
edited = sorted(c for c, (h, e) in H0.items() if e)
print("edited chunks", len(edited), "of", len(H0), "verify mismatches", mism, flush=True)
A = captures("A")

s1 = call("POST", P, {"save": True})
out["save"] = {k: s1.get(k) for k in ("saved_chunks", "region_chunks", "bytes", "encode_ms", "write_ms", "error")}
out["save"]["per_chunk"] = [(c["chunk"], c["bytes"], c["store_bytes"], round(c["encode_ms"], 2)) for c in s1.get("chunks", [])]
print("save", json.dumps({k: v for k, v in out["save"].items() if k != "per_chunk"}), flush=True)

b1 = build(False)
H1 = hashes()
out["control_generator"] = {"edited_differ": sum(1 for c in edited if H1[c][0] != H0[c][0]),
                            "unedited_equal": sum(1 for c in H0 if c not in edited and H1[c][0] == H0[c][0]),
                            "unedited": len(H0) - len(edited)}
print("control generator", json.dumps(out["control_generator"]), flush=True)

b2 = build(True)
out["load"] = b2["load_saved"]
out["load"]["wall_s"] = b2["wall_s"]
H2 = hashes()
out["roundtrip"] = {"chunks_equal": sum(1 for c in H0 if H2[c][0] == H0[c][0]), "chunks": len(H0),
                    "edited_flags_equal": sum(1 for c in H0 if H2[c][1] == H0[c][1])}
print("load", json.dumps(out["load"]), "roundtrip", json.dumps(out["roundtrip"]), flush=True)
L = captures("L")
call("POST", P, {"edit": {"replace_all": True}})
F = captures("F")
L2 = captures("L2")   # control: the full rebuild again, nothing changed
out["pixels"] = {n: {"L_vs_A": px(L[n], A[n]), "L_vs_F": px(L[n], F[n]), "F_vs_L2": px(F[n], L2[n])} for n in A}
print("pixels", json.dumps(out["pixels"]), flush=True)
s2 = call("POST", P, {"save": True})
h1 = {tuple(c["chunk"]): c["blob_hash"] for c in s1.get("chunks", [])}
h2 = {tuple(c["chunk"]): c["blob_hash"] for c in s2.get("chunks", [])}
out["resave"] = {"chunks": len(h2), "identical": sum(1 for c in h1 if h2.get(c) == h1[c])}
print("resave", json.dumps(out["resave"]), flush=True)
call("POST", "/api/debug/foliage", {"windStrength": 0.05})
call("POST", "/api/debug/gi", {"enabled": True})
json.dump(out, open(os.path.join(HERE, "voxfound_f33_gate.json"), "w"), indent=1)
print("end", flush=True)
