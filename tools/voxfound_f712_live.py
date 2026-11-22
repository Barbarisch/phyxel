"""F7.12 live (docs/evidence/voxfound_f7_prediction.txt V1-V3): settlement buildings on the switched world. VoxFoundGate
(port 8120); the gate region with flora_window open, switched with load_saved false. The settlement: the ENGINE's
SettlementBuildService (POST /api/settlement/build, 2 x 2 plots, async job) -- never hand-placed. Modes: control / red
(struct_stub) / green. Every artifact: WorldGenerator Perlin s7 + flora (the region) + the settlement generator."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False, "flora_window": "open",
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
SETTLE = {"position": {"x": 8140, "y": 28, "z": 80}, "width": 52, "depth": 36, "cols": 2, "rows": 2,
          "typology": "hall_house", "residents": False}


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


def wait_job(jid):
    for _ in range(600):
        j = call("GET", "/api/jobs")
        jobs = j.get("jobs", []) if isinstance(j, dict) else j
        for x in jobs:
            if x.get("id") == jid and x.get("state") in ("complete", "failed", "cancelled"):
                return x
        time.sleep(0.5)
    return {"timeout": jid}


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    call("POST", P, REGION)
    if mode == "red":
        out["stub"] = call("POST", P, {"struct_stub": True})
    call("POST", P, {"world_switch": True, "load_saved": False})
    h0 = hashes()
    d0 = call("GET", "/api/undo/status").get("voxfound", {}).get("undo_depth")
    t0 = time.time()
    r = call("POST", "/api/settlement/build", SETTLE)
    out["new_store"] = r.get("new_store")
    out["plan"] = {"buildings": r.get("settlement", {}).get("buildings"), "streets": r.get("settlement", {}).get("streets"),
                   "queued": [(q["typology"], q["footprint"], q["position"]) for q in r.get("queued_builds", [])]}
    job = wait_job(r.get("job_id"))
    out["job"] = {k: job.get(k) for k in ("state", "message", "units_done", "units_total", "elapsed_seconds")}
    out["wall_s"] = round(time.time() - t0, 1)
    h1 = hashes()
    out["region_chunks_changed"] = [k for k in h0 if h0[k] != h1[k]]
    out["undo_entries_added"] = call("GET", "/api/undo/status").get("voxfound", {}).get("undo_depth", 0) - (d0 or 0)
    if mode == "green":
        log = call("POST", P, {"struct_log": True}).get("placements", [])
        out["V1"] = [{k: p.get(k) for k in ("success", "canvas_cells", "verify_bad", "clipped_outside_region", "excavated",
                                             "cleared_tree_voxels", "floor_y_voxel", "lo", "hi")} for p in log]
        v2 = []
        for i, p in enumerate(log):
            lo, hi, fy = p["lo"], p["hi"], p["floor_y_voxel"]
            cx, cz = (lo[0] + hi[0]) / 16, (lo[2] + hi[2]) / 16
            pid = f"f712probe{i}"
            call("POST", "/api/entity/spawn", {"type": "animated", "id": pid, "position": {"x": cx, "y": fy / 8 + 1.0, "z": cz}})
            time.sleep(3.0)
            e = call("GET", "/api/entity/" + pid)
            v2.append({"house": i, "floor_m": fy / 8, "rest_y": (e.get("position") or {}).get("y")})
            call("POST", "/api/entity/remove", {"id": pid})
        out["V2"] = v2
        n = 0
        while call("POST", "/api/undo", {}).get("world") == "voxfound":
            n += 1
        out["V3_undos"] = n
        out["V3_back"] = sum(hashes()[k] == h0[k] for k in h0)
    call("POST", P, {"world_switch": False})
    if mode == "red":
        call("POST", P, {"struct_stub": False})
    print(json.dumps(out)[:5000])
    with open(os.path.join(EVID, "voxfound_f712_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
