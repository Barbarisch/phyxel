"""F7.6d live (docs/evidence/voxfound_f7_prediction.txt D1-D4): LOD tiers 2/3 on the switched world. VoxFoundGate (8120).
  red   : lod_stub true (edits mark nothing dirty) -- D2 / D3 as below.
  green : D1 lod_check at no edits; D3 lod_draw 3 at the stated pose, a fill (4x4x4 cubes of Stone in air), drain,
          screenshots before / after (pixel change in the F2.19 crop); D2 a brush break + undo, drain, lod_check;
          lod_auto on + another fill: the selection survives; D4 the refresh costs.
The region: editable + lod_levels 3, load_saved:false (the generator's). Every artifact: WorldGenerator Perlin s7 +
flora (the 1/8 m region), coarse levels by LodBuilderGpu + FaceBuilderGpu, edited through the engine's voxel API."""
import json, os, shutil, sys, time, urllib.request
from PIL import Image, ImageChops

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False, "lod_levels": 3,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
POSE = {"mode": "free", "position": {"x": 8131.5, "y": 36.0, "z": 56.0}, "yaw": 90, "pitch": -30}
CROP = (425, 105, 1195, 620)


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def wait_async(r):
    aid = r.get("async_id")
    for _ in range(600):
        if not aid:
            return r
        s = call("GET", f"/api/async/{aid}")
        if s.get("status") == "complete" or "error" in s:
            return s
        time.sleep(0.1)
    return {"timeout": aid}


def drain():
    t0 = time.time()
    while True:
        s = call("POST", P, {"lod_status": True})
        if s.get("dirty", 0) == 0 or time.time() - t0 > 120:
            s["drain_wall_s"] = round(time.time() - t0, 2)
            return s
        time.sleep(0.2)


def shot(name):
    time.sleep(2.0)
    dst = os.path.join(EVID, name)
    shutil.copy(os.path.join(REPO, call("GET", "/api/screenshot")["path"]), dst)
    return dst


def pix(a, b):
    A = Image.open(a).convert("RGB").crop(CROP)
    B = Image.open(b).convert("RGB").crop(CROP)
    d = ImageChops.difference(A, B).convert("L")
    n = sum(1 for v in d.getdata() if v > 8)
    return round(100.0 * n / (A.size[0] * A.size[1]), 3)


def fill(lo, hi, mat="Stone"):
    r = wait_async(call("POST", "/api/world/fill", {"x1": lo[0], "y1": lo[1], "z1": lo[2], "x2": hi[0], "y2": hi[1],
                                                     "z2": hi[2], "material": mat}))
    return {k: r.get(k) for k in ("placed", "voxels_changed", "world", "undo", "error")}


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    b = call("POST", P, REGION)
    out["region"] = {k: b.get(k) for k in ("faces", "chunks", "error", "lod_faces")}
    if mode == "red":
        out["stub"] = call("POST", P, {"lod_stub": True})
    out["switch"] = call("POST", P, {"world_switch": True, "load_saved": False})
    if mode == "green":
        c = call("POST", P, {"lod_check": True})
        side = lambda ch: ch[0] in (254, 257) or ch[2] in (2, 5)
        out["D1"] = {"mismatched_by_level": c.get("mismatched_by_level"), "faces_by_level": c.get("faces_by_level"),
                     "interior": [r for r in c.get("chunks", []) if not side(r["chunk"])],
                     "side_mismatch_by_level": [sum(l["mismatched"] for r in c.get("chunks", []) if side(r["chunk"])
                                                    for l in r["levels"] if l["level"] == k) for k in (1, 2, 3)],
                     "side_faces_by_level": [sum(l["faces"] for r in c.get("chunks", []) if side(r["chunk"])
                                                 for l in r["levels"] if l["level"] == k) for k in (1, 2, 3)]}
    call("POST", "/api/camera", POSE)
    out["lod_draw"] = call("POST", P, {"lod_draw": 3})
    a = shot(f"voxfound_f76d_{mode}_0_level3_before.png")
    out["fill"] = fill([8130, 30, 64], [8133, 33, 67])
    out["status_after_fill"] = call("POST", P, {"lod_status": True})
    out["drain"] = drain()
    bb = shot(f"voxfound_f76d_{mode}_1_level3_after_fill.png")
    out["D3_pixels_changed_pct"] = pix(a, bb)
    out["brush"] = {k: v for k, v in call("POST", P, {"edit": {"brush": {"at": [65059, 220, 535], "size": 8},
                                                               "material": "air"}}).items()
                    if k in ("voxels_changed", "undo")}
    out["undo"] = {k: v for k, v in call("POST", "/api/undo", {}).items() if k in ("voxels_restored", "world")}
    out["drain2"] = drain()
    c2 = call("POST", P, {"lod_check": True})
    out["D2_mismatched_by_level"] = c2.get("mismatched_by_level")
    out["D2_bad_chunks"] = [r["chunk"] for r in c2.get("chunks", []) if any(l["mismatched"] for l in r["levels"])]
    if mode == "green":
        out["lod_auto"] = call("POST", P, {"lod_auto": True, "lod_px": 8.0})
        out["fill2"] = fill([8130, 34, 64], [8131, 35, 65], "Bricks")
        out["status_after_fill2"] = call("POST", P, {"lod_status": True})
        out["drain3"] = drain()
        c3 = call("POST", P, {"lod_check": True})
        out["auto_mismatched_by_level"] = c3.get("mismatched_by_level")
        out["D4"] = call("POST", P, {"lod_status": True})
    call("POST", P, {"world_switch": False})
    if mode == "red":
        call("POST", P, {"lod_stub": False})
    print(json.dumps(out)[:6000])
    with open(os.path.join(EVID, "voxfound_f76d_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
