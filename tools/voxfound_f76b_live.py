"""F7.6b live (docs/evidence/voxfound_f7_prediction.txt U1-U3): undo / redo on the switched world. VoxFoundGate (8120).
  control / red : switch on, one brush break (F7.4 edit_action), POST /api/undo -> are the region hashes back?
                  (red: undo_stub true first)
  green         : U1 (break, undo, redo), U2 (20 overlapping breaks / places, 20 undos stepwise, a 21st, redo cleared
                  by a new edit after 3 undos), U3 (undo / redo wall vs the edit's).
The region is built with load_saved:false (the generator's region, as F7.3-F7.5). Every artifact: WorldGenerator Perlin
s7 + flora (the 1/8 m region), edited through VoxelInteractionSystem (edit_action) and the preview edit API."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}
POSE = {"mode": "free", "position": {"x": 8132.5, "y": 33.0, "z": 60.5}, "yaw": 90, "pitch": -40}


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:   # undo with nothing to undo answers an error status
        return {"http_status": e.code, "body": json.loads(e.read().decode() or "{}")}


def hashes():
    return {",".join(map(str, c["chunk"])): c["hash"] for c in call("POST", P, {"store_hash": True})["chunks"]}


def brush_at(v, size, material):
    r = call("POST", P, {"edit": {"brush": {"at": v, "size": size}, "material": material}})
    return {k: r.get(k) for k in ("voxels_changed", "undo", "rebuild_ms", "assemble_ms", "lo")}


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    out["region"] = {k: v for k, v in call("POST", P, REGION).items() if k in ("faces", "chunks")}
    if mode == "red":
        out["stub"] = call("POST", P, {"undo_stub": True})
    out["switch"] = call("POST", P, {"world_switch": True, "load_saved": False})
    call("POST", "/api/camera", POSE)
    time.sleep(1.0)
    h0 = hashes()
    out["brush"] = {k: v for k, v in call("POST", P, {"edit_action": {"action": "break", "mode": "cube"}}).items()
                    if k in ("voxels_changed", "hover_voxel", "undo")}
    h1 = hashes()
    t0 = time.time()
    out["undo"] = call("POST", "/api/undo", {})
    out["undo_http_s"] = round(time.time() - t0, 3)
    h2 = hashes()
    out["U1_edit_changed_chunks"] = [k for k in h0 if h0[k] != h1[k]]
    out["U1_undo_equals_pre_edit"] = sum(h2[k] == h0[k] for k in h0)
    if mode == "green":
        t0 = time.time()
        out["redo"] = call("POST", "/api/redo", {})
        out["redo_http_s"] = round(time.time() - t0, 3)
        h3 = hashes()
        out["U1_redo_equals_edited"] = sum(h3[k] == h1[k] for k in h1)
        # U2: 25 overlapping size-8 edits cycling over 3 brush cells (8 voxels apart, so their 1-voxel neighbourhoods
        # interact) with each cell's material alternating air / Stone per visit -- every edit changes voxels (recorded);
        # 25 > the cap 20: the oldest 5 (and the U1 edit) drop. Then 20 undos, each compared with the state recorded
        # before that edit; a 21st answers nothing (the state = after edit 5); redo after a new edit = nothing.
        hv = out["brush"]["hover_voxel"] or [65059, 220, 535]
        cells = [[hv[0] - 8, hv[1] - 8, hv[2]], [hv[0], hv[1] - 8, hv[2]], [hv[0], hv[1] - 8, hv[2] + 8]]
        steps, edits = [hashes()], []
        for i in range(25):
            edits.append(brush_at(cells[i % 3], 8, "air" if (i // 3) % 2 == 0 else "Stone"))
            steps.append(hashes())
        out["U2_edits"] = edits
        out["U2_all_recorded"] = all(e["undo"] == "recorded" for e in edits)
        out["U2_depth_after_edits"] = call("GET", "/api/undo/status").get("voxfound")
        undo_ok, undo_ms = [], []
        for i in range(25, 5, -1):
            r = call("POST", "/api/undo", {})
            undo_ms.append(r.get("wall_ms"))
            undo_ok.append(hashes() == steps[i - 1])
        out["U2_undo_steps_exact"] = undo_ok
        out["U2_undo_wall_ms"] = undo_ms
        out["U2_21st_undo"] = call("POST", "/api/undo", {})
        out["U2_after_21st_equals_after_edit5"] = hashes() == steps[5]
        for i in range(3):
            call("POST", "/api/redo", {})
        out["U2_after_3_redos_equals_after_edit8"] = hashes() == steps[8]
        out["U2_new_edit"] = brush_at([hv[0], hv[1] + 8, hv[2]], 8, "Stone")
        out["U2_redo_after_new_edit"] = call("POST", "/api/redo", {})
        out["U3_edit_ms"] = [round((e.get("rebuild_ms") or 0) + (e.get("assemble_ms") or 0), 3) for e in edits]
    call("POST", P, {"world_switch": False})
    if mode == "red":
        call("POST", P, {"undo_stub": False})
    print(json.dumps(out)[:4000])
    with open(os.path.join(EVID, "voxfound_f76b_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
