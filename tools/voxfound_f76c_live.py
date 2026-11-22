"""F7.6c live (docs/evidence/voxfound_f7_prediction.txt A1-A4): the voxel API on the switched world. VoxFoundGate (8120).
  control / red : switch on, place_voxel Stone in air at (8132, 30, 68) -> region hashes, GET /api/world/voxel there.
                  (red: api_stub true first)
  green         : A1-A4 as stated. The region built with load_saved:false (the generator's). Every artifact:
                  WorldGenerator Perlin s7 + flora (the 1/8 m region), edited through the engine's voxel HTTP API."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
X, Y, Z = 8132, 30, 68   # open ground (F7.5's hit column, top cube 27): y 30 is air


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def hashes():
    return {",".join(map(str, c["chunk"])): c["hash"] for c in call("POST", P, {"store_hash": True})["chunks"]}


def q(x, y, z):
    return call("GET", f"/api/world/voxel?x={x}&y={y}&z={z}")


def top(x, z):
    return call("POST", P, {"column_top": [[x, z]]})["column_top"][0]


def wait_async(r):   # fill_region / clear_region answer {"async_id"}; poll the job
    aid = r.get("async_id")
    if not aid:
        return r
    for _ in range(600):
        s = call("GET", f"/api/async/{aid}")
        if s.get("status") == "complete" or "error" in s:
            return s
        time.sleep(0.1)
    return {"timeout": aid}


def solid_in(lo, hi):
    n = 0
    for x in range(lo[0], hi[0] + 1):
        for y in range(lo[1], hi[1] + 1):
            for z in range(lo[2], hi[2] + 1):
                n += (q(x, y, z).get("voxfound") or {}).get("solid_voxels", 0)
    return n


def box(lo, hi, **kw):
    b = {"x1": lo[0], "y1": lo[1], "z1": lo[2], "x2": hi[0], "y2": hi[1], "z2": hi[2]}
    b.update(kw)
    return b


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    out["region"] = {k: v for k, v in call("POST", P, REGION).items() if k in ("faces", "chunks")}
    call("POST", P, {"world_switch": False})
    out["today_query_before"] = q(X, Y, Z)
    if mode == "red":
        out["stub"] = call("POST", P, {"api_stub": True})
    out["switch"] = call("POST", P, {"world_switch": True, "load_saved": False})
    h0 = hashes()
    if mode in ("control", "red"):
        out["place"] = call("POST", "/api/world/voxel", {"x": X, "y": Y, "z": Z, "material": "Stone"})
        out["region_changed_chunks"] = [k for k, v in hashes().items() if v != h0[k]]
        out["query_switched"] = q(X, Y, Z)
        call("POST", P, {"world_switch": False})
        out["today_query_after"] = q(X, Y, Z)
        if mode == "red":
            call("POST", P, {"api_stub": False})
    else:
        a = {}
        a["top_before"] = top(X, Z)
        a["place"] = call("POST", "/api/world/voxel", {"x": X, "y": 28, "z": Z, "material": "Stone"})
        a["top_after_place"] = top(X, Z)
        a["query_after_place"] = q(X, 28, Z)
        a["remove"] = call("POST", "/api/world/voxel/remove", {"x": X, "y": 28, "z": Z})
        a["query_after_remove"] = q(X, 28, Z)
        out["A1"] = a
        b = {}
        lo, hi = [8130, 30, 64], [8133, 32, 67]
        b["solid_before"] = solid_in(lo, hi)
        b["fill"] = wait_async(call("POST", "/api/world/fill", box(lo, hi, material="Stone")))
        b["solid_after_fill"] = solid_in(lo, hi)
        b["clear"] = wait_async(call("POST", "/api/world/clear", box(lo, hi)))
        b["solid_after_clear"] = solid_in(lo, hi)
        b["hollow"] = wait_async(call("POST", "/api/world/fill", box(lo, hi, material="Stone", hollow=True, replace=True)))
        b["solid_after_hollow"] = solid_in(lo, hi)
        b["clear2"] = wait_async(call("POST", "/api/world/clear", box(lo, hi)))
        lo2, hi2 = [8130, 25, 64], [8133, 27, 67]   # cuts the terrain (surface ~27)
        b["terrain_solid_before"] = solid_in(lo2, hi2)
        b["fill_no_replace"] = wait_async(call("POST", "/api/world/fill", box(lo2, hi2, material="Glass")))
        b["terrain_solid_after"] = solid_in(lo2, hi2)
        out["A2"] = b
        c = {}
        vx, vy, vz = X * 8 + 3, 31 * 8 + 2, Z * 8 + 5
        c["size1"] = call("POST", "/api/world/voxel", {"x": vx, "y": vy, "z": vz, "units": "voxel", "size": 1,
                                                        "material": "Gold"})
        c["size2"] = call("POST", "/api/world/voxel", {"x": vx + 8, "y": vy, "z": vz, "units": "voxel", "size": 2,
                                                        "material": "Gold"})
        c["subcube"] = call("POST", "/api/world/subcube", {"x": X, "y": 33, "z": Z, "sx": 2, "sy": 0, "sz": 1,
                                                            "material": "Bricks"})
        c["subcube_query"] = q(X, 33, Z)
        out["A3"] = c
        st = call("GET", "/api/undo/status").get("voxfound")
        out["A4_depth"] = st
        n = 0
        while True:
            r = call("POST", "/api/undo", {})
            if r.get("world") != "voxfound":
                break
            n += 1
        out["A4_undos"] = n
        h1 = hashes()
        out["A4_back_to_start"] = sum(h1[k] == h0[k] for k in h0)
        call("POST", P, {"world_switch": False})
        out["today_query_after"] = q(X, Y, Z)
        out["today_untouched"] = out["today_query_after"] == out["today_query_before"]
    print(json.dumps(out)[:5000])
    with open(os.path.join(EVID, "voxfound_f76c_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
