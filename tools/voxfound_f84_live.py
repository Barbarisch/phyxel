"""F8.4 live (docs/evidence/voxfound_f8_prediction.txt O1-O3): overlapping structures on the switched world
(VoxFoundBoot). Modes: control / red (overlap_stub) / green (then: reload -- after a relaunch, the registry back).
Two hall houses by the ENGINE's StructureForge (POST /api/structure/build); A at (8150, 200), B at (8154, 203).
Every artifact: WorldGenerator Perlin s7 + flora + StructureForge's 1/8 m shells."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
HOUSE = {"schema": "v2", "type": "house", "typology": "hall_house", "footprint": [16, 7], "stories": 1}
A, B = (8150, 200), (8154, 203)
PROBE = (8151.5, 201.5)   # inside A, outside B


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def runs(x, z):
    out, prev = [], False
    for vy in range(60 * 8, 10 * 8, -1):
        s = call("POST", P, {"pick": {"origin": [x, (vy + 0.5) / 8, z], "dir": [0, -1, 0], "max_m": 0.06, "size": 8}}).get("voxel")
        if s and not prev:
            out.append((vy + 1) / 8)
        prev = bool(s)
    return out


def build(xz):
    b = dict(HOUSE)
    b["position"] = {"x": xz[0], "y": 28, "z": xz[1]}
    r = call("POST", "/api/structure/build", b)
    return {k: r.get(k) for k in ("success", "world", "error", "overlaps_cleared", "floor_y_voxel", "lo", "hi")}


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    if mode == "reload":
        out["O3_registry"] = call("POST", P, {"struct_registry": True})
    else:
        if mode == "red":
            out["stub"] = call("POST", P, {"overlap_stub": True})
        out["before_runs"] = runs(*PROBE)
        out["A"] = build(A)
        out["after_A_runs"] = runs(*PROBE)
        out["B"] = build(B)
        r = runs(*PROBE)
        out["O1_after_B_runs"] = r
        ground = r[-1] if r else None
        out["O1_A_remains"] = any(t > ground + 1.0 for t in r) if ground is not None else None
        if mode == "green":
            out["O2_registry"] = call("POST", P, {"struct_registry": True})
            b = out["B"]
            cx, cz = (b["lo"][0] + b["hi"][0]) / 16 + 0.3, (b["lo"][2] + b["hi"][2]) / 16 + 0.3
            call("POST", "/api/entity/spawn", {"type": "animated", "id": "f84p", "position": {"x": cx, "y": b["floor_y_voxel"] / 8 + 1.0, "z": cz}})
            time.sleep(3.0)
            out["O2_drop"] = {"floor": b["floor_y_voxel"] / 8, "rest": (call("GET", "/api/entity/f84p").get("position") or {}).get("y")}
            call("POST", "/api/entity/remove", {"id": "f84p"})
            out["save"] = (call("POST", "/api/world/save", {}).get("voxfound") or {}).get("saved_chunks")
        if mode == "red":
            call("POST", P, {"overlap_stub": False})
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f84_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
