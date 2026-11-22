"""F7.9c live (docs/evidence/voxfound_f7_prediction.txt H1-H3): background, incremental region shifts. VoxFoundGate
(port 8120); regions built with flora_window "open", switched with load_saved false.
  red   : stream_bg false: the camera crosses into chunk 257 -> F7.9b's synchronous shift (its frame cost).
  green : H1 an edit on chunk (257,1,3)'s +x face, a +1 background shift (camera driven), per-chunk hash_by_name +
          faces + sprigs vs a fresh build of the new box with the same edit; H2 slices; H3 the 256 m walk.
Every artifact: WorldGenerator Perlin s7 + flora (open window), edits through the engine's voxel API."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
BASE = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False, "flora_window": "open"}
BOX0 = {"from": [254, 0, 2], "to": [257, 1, 5]}
BOX1 = {"from": [255, 0, 2], "to": [258, 1, 5]}
EDIT = {"x": 8255, "y": 40, "z": 100, "material": "Stone"}   # chunk (257,1,3), its +x face (cube x 8255 = last of 257)


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=3600) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def chunks():
    return {",".join(map(str, c["chunk"])): (c["hash_by_name"], c["faces"], c["sprigs"])
            for c in call("POST", P, {"store_hash": True})["chunks"]}


def build(box):
    b = dict(BASE)
    b["region"] = box
    return call("POST", P, b)


def cam(x, z):
    return call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": 60.0, "z": z}, "yaw": 90, "pitch": -60})


def status():
    return call("POST", P, {"stream_status": True})


def wait_shift(n0, timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        s = status()
        if s["shifts"] > n0 and not s["job_pending"]:
            return s
        time.sleep(0.2)
    return status()


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    if mode == "green":   # the reference first: a fresh build of BOX1 with the edit
        build(BOX1)
        call("POST", P, {"world_switch": True, "load_saved": False})
        out["ref_edit"] = call("POST", "/api/world/voxel", EDIT).get("voxels_changed")
        ref = chunks()
        call("POST", P, {"world_switch": False})
    build(BOX0)
    call("POST", P, {"world_switch": True, "load_saved": False})
    cam(8176.0, 112.0)
    out["edit"] = call("POST", "/api/world/voxel", EDIT).get("voxels_changed")
    call("POST", P, {"stream": True, "stream_bg": mode == "green"})
    n0 = status()["shifts"]
    t0 = time.time()
    cam(8176.0 + 64.0, 112.0)   # chunk 257 = the box's last: leaves the inner chunks -> a +1 shift
    s = wait_shift(n0)
    out["shift_wall_s"] = round(time.time() - t0, 2)
    out["status"] = {k: s.get(k) for k in ("region", "shifts", "last_shift_ms", "max_shift_ms", "max_slice_ms", "last_bg", "faces")}
    if mode == "green":
        got = chunks()
        out["H1_chunks"] = len(got)
        out["H1_equal_voxels"] = sum(got.get(k, (None,))[0] == ref[k][0] for k in ref)
        out["H1_equal_faces"] = sum(got.get(k, (0, None))[1] == ref[k][1] for k in ref)
        out["H1_equal_sprigs"] = sum(got.get(k, (0, 0, None))[2] == ref[k][2] for k in ref)
        out["H1_mismatch"] = [[k, got.get(k), ref[k]] for k in ref if got.get(k) != ref[k]][:6]
        walk, x, z = [], 8240.0, 112.0
        for step in range(17):
            cam(x, z)
            time.sleep(1.5)
            st = status()
            rg = st["region"]
            cx, cz = int(x // 32), int(z // 32)
            inside = rg["from"][0] <= cx <= rg["to"][0] and rg["from"][2] <= cz <= rg["to"][2]
            pick = call("POST", P, {"pick": {"origin": [x, 62.0, z], "dir": [0, -1, 0], "max_m": 62, "size": 8}}).get("voxel")
            walk.append({"x": x, "from": rg["from"], "in": inside, "ground": pick is not None, "shifts": st["shifts"],
                         "pending": st["job_pending"]})
            x += 16.0
        out["H3_walk"] = walk
        out["H3_all_in"] = all(w["in"] for w in walk)
        out["H3_all_ground"] = all(w["ground"] for w in walk)
        out["H2"] = {k: status().get(k) for k in ("shifts", "max_slice_ms", "max_shift_ms", "last_bg")}
    call("POST", P, {"stream": False})
    call("POST", P, {"world_switch": False})
    print(json.dumps(out)[:5000])
    with open(os.path.join(EVID, "voxfound_f79c_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
