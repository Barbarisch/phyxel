"""F7.21e live (docs/evidence/voxfound_f7_prediction.txt E1-E3): sit / interact on the 1/8 m furnished structures.
Project VoxFoundBoot (switched). Modes: red (fixture_stub) / green. Every artifact: StructureForge hall_house + the
1/8 m furniture library + PlacedObjectManager fixture records (the templates' interaction definitions), on
WorldGenerator Perlin s7 + flora."""
import json, os, sys, time, urllib.request

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


def build(x, z, stub):
    return call("POST", "/api/structure/build", {"schema": "v2", "type": "house", "typology": "hall_house",
                                                 "footprint": [16, 7], "stories": 1, "furnish": True,
                                                 "fixture_stub": stub, "position": {"x": x, "y": 28, "z": z}})


def placed_ids():
    r = call("GET", "/api/placed_objects")
    objs = r if isinstance(r, list) else (r.get("objects") or r.get("placed_objects") or [])
    return {o.get("id") for o in objs}


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    a = build(8150, 200, mode == "red")
    f = a.get("furnish_grid8") or {}
    o, fy = a.get("origin"), a.get("floor_y_voxel")
    out["E1"] = {"seat_points": f.get("seat_points"), "fixtures": len(f.get("fixtures", [])),
                 "by_type": {}}
    for fx in f.get("fixtures", []):
        out["E1"]["by_type"][fx["type"]] = out["E1"]["by_type"].get(fx["type"], 0) + len(fx["seats"])
    if mode == "green":
        far = []
        for fx in f["fixtures"]:
            lo = [(o[i] + fx["cell"][i]) / 8 for i in range(3)]
            hi = [lo[i] + fx["ext"][i] / 8 for i in range(3)]
            for s in fx["seats"]:
                dx = max(lo[0] - s[0], 0, s[0] - hi[0])
                dz = max(lo[2] - s[2], 0, s[2] - hi[2])
                far.append({"type": fx["type"], "horiz_out_m": round((dx * dx + dz * dz) ** 0.5, 3),
                            "above_floor_m": round(s[1] - fy / 8, 3)})
        out["E2"] = far
        out["E2_max_out_m"] = max((x["horiz_out_m"] for x in far), default=None)
        out["E2_min_above_floor_m"] = min((x["above_floor_m"] for x in far), default=None)
        ids = [fx["id"] for fx in f["fixtures"]]
        b = build(8154, 203, False)
        have = placed_ids()
        out["E3"] = {"overlaps_cleared": b.get("overlaps_cleared"), "first_ids_remaining": sum(1 for i in ids if i in have),
                     "second_fixtures": len((b.get("furnish_grid8") or {}).get("fixtures", []))}
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f721e_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
