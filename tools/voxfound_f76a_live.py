"""F7.6a live (docs/evidence/voxfound_f7_prediction.txt S1-S3): saving the switched world. VoxFoundGate (port 8120).
  before : (a fresh session) build the region, switch on, edit -- an F7.4 brush break at the stated pose, an F7.5 blast
           and its forced bake -- record every region chunk's store hash + the hit columns' tops, then save_world.
  after  : (after a relaunch) build the region, switch on (F7.6a: loads the saved chunks), record the same; compare.
Writes docs/evidence/voxfound_f76a_live.jsonl. The project's worlds/default.db is backed up by the caller and restored.
Every artifact: WorldGenerator Perlin s7 + flora (the 1/8 m region), edited through VoxelInteractionSystem / DamageSystem."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
B = "/api/debug/voxfound_bodies"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
POSE = {"mode": "free", "position": {"x": 8132.5, "y": 33.0, "z": 60.5}, "yaw": 90, "pitch": -40}
COLS = [[8132, 66], [8132, 68], [8133, 70], [8130, 72]]
BLAST = (8132, 68)   # open ground (F7.5's hit; the first control aimed at (8150, 120): tree canopy, it broke nothing)


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=3600) as resp:
        return json.loads(resp.read().decode())


def state():
    h = call("POST", P, {"store_hash": True})
    return {"hashes": {",".join(map(str, c["chunk"])): c["hash"] for c in h.get("chunks", [])},
            "edited": [c["chunk"] for c in h.get("chunks", []) if c.get("edited")],
            "tops": call("POST", P, {"column_top": COLS})["column_top"]}


def main():
    mode = sys.argv[1]
    tag = sys.argv[2] if len(sys.argv) > 2 else mode
    out = {"mode": mode, "tag": tag}
    out["region"] = {k: v for k, v in call("POST", P, REGION).items() if k in ("faces", "chunks")}
    t0 = time.time()
    out["switch"] = call("POST", P, {"world_switch": True})
    out["switch_wall_s"] = round(time.time() - t0, 3)
    if mode == "before":
        out["generator_state"] = state()
        call("POST", "/api/camera", POSE)
        time.sleep(1.0)
        out["brush"] = {k: v for k, v in call("POST", P, {"edit_action": {"action": "break", "mode": "cube"}}).items()
                        if k in ("voxels_changed", "hover_voxel")}
        top = call("POST", P, {"column_top": [list(BLAST)]})["column_top"][0]
        out["blast"] = call("POST", "/api/damage/apply", {"x": BLAST[0] + 0.5, "y": top[1] + 0.5, "z": BLAST[1] + 0.5,
                                                          "radius": 2.0, "energy": 400.0})
        time.sleep(8.0)
        call("POST", B, {"run": False})
        bk = call("POST", B, {"bake_now": {"budget": 0, "keep_m": 0.0, "rest_ticks": 0}, "status": True, "pen": False,
                              "world_check": True})
        out["bake"] = {"baked": bk.get("rest", {}).get("baked"), "world": bk.get("world"), "bodies": bk.get("bodies")}
        out["edited_state"] = state()
        t0 = time.time()
        out["save_world"] = call("POST", "/api/world/save", {})
        out["save_wall_s"] = round(time.time() - t0, 3)
    else:
        out["loaded_state"] = state()
    print(json.dumps(out)[:3000])
    with open(os.path.join(EVID, "voxfound_f76a_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
