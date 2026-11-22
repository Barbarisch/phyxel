"""F7.13 live (docs/evidence/voxfound_f7_prediction.txt X2-X4): the old-grid writers on the switched world. VoxFoundGate
(port 8120); the gate region (flora_window open), switched with load_saved false; F7.12's village (the ENGINE's
SettlementBuildService, POST /api/settlement/build). Modes: control / red (writer_stub) / green. Every artifact:
WorldGenerator Perlin s7 + flora + the settlement generator."""
import json, os, sys, time, urllib.request, shutil

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False, "flora_window": "open",
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
SETTLE = {"position": {"x": 8140, "y": 28, "z": 80}, "width": 52, "depth": 36, "cols": 2, "rows": 2,
          "typology": "hall_house", "residents": False}
STREET = (8166.5, 98.5)   # between the plot columns and rows
SAMPLES = [(x, y, z) for x in range(8140, 8194, 6) for y in range(24, 33, 2) for z in range(80, 118, 6)]


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


def today():
    return [call("GET", f"/api/world/voxel?x={x}&y={y}&z={z}").get("exists") for (x, y, z) in SAMPLES]


def wait_job(jid):
    for _ in range(600):
        j = call("GET", "/api/jobs")
        for x in (j.get("jobs", []) if isinstance(j, dict) else j):
            if x.get("id") == jid and x.get("state") in ("complete", "failed", "cancelled"):
                return x
        time.sleep(0.5)


def main():
    mode = sys.argv[1]
    out = {"mode": mode, "samples": len(SAMPLES)}
    call("POST", P, REGION)
    t0 = today()
    if mode == "red":
        out["stub"] = call("POST", P, {"writer_stub": True})
    call("POST", P, {"world_switch": True, "load_saved": False})
    h0 = hashes()
    r = call("POST", "/api/settlement/build", SETTLE)
    job = wait_job(r.get("job_id"))
    out["job"] = {k: job.get(k) for k in ("state", "elapsed_seconds")}
    out["region_chunks_changed"] = sum(hashes()[k] != h0[k] for k in h0)
    if mode == "green":
        out["writer_log"] = call("POST", P, {"writer_log": True}).get("by_label")
        x, z = STREET
        pick = call("POST", P, {"pick": {"origin": [x, 60.0, z], "dir": [0, -1, 0], "max_m": 60, "size": 8}}).get("voxel")
        call("POST", "/api/entity/spawn", {"type": "animated", "id": "f713probe", "position": {"x": x, "y": 34.0, "z": z}})
        time.sleep(4.0)
        e = call("GET", "/api/entity/f713probe")
        out["X3"] = {"street_top_m": (pick[1] + 1) / 8 if pick else None, "rest_y": (e.get("position") or {}).get("y")}
        call("POST", "/api/camera", {"mode": "free", "position": {"x": 8166.0, "y": 95.0, "z": 98.0}, "yaw": 90, "pitch": -89})
        time.sleep(3)
        shutil.copy(call("GET", "/api/screenshot")["path"], os.path.join(EVID, "voxfound_f713_village_top.png"))
        call("POST", "/api/entity/remove", {"id": "f713probe"})
        n = 0
        while call("POST", "/api/undo", {}).get("world") == "voxfound":
            n += 1
        out["X4_undos"] = n
        out["X4_back"] = sum(hashes()[k] == h0[k] for k in h0)
    call("POST", P, {"world_switch": False})
    t1 = today()
    out["X2_today_changed"] = sum(a != b for a, b in zip(t0, t1))
    if mode == "red":
        call("POST", P, {"writer_stub": False})
    print(json.dumps(out)[:4000])
    with open(os.path.join(EVID, "voxfound_f713_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
