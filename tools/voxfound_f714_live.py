"""F7.14 live (docs/evidence/voxfound_f7_prediction.txt Y1-Y3): tree-matter reads on the switched world. VoxFoundGate
(8120); the gate region (flora_window open), switched (load_saved false); a floating Leaf cube placed through the voxel
API at (8166, 45, 98), then F7.12's village (the ENGINE's SettlementBuildService). Modes: control / red (tree_stub) /
green. The canopy unit's frame is read from phyxel.log (the STUTTER line after "orphaned canopy")."""
import json, os, re, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False, "flora_window": "open",
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
SETTLE = {"position": {"x": 8140, "y": 28, "z": 80}, "width": 52, "depth": 36, "cols": 2, "rows": 2,
          "typology": "hall_house", "residents": False}
LEAF = (8134, 50, 100)   # in the canopy sweep's bounds (x 8132..8200), outside every plot's clearing (x >= 8142); earlier spots were cleared by the parcel clearing
SAMPLES = [(x, y, z) for x in range(8140, 8194, 6) for y in range(24, 33, 2) for z in range(80, 118, 6)]


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def today():
    return [call("GET", f"/api/world/voxel?x={x}&y={y}&z={z}").get("exists") for (x, y, z) in SAMPLES]


def wait_job(jid):
    for _ in range(600):
        j = call("GET", "/api/jobs")
        for x in (j.get("jobs", []) if isinstance(j, dict) else j):
            if x.get("id") == jid and x.get("state") in ("complete", "failed", "cancelled"):
                return x
        time.sleep(0.5)


def canopy_frame_ms():
    lines = open(os.path.join(REPO, "phyxel.log"), encoding="utf-8", errors="replace").read().splitlines()
    idx = max(i for i, l in enumerate(lines) if "orphaned canopy:" in l)
    for l in lines[idx:idx + 6]:
        m = re.search(r"Frame Time: ([0-9.]+)ms", l)
        if m:
            return float(m.group(1)), lines[idx][:120]
    return None, lines[idx][:120]


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    call("POST", P, REGION)
    t0 = today()
    if mode == "red":
        out["stub"] = call("POST", P, {"tree_stub": True})
    call("POST", P, {"world_switch": True, "load_saved": False})
    out["leaf"] = call("POST", "/api/world/voxel", {"x": LEAF[0], "y": LEAF[1], "z": LEAF[2], "material": "Leaf"}).get("voxels_changed")
    out["leaf_before"] = (call("GET", f"/api/world/voxel?x={LEAF[0]}&y={LEAF[1]}&z={LEAF[2]}").get("voxfound") or {}).get("solid_voxels")
    r = call("POST", "/api/settlement/build", SETTLE)
    job = wait_job(r.get("job_id"))
    out["job"] = {k: job.get(k) for k in ("state", "elapsed_seconds")}
    out["canopy_cleared"] = (job.get("result") or {}).get("paths", {}).get("orphaned_canopy_cleared")
    out["Y1_leaf_after"] = (call("GET", f"/api/world/voxel?x={LEAF[0]}&y={LEAF[1]}&z={LEAF[2]}").get("voxfound") or {}).get("solid_voxels")
    out["Y2_canopy_frame_ms"], out["canopy_line"] = canopy_frame_ms()
    call("POST", P, {"world_switch": False})
    out["Y3_today_changed"] = sum(a != b for a, b in zip(t0, today()))
    if mode == "red":
        call("POST", P, {"tree_stub": False})
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f714_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
