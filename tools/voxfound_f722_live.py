"""F7.22 live (L4 confirmation of the unit-tested N1-N3): the NPC nav graph on the switched world runs at 8 cells per
metre and paths from inside a generated, furnished hall house to the ground outside (through its generated door).
Project VoxFoundBoot. Every artifact: StructureForge hall_house (furnish:true, the 1/8 m library) on WorldGenerator
Perlin s7 + flora; the path by NPCManager's NavGraph (the default NavAgentProfile)."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "furnish"   # furnish | bare
    out = {"mode": mode}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    b = call("POST", "/api/structure/build", {"schema": "v2", "type": "house", "typology": "hall_house",
                                              "footprint": [16, 7], "stories": 1, "furnish": mode == "furnish",
                                              "position": {"x": 8150, "y": 28, "z": 200}})
    lo, hi, fy = b["lo"], b["hi"], b["floor_y_voxel"]
    cx, cz = (lo[0] + hi[0]) / 16, (lo[2] + hi[2]) / 16
    # a FREE floor point 1.5 m inside the south door (x 8154) -- the house centre lands on the hall table, which the
    # path finder snaps to its top (dropping off a table is a legal fall, climbing onto one is not): run 1's one-way
    # result was that probe artifact, not the generator
    inside = [8154.5, fy / 8, hi[2] / 8 - 1.5]
    outside = [int(cx), int(hi[2] / 8) + 5]
    out["house"] = {"lo_m": [v / 8 for v in lo], "hi_m": [v / 8 for v in hi], "floor_m": fy / 8, "furnished":
                    (b.get("furnish_grid8") or {}).get("count")}
    out["in_to_out"] = call("POST", P, {"nav_check": {"from": inside, "to": outside}}).get("nav_check")
    out["out_to_in"] = call("POST", P, {"nav_check": {"from": outside, "to": inside}}).get("nav_check")
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f722_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
