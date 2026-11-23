"""F7.29 live (docs/evidence/voxfound_f7_prediction.txt B1-B2): grass blades on the switched world. Project VoxFoundBoot.
Red (grass_stub) then green, the same pose. Every artifact: WorldGenerator Perlin s7 + flora on the 1/8 m grid + the
grass instances built from it, drawn by today's grass pipeline."""
import json, os, shutil, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=600) as x:
        return json.loads(x.read().decode())


def shot(name):
    time.sleep(4)
    r = call("GET", "/api/screenshot")
    dst = os.path.join(EVID, name)
    shutil.copyfile(os.path.join(REPO, r["path"]), dst)
    return os.path.relpath(dst, REPO)


def main():
    out = {}
    for _ in range(150):
        if call("POST", P, {"boot_status": True}).get("report"):
            break
        time.sleep(2)
    call("POST", P, {"stream": False})
    x, z = 8190.5, 200.5
    g = None
    for vy in range(40 * 8, 10 * 8, -1):
        if call("POST", P, {"pick": {"origin": [x, (vy + 0.5) / 8, z], "dir": [0, -1, 0], "max_m": 0.01, "size": 8}}).get("voxel"):
            g = (vy + 1) / 8
            break
    out["ground"] = g
    call("POST", "/api/camera", {"mode": "free", "position": {"x": x - 3.0, "y": g + 2.0, "z": z - 3.0}, "yaw": 45, "pitch": -20})
    out["red"] = call("POST", P, {"grass_stub": True})
    out["red_shot"] = shot("f729_grass_red.png")
    out["green"] = call("POST", P, {"grass_stub": False})
    out["green_status"] = call("POST", P, {"grass_status": True})
    out["green_shot"] = shot("f729_grass_green.png")
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_f729_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
