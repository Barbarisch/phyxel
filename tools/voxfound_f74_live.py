"""F7.4 live (docs/evidence/voxfound_f7_prediction.txt E2-E4): editing on the switched world, the VoxFoundGate project
(port 8120, the region built as F7.3's gate builds it). Modes:
  control : the switch on, today's break path (/api/debug/break_voxel, the B-key logic) at an open-ground cube -> the
            region must not change (the hidden old world does).
  red     : edit_stub (the switch leaves the editor unset): an edit action changes 0 region voxels.
  cost    : E4 without verify + the reads: break / place cube x 5 at the pose (column_top of the hit column before /
            after each, through the ENGINE's routed reads), action_ms per action.
  green   : E2-E4 -- at a stated pose looking at open ground: break cube (screenshot before / after), place cube back,
            break subcube, break micro; each with edit_verify (the incremental chunks vs a full rebuild).
Run after F7.3's gate built the region (or pass --build).
Every artifact: the engine's WorldGenerator (Perlin seed 7, flora) -- today's chunks and the 1/8 m region."""
import json, os, shutil, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
XZ = (8132, 68)   # an open-ground column (F7.3's D4 start)
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
POSE = {"mode": "free", "position": {"x": 8132.5, "y": 33.0, "z": 60.5}, "yaw": 90, "pitch": -40}   # looking +z, down
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=600) as resp:
        return json.loads(resp.read().decode())


def region_state():
    top = call("POST", P, {"column_top": [list(XZ)]})["column_top"][0]
    pk = call("POST", P, {"pick": {"origin": [XZ[0] + 0.5, 40, XZ[1] + 0.5], "dir": [0, -1, 0], "max_m": 30, "size": 8}})
    return {"top_cube": top, "pick_voxel": pk.get("voxel"), "pick_material": pk.get("material")}


def shot(name):
    shutil.copy(os.path.join(REPO, call("GET", "/api/screenshot")["path"]), os.path.join(EVID, name))


def act(action, mode):
    r = call("POST", P, {"edit_action": {"action": action, "mode": mode}})
    e = r.get("edit", {}) or {}
    return {k: r.get(k) for k in ("action", "mode", "editor_set", "hover_hit", "hover_voxel", "hover_normal",
                                  "hover_material", "voxels_changed", "action_ms")} | {
        "brush_lo": e.get("lo"), "brush_hi": e.get("hi"), "rebuild_ms": e.get("rebuild_ms"),
        "assemble_ms": e.get("assemble_ms"), "verify": e.get("verify")}


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    if "--build" in sys.argv:
        out["region"] = {k: v for k, v in call("POST", P, REGION).items() if k in ("faces", "chunks", "editable")}
    if mode == "cost":
        call("POST", P, {"edit_stub": False, "edit_verify": False})
        out["switch"] = call("POST", P, {"world_switch": True})
        call("POST", "/api/camera", POSE)
        time.sleep(1.0)
        rounds = []
        for i in range(5):
            b = act("break", "cube")
            hv = b["hover_voxel"]
            col = [hv[0] // 8, hv[2] // 8]
            after_break = call("POST", P, {"column_top": [col]})["column_top"][0]
            pl = act("place", "cube")
            after_place = call("POST", P, {"column_top": [col]})["column_top"][0]
            rounds.append({"break_ms": b["action_ms"], "break_voxels": b["voxels_changed"], "hit_cube": [hv[0] // 8, hv[1] // 8, hv[2] // 8],
                           "top_after_break": after_break[1], "place_ms": pl["action_ms"], "place_voxels": pl["voxels_changed"],
                           "top_after_place": after_place[1], "rebuild_ms": [b["rebuild_ms"], pl["rebuild_ms"]],
                           "assemble_ms": [b["assemble_ms"], pl["assemble_ms"]]})
        out["rounds"] = rounds
    if mode in ("red", "green"):
        call("POST", P, {"world_switch": False})
        call("POST", P, {"edit_stub": mode == "red", "edit_verify": True})
        out["switch"] = call("POST", P, {"world_switch": True})
        call("POST", "/api/camera", POSE)
        time.sleep(1.0)
        if mode == "red":
            out["break_cube"] = act("break", "cube")
            call("POST", P, {"edit_stub": False})
        else:
            shot("voxfound_f74_0_before.png")
            out["break_cube"] = act("break", "cube")
            time.sleep(1.0)
            shot("voxfound_f74_1_broken.png")
            out["place_cube"] = act("place", "cube")
            time.sleep(1.0)
            shot("voxfound_f74_2_placed_back.png")
            out["break_subcube"] = act("break", "subcube")
            out["break_micro"] = act("break", "micro")
        call("POST", P, {"edit_verify": False})
    if mode == "control":
        out["switch"] = call("POST", P, {"world_switch": True})
        out["before"] = region_state()
        y = out["before"]["top_cube"][1]
        out["old_path_break"] = call("POST", "/api/debug/break_voxel", {"x": XZ[0], "y": y, "z": XZ[1], "level": "cube"})
        out["after"] = region_state()
        out["region_changed"] = out["before"] != out["after"]
    print(json.dumps(out))


if __name__ == "__main__":
    main()
