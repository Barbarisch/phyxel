"""F7.11 live (docs/evidence/voxfound_f7_prediction.txt W1-W4): build_structure v2 on the switched world. VoxFoundGate
(port 8120); the gate region with flora_window open, switched with load_saved false. The building: the ENGINE's
generator (StructureForge via POST /api/structure/build, schema v2, typology hall_house, footprint [16, 7]) -- never
hand-placed. Modes: control / red (struct_stub) / green. Every artifact: WorldGenerator Perlin s7 + flora (the region)
+ StructureForge's realized shell."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False, "flora_window": "open",
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
BUILD = {"schema": "v2", "type": "house", "typology": "hall_house", "footprint": [16, 7], "stories": 1,
         "position": {"x": 8130, "y": 28, "z": 66}}
PID = "f711probe"


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def hashes():
    return {",".join(map(str, c["chunk"])): c["hash_by_name"] for c in call("POST", P, {"store_hash": True})["chunks"]}


def today_cubes():
    return [call("GET", f"/api/world/voxel?x={x}&y={y}&z={z}").get("exists")
            for x in (8132, 8137, 8143) for y in (28, 29, 30) for z in (67, 69, 71)]


def structure_route():
    return "/api/structure/build"


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    call("POST", P, REGION)
    out["today_before"] = today_cubes()
    if mode == "red":
        out["stub"] = call("POST", P, {"struct_stub": True})
    call("POST", P, {"world_switch": True, "load_saved": False})
    h0 = hashes()
    t0 = time.time()
    r = call("POST", structure_route(), BUILD)
    if "async_id" in r:
        aid = r["async_id"]
        for _ in range(1200):
            s = call("GET", f"/api/async/{aid}")
            if s.get("status") == "complete" or "error" in s:
                r = s
                break
            time.sleep(0.25)
    out["build_wall_s"] = round(time.time() - t0, 2)
    out["build"] = {k: r.get(k) for k in ("success", "world", "error", "refused_at", "voxels_changed", "canvas_cells",
                                         "excavated", "cleared_tree_voxels", "verify_bad", "verify_samples", "clipped_outside_region", "undo", "lo", "hi", "floor_y_voxel", "gates")}
    h1 = hashes()
    out["region_chunks_changed"] = [k for k in h0 if h0[k] != h1[k]]
    if mode == "green" and r.get("world") == "voxfound":
        lo, hi, fy = r["lo"], r["hi"], r["floor_y_voxel"]
        cx, cz = (lo[0] + hi[0]) / 2 / 8, (lo[2] + hi[2]) / 2 / 8
        out["W3"] = call("POST", P, {"struct_check": {"lo": lo, "hi": hi, "floor": fy}})
        call("POST", "/api/entity/spawn", {"type": "animated", "id": PID, "position": {"x": cx, "y": fy / 8 + 1.0, "z": cz}})
        time.sleep(4.0)
        e = call("GET", "/api/entity/" + PID)
        out["W2"] = {"spawn_at": [cx, fy / 8 + 1.0, cz], "rest_y": (e.get("position") or {}).get("y"), "floor_m": fy / 8}
        n = 0
        while call("POST", "/api/undo", {}).get("world") == "voxfound":
            n += 1
        out["W4_undos"] = n
        out["W4_back"] = sum(hashes()[k] == h0[k] for k in h0)
    call("POST", P, {"world_switch": False})
    out["today_after"] = today_cubes()
    out["today_unchanged"] = out["today_after"] == out["today_before"]
    if mode == "red":
        call("POST", P, {"struct_stub": False})
    print(json.dumps(out)[:5000])
    with open(os.path.join(EVID, "voxfound_f711_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
