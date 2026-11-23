"""F7.21b live (docs/evidence/voxfound_f7_prediction.txt B1-B4): the 1/8 m furnish stage on the sink path.
Project VoxFoundBoot (switched). Modes: bare (the control / red: no "furnish") / furnish. Each starts from the same
saved world (nothing is saved). The engine's StructureForge hall_house (POST /api/structure/build); every artifact:
WorldGenerator Perlin s7 + flora + StructureForge's 1/8 m shell + (furnish) the 1/8 m furniture library."""
import json, os, shutil, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
HOUSE = {"schema": "v2", "type": "house", "typology": "hall_house", "footprint": [16, 7], "stories": 1,
         "position": {"x": 8150, "y": 28, "z": 200}}


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


def total(c):
    return sum(v for k, v in c.items() if k != "air")


def shot(name, pos, yaw, pitch):
    call("POST", "/api/camera", {"mode": "free", "position": pos, "yaw": yaw, "pitch": pitch})
    time.sleep(2.5)
    r = call("GET", "/api/screenshot")
    src = os.path.join(REPO, r.get("path", "")) if r.get("path") else None
    if src and os.path.exists(src):
        dst = os.path.join(EVID, name)
        shutil.copyfile(src, dst)
        return os.path.relpath(dst, REPO)
    return r


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    c0 = census()
    body = dict(HOUSE)
    if mode == "furnish":
        body["furnish"] = True
    r = call("POST", "/api/structure/build", body)
    c1 = census()
    out["build"] = {k: r.get(k) for k in ("success", "world", "error", "verify_bad", "clipped", "floor_y_voxel", "lo", "hi",
                                           "skipped")}
    f = r.get("furnish_grid8")
    if f:
        out["furnish_grid8"] = {k: f.get(k) for k in ("count", "voxels", "refused_cells", "no_grid8_template", "unplaced", "builtins",
                                                      "not_in_this_stage")}
        out["pieces"] = [(p["type"], p["room"], p["rotation"], p["voxels"], p["refused_cells"], p["cell"]) for p in f.get("pieces", [])]
    out["census_delta_total"] = total(c1) - total(c0)
    out["census_delta"] = {k: c1.get(k, 0) - c0.get(k, 0) for k in set(c0) | set(c1) if c1.get(k, 0) != c0.get(k, 0)}
    if mode == "furnish" and r.get("lo"):
        lo, hi, fy = r["lo"], r["hi"], r["floor_y_voxel"]
        eye = fy / 8 + 1.55
        cz = (lo[2] + hi[2]) / 16 + 0.5
        # PHOTOGRAPHY AID ONLY (hand-placed, not generator output): 3 point lights along the room's long axis -- the
        # 1/8 m furnish stage places no lights yet, so the closed interior renders black without them
        out["photo_lights"] = []
        for fx in (0.25, 0.5, 0.75):
            x = (lo[0] + (hi[0] - lo[0]) * fx) / 8
            out["photo_lights"].append(call("POST", "/api/light/point/add",
                                            {"x": x, "y": eye + 0.6, "z": cz, "intensity": 6.0, "radius": 9.0}).get("id"))
        out["shots"] = [shot("f721b_interior_east.png", {"x": lo[0] / 8 + 1.2, "y": eye, "z": cz}, 0, -12),
                        shot("f721b_interior_west.png", {"x": hi[0] / 8 - 1.2, "y": eye, "z": cz}, 180, -12)]
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f721b_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
