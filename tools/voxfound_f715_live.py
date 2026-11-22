"""F7.15 live (docs/evidence/voxfound_f7_prediction.txt Z1-Z4): furnished buildings on the switched world. VoxFoundGate
(8120); the gate region (flora_window open), switched (load_saved false). The building: the ENGINE's StructureForge
(POST /api/structure/build, hall_house [16, 7], 1 story); the reference = the same request on today's world (switch off).
Modes: control (F7.14 editor) / red (no furnish) / green (furnish: true)."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False, "flora_window": "open",
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
BUILD = {"schema": "v2", "type": "house", "typology": "hall_house", "footprint": [16, 7], "stories": 1,
         "position": {"x": 8130, "y": 28, "z": 66}}
SAMPLES = [(x, y, z) for x in range(8128, 8150, 3) for y in range(24, 36, 2) for z in range(62, 78, 3)]


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


def fixtures(r):
    return sorted((f.get("type"), f.get("room")) for f in r.get("fixtures", []) or [])


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    call("POST", P, REGION)
    ref = call("POST", "/api/structure/build", BUILD)   # today's world (switch off): the reference plan
    out["ref"] = {"success": ref.get("success"), "fixtures": len(ref.get("fixtures", []) or [])}
    time.sleep(5.0)   # the reference build's late work settles before the baseline (run 0 read 17 changes without it)
    t0 = today()
    call("POST", P, {"world_switch": True, "load_saved": False})
    req = dict(BUILD)
    if mode == "green":
        req["furnish"] = True
    if mode == "control":
        req["furnish"] = True
    r = call("POST", "/api/structure/build", req)
    out["build"] = {"success": r.get("success"), "world": r.get("world"), "error": r.get("error"),
                    "fixtures": len(r.get("fixtures", []) or []), "skipped": r.get("skipped"),
                    "floor_top_micro": r.get("floor_top_micro"), "lights": r.get("lights_registered")}
    out["Z1_same_plan"] = fixtures(r) == fixtures(ref)
    if r.get("fixtures"):
        have = []
        for f in r["fixtures"]:
            p = f.get("position", {})
            s = (call("GET", f"/api/world/voxel?x={p.get('x')}&y={p.get('y')}&z={p.get('z')}").get("voxfound") or {}).get("solid_voxels", 0)
            have.append(s > 0)
        out["Z3_fixtures_with_voxels"] = [sum(have), len(have)]
        fm = r.get("floor_top_micro")
        if fm is not None:
            z4 = []
            for i, (cx, cz) in enumerate([(8133.5, 68.5), (8138.5, 69.5), (8141.5, 68.5), (8144.5, 70.5)]):
                pid = f"f715probe{i}"
                call("POST", "/api/entity/spawn", {"type": "animated", "id": pid, "position": {"x": cx, "y": fm / 9 + 1.0, "z": cz}})
                time.sleep(3.0)
                e = call("GET", "/api/entity/" + pid)
                ry = (e.get("position") or {}).get("y")
                pick = call("POST", P, {"pick": {"origin": [cx, (ry or 0) + 0.5, cz], "dir": [0, -1, 0], "max_m": 4, "size": 8}}).get("voxel")
                z4.append({"at": [cx, cz], "rest_y": ry, "region_surface_below": (pick[1] + 1) / 8 if pick else None})
                call("POST", "/api/entity/remove", {"id": pid})
            out["Z4"] = {"floor_m": fm / 9, "drops": z4}
    call("POST", P, {"world_switch": False})
    t1 = today()
    out["Z2_today_changed"] = sum(a != b for a, b in zip(t0, t1))
    out["Z2_changed_at"] = [[SAMPLES[i], t0[i], t1[i]] for i in range(len(t0)) if t0[i] != t1[i]]
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f715_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
