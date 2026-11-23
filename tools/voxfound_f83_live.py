"""F8.3 live (docs/evidence/voxfound_f8_prediction.txt T1-T4): a whole town on the switched world (VoxFoundBoot).
The town: the ENGINE's SettlementBuildService (POST /api/settlement/build, 4 x 3 plots), never hand-placed.
Every artifact: WorldGenerator Perlin s7 + flora + the settlement / structure generators."""
import json, os, re, time, urllib.request, shutil

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
TOWN = {"position": {"x": 8136, "y": 28, "z": 172}, "width": 104, "depth": 84, "cols": 4, "rows": 3,
        "typology": "hall_house", "residents": False}
SAMPLES = [(x, y, z) for x in range(8136, 8242, 8) for y in (26, 29, 32) for z in range(172, 258, 8)]


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


def main():
    out = {"mode": "run"}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})   # the town is built over a fixed region (no shift mid-build)
    call("POST", P, {"world_switch": False})
    t0 = today()
    call("POST", P, {"world_switch": True, "load_saved": True})
    log_before = len(open(os.path.join(REPO, "phyxel.log"), encoding="utf-8", errors="replace").read().splitlines())
    ts = time.time()
    r = call("POST", "/api/settlement/build", TOWN)
    out["plan"] = {"buildings": r.get("settlement", {}).get("buildings"), "new_store": r.get("new_store"),
                   "queued": len(r.get("queued_builds", []))}
    jid = r.get("job_id")
    job = {}
    for _ in range(1200):
        j = call("GET", "/api/jobs")
        for x in (j.get("jobs", []) if isinstance(j, dict) else j):
            if x.get("id") == jid and x.get("state") in ("complete", "failed", "cancelled"):
                job = x
        if job:
            break
        time.sleep(0.5)
    out["T4"] = {"job_s": job.get("elapsed_seconds"), "wall_s": round(time.time() - ts, 1)}
    lines = open(os.path.join(REPO, "phyxel.log"), encoding="utf-8", errors="replace").read().splitlines()[log_before:]
    frames = [float(m.group(1)) for l in lines for m in [re.search(r"STUTTER DETECTED! Frame Time: ([0-9.]+)ms", l)] if m]
    fails = [l[:160] for l in lines if "refused" in l and "Settlement" in l]
    out["T4"]["worst_frame_ms"] = max(frames) if frames else 0.0
    out["T4"]["stutter_frames"] = len(frames)
    out["lot_failures_logged"] = fails[:6]
    log = call("POST", P, {"struct_log": True}).get("placements", [])
    out["T1"] = {"placements": len(log), "verify_bad": sum(p.get("verify_bad", 0) for p in log),
                 "clipped": sum(p.get("clipped_outside_region", 0) for p in log),
                 "all_success": all(p.get("success") for p in log)}
    t2 = []
    for i, p in enumerate(log):
        lo, hi, fy = p["lo"], p["hi"], p["floor_y_voxel"]
        cx, cz = (lo[0] + hi[0]) / 16 + 0.3, (lo[2] + hi[2]) / 16 + 0.3
        pid = f"f83p{i}"
        call("POST", "/api/entity/spawn", {"type": "animated", "id": pid, "position": {"x": cx, "y": fy / 8 + 1.0, "z": cz}})
        time.sleep(2.5)
        e = call("GET", "/api/entity/" + pid)
        ry = (e.get("position") or {}).get("y")
        pk = call("POST", P, {"pick": {"origin": [cx, (ry or 0) + 0.3, cz], "dir": [0, -1, 0], "max_m": 3, "size": 8}}).get("voxel")
        surf = (pk[1] + 1) / 8 if pk else None
        t2.append({"house": i, "floor_m": fy / 8, "rest_y": ry, "surface_below": surf,
                   "on_surface": surf is not None and ry is not None and abs(ry - surf) <= 0.125})
        call("POST", "/api/entity/remove", {"id": pid})
    out["T2"] = {"on_a_surface": sum(1 for x in t2 if x["on_surface"]), "of": len(t2), "drops": t2}
    call("POST", "/api/camera", {"mode": "free", "position": {"x": 8188.0, "y": 130.0, "z": 214.0}, "yaw": 90, "pitch": -89})
    time.sleep(3)
    shutil.copy(call("GET", "/api/screenshot")["path"], os.path.join(EVID, "voxfound_f83_town_top.png"))
    call("POST", P, {"world_switch": False})
    out["T3_today_changed"] = sum(a != b for a, b in zip(t0, today()))
    print(json.dumps(out)[:4000])
    with open(os.path.join(EVID, "voxfound_f83_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
