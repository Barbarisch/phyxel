"""F7.21c live (docs/evidence/voxfound_f7_prediction.txt L1-L4): lights for the 1/8 m furnished structures.
Project VoxFoundBoot (switched). Modes: red (no furnish) / green (A furnished, then B furnished overlapping A, saved) /
reload (after a relaunch). Every artifact: StructureForge hall_house + the 1/8 m furniture library + the forge's
fixture / hearth lights, on WorldGenerator Perlin s7 + flora."""
import json, math, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
OUT = os.path.join(EVID, "voxfound_f721c_live.jsonl")


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def lights():
    r = call("GET", "/api/lights")
    if isinstance(r, list):
        return r
    for k in ("lights", "point_lights", "pointLights"):
        if isinstance(r.get(k), list):
            return r[k]
    return [v for v in r.values() if isinstance(v, list)][0] if r else []


def build(x, z, furnish):
    b = {"schema": "v2", "type": "house", "typology": "hall_house", "footprint": [16, 7], "stories": 1,
         "position": {"x": x, "y": 28, "z": z}}
    if furnish:
        b["furnish"] = True
    return call("POST", "/api/structure/build", b)


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    n0 = len(lights())
    out["lights_before"] = n0
    if mode == "reload":
        reg = call("POST", P, {"struct_registry": True})
        out["registry"] = [{"id": s["id"], "lights": len(s.get("lights", []))} for s in reg.get("structures", [])]
        reg_lights = [l for s in reg.get("structures", []) for l in s.get("lights", [])]
        prev = [json.loads(l) for l in open(OUT) if l.strip()]
        saved = [p for p in prev if p["mode"] == "green"][-1]["registry_lights"]
        pos_now = sorted(tuple(round(v, 4) for v in l["pos"]) for l in reg_lights)
        pos_saved = sorted(tuple(round(v, 4) for v in l["pos"]) for l in saved)
        out["L3_count"] = [len(reg_lights), len(saved)]
        out["L3_same_positions"] = pos_now == pos_saved
        out["L3_registered_ids_valid"] = all(l.get("id", -1) >= 0 for l in reg_lights)
        out["L3_engine_lights"] = n0
    else:
        a = build(8150, 200, mode == "green")
        n1 = len(lights())
        f = a.get("furnish_grid8") or {}
        out["A"] = {"success": a.get("success"), "structure_id": a.get("structure_id"), "origin": a.get("origin"),
                    "lights_reported": len(f.get("lights", [])), "emitters": [e["type"] for e in f.get("emitters", [])]}
        out["L1_engine_delta"] = n1 - n0
        if mode == "green":
            lo, hi, o = a["lo"], a["hi"], a["origin"]
            inside, far = 0, []
            for l in f["lights"]:
                p = l["pos"]
                inside += all(lo[i] / 8 <= p[i] <= (hi[i] + 1) / 8 for i in range(3))
            for pc in f["pieces"]:
                if pc["type"] != "wall_lantern":
                    continue
                base = [(o[i] + pc["cell"][i]) / 8 for i in range(3)]
                d = min(math.dist(l["pos"], [base[0] + 0.25, base[1] + 0.25, base[2] + 0.25]) for l in f["lights"])
                far.append(round(d, 3))
            out["L2"] = {"inside_box": inside, "of": len(f["lights"]), "lantern_light_dist_m": far}
            b = build(8154, 203, True)
            n2 = len(lights())
            fb = b.get("furnish_grid8") or {}
            out["B"] = {"success": b.get("success"), "overlaps_cleared": b.get("overlaps_cleared"),
                        "lights_reported": len(fb.get("lights", []))}
            out["L4_engine_lights"] = n2
            out["L4_expected"] = n0 + len(fb.get("lights", []))
            reg = call("POST", P, {"struct_registry": True})
            out["registry_lights"] = [l for s in reg.get("structures", []) for l in s.get("lights", [])]
            out["save"] = (call("POST", "/api/world/save", {}).get("voxfound") or {}).get("saved_chunks")
    print(json.dumps(out))
    with open(OUT, "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
