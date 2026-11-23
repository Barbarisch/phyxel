"""D3a live (docs/evidence/voxfound_f7_prediction.txt D3a T3): legacy C/S/M templates spawned on the switched world.
VoxFoundGate8 (8120, booted switched). Usage: voxfound_d3a_live.py <red|green>. Every artifact: the template file
(resources/templates, today's C/S/M) converted by VoxFound::loadCsmTemplate and painted by the switched spawn_template."""
import json, os, sys, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
NAMES = ["burgomaster", "door_plank", "tree_oak", "barony_fountain"]


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def main():
    mode = sys.argv[1]
    out = {"mode": mode, "stub": call("POST", P, {"legacy_template_stub": mode == "red"})}
    out["spawns"] = []
    for i, n in enumerate(NAMES):
        r = call("POST", "/api/world/template", {"name": n, "position": {"x": 8120.0 + 22.0 * i, "y": 40.0, "z": 232.0}})
        out["spawns"].append({"name": n, **{k: r.get(k) for k in ("success", "world", "error", "voxels_changed",
                                                                    "verify_bad", "cells", "clipped_outside_region", "lo", "hi", "template", "converted")}})
    call("POST", P, {"legacy_template_stub": False})
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_d3a_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
