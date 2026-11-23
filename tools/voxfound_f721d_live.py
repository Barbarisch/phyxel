"""F7.21d live (docs/evidence/voxfound_f7_prediction.txt S2-S4): item props on the 1/8 m furnished surfaces.
Project VoxFoundBoot (switched). Modes: red (no furnish) / green (A furnished, then B furnished over A, saved) / reload.
Every artifact: StructureForge hall_house + the 1/8 m furniture library + FurniturePlacer's surface items
(ItemPropManager props), on WorldGenerator Perlin s7 + flora."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
OUT = os.path.join(EVID, "voxfound_f721d_live.jsonl")


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def item_props():
    r = call("GET", "/api/placed_objects")
    objs = r if isinstance(r, list) else (r.get("objects") or r.get("placed_objects") or [])
    return [o for o in objs if (o.get("category") == "item")]


def solid(x, y, z):
    return call("POST", P, {"pick": {"origin": [x, y, z], "dir": [0, -1, 0], "max_m": 0.01, "size": 8}}).get("voxel") is not None


def build(x, z, furnish):
    b = {"schema": "v2", "type": "house", "typology": "hall_house", "footprint": [16, 7], "stories": 1,
         "position": {"x": x, "y": 28, "z": z}}
    if furnish:
        b["furnish"] = True
    return call("POST", "/api/structure/build", b)


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    n0 = len(item_props())
    out["props_before"] = n0
    if mode == "reload":
        reg = call("POST", P, {"struct_registry": True})
        ids = [p for s in reg.get("structures", []) for p in s.get("props", [])]
        have = {o.get("id") for o in item_props()}
        prev = [json.loads(l) for l in open(OUT) if l.strip()]
        saved = [p for p in prev if p["mode"] == "green"][-1]
        out["S4"] = {"registry_props": len(ids), "present": sum(1 for i in ids if i in have),
                     "saved_B_props": saved["B"]["props"], "engine_item_props": n0}
    else:
        a = build(8150, 200, mode == "green")
        f = a.get("furnish_grid8") or {}
        props = f.get("props", [])
        out["A"] = {"success": a.get("success"), "props": len(props), "surfaces": [s["type"] for s in f.get("surfaces", [])],
                    "engine_item_props": len(item_props()) - n0}
        rest = []
        for p in props:
            x, y, z = p["pos"]
            below, above = solid(x, y - 1 / 16, z), solid(x, y + 1 / 16, z)
            rest.append({"item": p["item"], "on": p["on"], "y": round(y, 4), "below_solid": below, "above_air": not above})
        out["S2_rest"] = rest
        out["S2_all_rest"] = all(r["below_solid"] and r["above_air"] for r in rest) if rest else None
        if mode == "green":
            b = build(8154, 203, True)
            fb = b.get("furnish_grid8") or {}
            out["B"] = {"success": b.get("success"), "props": len(fb.get("props", [])),
                        "overlaps_cleared": b.get("overlaps_cleared")}
            out["S3_engine_item_props"] = len(item_props())
            out["S3_expected"] = n0 + len(fb.get("props", []))
            out["save"] = (call("POST", "/api/world/save", {}).get("voxfound") or {}).get("saved_chunks")
    print(json.dumps(out))
    with open(OUT, "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
