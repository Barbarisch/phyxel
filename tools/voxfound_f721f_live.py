"""F7.21f live (docs/evidence/voxfound_f7_prediction.txt S1-S4): trade signage on the 1/8 m furnished structures.
Project VoxFoundGate8 (switched, today's streaming off from the boot). Every artifact: StructureForge tavern / hall_house
(furnish:true, the 1/8 m library) + planSignMount's bracket + the typology's sign item, on WorldGenerator Perlin s7."""
import json, os, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def census():
    return call("POST", P, {"census": True}).get("census", {})


def main():
    out = {}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    c0 = census()
    t = call("POST", "/api/structure/build", {"schema": "v2", "type": "tavern", "typology": "tavern", "footprint": [14, 7],
                                              "stories": 1, "furnish": True, "position": {"x": 8190, "y": 28, "z": 230}})
    c1 = census()
    f = t.get("furnish_grid8") or {}
    sg = f.get("sign", {})
    out["tavern"] = {"success": t.get("success"), "error": t.get("error"), "pieces": f.get("count"), "sign": sg,
                     "floor_m": (t.get("floor_y_voxel") or 0) / 8}
    out["S1"] = sg.get("status") == "hung" and (sg.get("board_bottom_above_floor_m") or 0) >= 2.44
    if sg.get("world_pos"):
        x, y, z = sg["world_pos"]
        out["S2_board_centre_air"] = call("POST", P, {"pick": {"origin": [x, y, z], "dir": [0, -1, 0], "max_m": 0.01, "size": 8}}).get("voxel") is None
    out["S3_metal_delta"] = c1.get("Metal", 0) - c0.get("Metal", 0)
    out["S3_bracket_voxels"] = sg.get("bracket_voxels")
    h = call("POST", "/api/structure/build", {"schema": "v2", "type": "house", "typology": "hall_house", "footprint": [16, 7],
                                              "stories": 1, "furnish": True, "position": {"x": 8150, "y": 28, "z": 260}})
    out["S4_hall_house_sign"] = (h.get("furnish_grid8") or {}).get("sign")
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f721f_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
