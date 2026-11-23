"""F7.24 live (docs/evidence/voxfound_f7_prediction.txt P1-P5): the rest of the voxel API on the switched world.
Project VoxFoundBoot (switched). Modes: red (api_stub) / green. An air box above the ground at x 8200, y 30, z 260
(cubes); the shape S built by the voxel API at mixed sizes. Every artifact: WorldGenerator Perlin s7 + flora + those
API edits (hand-placed through the API, by this driver)."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
SX, SY, SZ = 8200, 30, 260   # S's min cube


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def box(cx, cy, cz, n=2):   # solid voxels of an n^3-cube box at cube (cx, cy, cz): sorted [dx, dy, dz, mat]
    r = call("POST", P, {"box_voxels": {"lo": [cx * 8, cy * 8, cz * 8], "hi": [(cx + n) * 8 - 1, (cy + n) * 8 - 1, (cz + n) * 8 - 1]}})
    return sorted(tuple(v) for v in r.get("voxels", []))


def summary(r):
    return {k: r.get(k) for k in ("success", "world", "voxels_changed", "error", "voxel_count", "size_voxels", "moved_voxels")}


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    if mode == "red":
        out["stub"] = call("POST", P, {"api_stub": True})
    out["solid_before"] = [len(box(SX, SY, SZ)), len(box(SX + 10, SY, SZ)), len(box(SX + 14, SY, SZ)),
                           len(box(SX, SY, SZ + 20)), len(box(SX, SY - 4, SZ - 10, 10))]
    # SETUP (run 1 found flora in the paste / move destinations; paste writes only the clip's solids): clear them
    for cx, cz in [(SX, SZ), (SX + 10, SZ), (SX + 14, SZ), (SX, SZ + 20)]:
        call("POST", "/api/world/clear", {"x1": cx, "y1": SY, "z1": cz, "x2": cx + 1, "y2": SY + 1, "z2": cz + 1})
    time.sleep(1.0)
    out["solid_after_setup"] = [len(box(SX, SY, SZ)), len(box(SX + 10, SY, SZ)), len(box(SX + 14, SY, SZ)),
                                len(box(SX, SY, SZ + 20))]
    p1_before = len(box(SX, SY - 4, SZ - 10, 10))
    # P1: a batch of 10 cubes in a row at y SY - 4, z SZ - 10
    batch = [{"x": SX + i, "y": SY - 4, "z": SZ - 10, "material": "Stone"} for i in range(10)]
    out["P1_batch"] = summary(call("POST", "/api/world/voxel/batch", {"voxels": batch}))
    out["P1_added"] = len(box(SX, SY - 4, SZ - 10, 10)) - p1_before
    out["P1_undo"] = {k: v for k, v in call("POST", "/api/undo", {}).items() if k in ("success", "world", "undone", "label", "error")}
    out["P1_after_undo_delta"] = len(box(SX, SY - 4, SZ - 10, 10)) - p1_before
    # the shape S (green only edits the region; red edits today's chunks)
    v = lambda x, y, z: (SX * 8 + x, SY * 8 + y, SZ * 8 + z)
    call("POST", "/api/world/voxel", {"x": SX, "y": SY, "z": SZ, "material": "Stone"})
    for (x, y, z), size, mat in [((9, 3, 5), 2, "Bricks"), ((14, 14, 1), 1, "Glass"), ((3, 10, 13), 1, "Glass"),
                                 ((12, 9, 12), 4, "Wood")]:
        X, Y, Z = v(x, y, z)
        call("POST", "/api/world/voxel", {"x": X, "y": Y, "z": Z, "units": "voxel", "size": size, "material": mat})
    S = box(SX, SY, SZ)
    out["S_solid"] = len(S)
    # P2: copy + paste at +10 m x
    out["P2_copy"] = summary(call("POST", "/api/clipboard/copy", {"x1": SX, "y1": SY, "z1": SZ, "x2": SX + 1, "y2": SY + 1, "z2": SZ + 1}))
    out["P2_paste"] = summary(call("POST", "/api/clipboard/paste", {"x": SX + 10, "y": SY, "z": SZ}))
    D = box(SX + 10, SY, SZ)
    out["P2_equal"] = D == S and len(S) > 0
    # P3: paste rotate 90 at +14 m x: expected x' = 15 - z, z' = x
    out["P3_paste"] = summary(call("POST", "/api/clipboard/paste", {"x": SX + 14, "y": SY, "z": SZ, "rotate": 90}))
    R = box(SX + 14, SY, SZ)
    exp = sorted((15 - z, y, x, m) for x, y, z, m in S)
    out["P3_equal"] = R == exp and len(S) > 0
    # P5: snapshot S's box, clear it, restore
    out["P5_create"] = summary(call("POST", "/api/snapshot/create", {"name": "f724", "x1": SX, "y1": SY, "z1": SZ,
                                                                     "x2": SX + 1, "y2": SY + 1, "z2": SZ + 1}))
    call("POST", "/api/world/clear", {"x1": SX, "y1": SY, "z1": SZ, "x2": SX + 1, "y2": SY + 1, "z2": SZ + 1})
    time.sleep(1.0)
    out["P5_cleared"] = len(box(SX, SY, SZ))
    out["P5_restore"] = summary(call("POST", "/api/snapshot/restore", {"name": "f724"}))
    out["P5_equal"] = box(SX, SY, SZ) == S and len(S) > 0
    # P4: move S by +20 m z
    out["P4_move"] = summary(call("POST", "/api/world/move_region", {"x1": SX, "y1": SY, "z1": SZ, "x2": SX + 1, "y2": SY + 1,
                                                                     "z2": SZ + 1, "dx": SX, "dy": SY, "dz": SZ + 20}))
    out["P4_source_solid"] = len(box(SX, SY, SZ))
    out["P4_equal"] = box(SX, SY, SZ + 20) == S and len(S) > 0
    if mode == "red":
        call("POST", P, {"api_stub": False})
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f724_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
