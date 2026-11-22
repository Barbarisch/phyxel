"""F7.16 live (docs/evidence/voxfound_f7_prediction.txt B1-B3): the switched world as a boot mode. The project
C:/Users/jack/Documents/PhyxelProjects/VoxFoundBoot = a copy of the gate project with world.voxfound set (its own
world.db). Modes: before (cold boot: B1, B3, then B2's edits + save_world) / after (a relaunch: B2's reload + the walk).
Edits through the engine: the preview edit API (a brush), DamageSystem (POST /api/damage/apply), StructureForge
(POST /api/structure/build). Every artifact: WorldGenerator Perlin s7 + flora + those generators."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
B = "/api/debug/voxfound_bodies"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
STATE = os.path.join(EVID, "voxfound_f716_state.json")


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def wait_boot():
    t0 = time.time()
    while time.time() - t0 < 300:
        b = call("POST", P, {"boot_status": True})
        if b.get("report"):
            return b
        time.sleep(1.0)
    return b


def hashes():
    return {",".join(map(str, c["chunk"])): c["hash_by_name"] for c in call("POST", P, {"store_hash": True})["chunks"]}


def top(x, z):
    return call("POST", P, {"column_top": [[x, z]]})["column_top"][0]


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    b = wait_boot()
    out["boot"] = b.get("report")
    s = call("POST", P, {"stream_status": True})
    cam = (b.get("report") or {}).get("camera", [0, 0, 0])
    rg = s.get("region") or {"from": [0, 0, 0], "to": [-1, -1, -1]}
    ccx, ccz = int(cam[0] // 32), int(cam[2] // 32)
    out["B1"] = {"switched": s.get("switched"), "stream": s.get("stream"), "region": rg,
                 "camera_chunk_in_region": rg["from"][0] <= ccx <= rg["to"][0] and rg["from"][2] <= ccz <= rg["to"][2],
                 "physics_overrides": call("POST", P, {"phys_probe": []}).get("overrides"),
                 "voxel_query_world": call("GET", "/api/world/voxel?x=8192&y=27&z=200").get("world")}
    if mode == "before":
        t = top(8180, 200)
        out["brush"] = call("POST", P, {"edit": {"brush": {"at": [8180 * 8, t[1] * 8, 200 * 8], "size": 8},
                                                 "material": "air"}}).get("voxels_changed")
        t2 = top(8200, 212)
        out["blast"] = call("POST", "/api/damage/apply", {"x": 8200.5, "y": t2[1] + 0.5, "z": 212.5, "radius": 2.0,
                                                          "energy": 400.0}).get("broken")
        time.sleep(6.0)
        call("POST", B, {"run": False})
        out["bake"] = (call("POST", B, {"bake_now": {"budget": 0, "keep_m": 0.0, "rest_ticks": 0}, "status": True,
                                        "pen": False}).get("rest") or {}).get("baked")
        out["build"] = {k: v for k, v in call("POST", "/api/structure/build", {"schema": "v2", "type": "house",
                        "typology": "hall_house", "footprint": [16, 7], "stories": 1,
                        "position": {"x": 8166, "y": 28, "z": 236}}).items() if k in ("success", "world", "canvas_cells", "error")}
        h = hashes()
        out["save"] = (call("POST", "/api/world/save", {}).get("voxfound") or {}).get("saved_chunks")
        json.dump({"hashes": h}, open(STATE, "w"))
    else:
        prev = json.load(open(STATE))["hashes"]
        h = hashes()
        common = [k for k in prev if k in h]
        out["B2_reload_equal"] = [sum(h[k] == prev[k] for k in common), len(common)]
        walk = []
        x0, z0 = cam[0], cam[2]
        for i in range(9):
            x = x0 + 16.0 * i
            call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": 60.0, "z": z0}, "yaw": 90, "pitch": -60})
            time.sleep(1.5)
            st = call("POST", P, {"stream_status": True})
            r = st["region"]
            walk.append(r["from"][0] <= int(x // 32) <= r["to"][0])
        out["B2_walk_in_region"] = [sum(walk), len(walk)]
    print(json.dumps(out)[:3000])
    with open(os.path.join(EVID, "voxfound_f716_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
