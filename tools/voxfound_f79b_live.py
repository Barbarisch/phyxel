"""F7.9b live (docs/evidence/voxfound_f7_prediction.txt T1-T4): the switched world streams. VoxFoundGate (port 8120).
  control / red : switch on (load_saved false), the camera 3 chunks east -> is its chunk in the region? the region's
                  pick down from the camera. (red: stream_stub + stream on)
  green         : T1 edits across shifts (stream_shift), T2 a 256 m walk with stream on, T3 shift costs, T4 save_world.
The caller backs up / restores the project's world.db (T4 writes it). Every artifact: WorldGenerator Perlin s7 + flora
(the 1/8 m region), edits through the engine's voxel API."""
import json, os, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
BASE = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False, "flora_window": "open"}
BOX0 = {"from": [254, 0, 2], "to": [257, 1, 5]}
BOX1 = {"from": [256, 0, 2], "to": [259, 1, 5]}


def call(m, p, b=None, timeout=3600):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}


def hashes():
    return {",".join(map(str, c["chunk"])): c["hash_by_name"] for c in call("POST", P, {"store_hash": True})["chunks"]}


def build(box):
    b = dict(BASE)
    b["region"] = box
    return call("POST", P, b)


def cam(x, z, y=60.0):
    return call("POST", "/api/camera", {"mode": "free", "position": {"x": x, "y": y, "z": z}, "yaw": 90, "pitch": -60})


def pick_down(x, z):
    r = call("POST", P, {"pick": {"origin": [x, 62.0, z], "dir": [0, -1, 0], "max_m": 62, "size": 8}})
    return r.get("voxel")


def status():
    return call("POST", P, {"stream_status": True})


def in_region(x, z, rg):
    cx, cz = int(x // 32), int(z // 32)
    return rg["from"][0] <= cx <= rg["to"][0] and rg["from"][2] <= cz <= rg["to"][2]


def main():
    mode = sys.argv[1]
    out = {"mode": mode}
    if mode in ("control", "red"):
        build(BOX0)
        if mode == "red":
            out["stub"] = call("POST", P, {"stream_stub": True, "stream": True})
        out["switch"] = call("POST", P, {"world_switch": True, "load_saved": False})
        cam(8176.0, 112.0)
        time.sleep(2.0)
        x, z = 8176.0 + 96.0, 112.0
        cam(x, z)
        time.sleep(4.0)
        out["camera_chunk"] = [int(x // 32), int(z // 32)]
        out["region_after"] = status().get("region") if mode == "red" else BOX0
        out["camera_in_region"] = in_region(x, z, out["region_after"] or BOX0)
        out["pick_down"] = pick_down(x, z)
        call("POST", P, {"world_switch": False})
        if mode == "red":
            call("POST", P, {"stream_stub": False, "stream": False})
    else:
        build(BOX1)
        g1 = hashes()
        build(BOX0)
        g0 = hashes()
        out["switch"] = call("POST", P, {"world_switch": True, "load_saved": False})
        cam(8176.0, 112.0)
        # T1: A in chunk (256,1,3) stays on a +2 shift, B in (254,1,3) leaves
        out["edit_A"] = call("POST", "/api/world/voxel", {"x": 8200, "y": 40, "z": 100, "material": "Stone"})
        out["edit_B"] = call("POST", "/api/world/voxel", {"x": 8140, "y": 40, "z": 100, "material": "Bricks"})
        e = hashes()
        A, B = "256,1,3", "254,1,3"
        out["T1_edited"] = {"A_changed": e[A] != g0[A], "B_changed": e[B] != g0[B]}
        t0 = time.time()
        out["shift_plus2"] = call("POST", P, {"stream_shift": [256, 0, 2]})
        out["shift_plus2_http_s"] = round(time.time() - t0, 2)
        h1 = hashes()
        out["T1_after_shift"] = {"A_equals_edited": h1.get(A) == e[A],
                                 "others_equal_generator": sum(h1[k] == g1[k] for k in g1 if k != A),
                                 "others": len(g1) - 1, "cache": status().get("cached_chunks")}
        out["shift_back"] = call("POST", P, {"stream_shift": [254, 0, 2]})
        h2 = hashes()
        out["T1_after_back"] = {"A_equals_edited": h2[A] == e[A], "B_equals_edited": h2[B] == e[B],
                                "others_equal_generator": sum(h2[k] == g0[k] for k in g0 if k not in (A, B)),
                                "others": len(g0) - 2}
        # T2: the walk
        call("POST", P, {"stream": True})
        walk = []
        x, z = 8176.0, 112.0
        for step in range(17):
            cam(x, z)
            t0 = time.time()
            time.sleep(0.5)
            st = status()
            while True:   # a shift runs inside a frame: wait until the request answers and the camera is in the box
                st = status()
                if in_region(x, z, st["region"]) or time.time() - t0 > 30:
                    break
                time.sleep(0.25)
            walk.append({"x": x, "region_from": st["region"]["from"], "in_region": in_region(x, z, st["region"]),
                         "pick": pick_down(x, z) is not None, "shifts": st["shifts"], "last_shift_ms": round(st["last_shift_ms"])})
            x += 16.0
        out["T2_walk"] = walk
        out["T2_all_in_region"] = all(w["in_region"] for w in walk)
        out["T2_all_ground"] = all(w["pick"] for w in walk)
        st = status()
        out["T3"] = {"shifts": st["shifts"], "max_shift_ms": round(st["max_shift_ms"]), "last_shift_ms": round(st["last_shift_ms"]),
                     "cached_chunks": st["cached_chunks"]}
        out["T4_save"] = call("POST", "/api/world/save", {}).get("voxfound")
        call("POST", P, {"stream": False})
        call("POST", P, {"world_switch": False})
    print(json.dumps(out)[:5000])
    with open(os.path.join(EVID, "voxfound_f79b_live.jsonl"), "a") as f:
        f.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
