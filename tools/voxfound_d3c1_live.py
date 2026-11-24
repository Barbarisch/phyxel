"""D3c-1 live (docs/evidence/voxfound_f7_prediction.txt K2 / K3). Usage: voxfound_d3c1_live.py <k2|k3> (8120).
k2: the fill_region JOB on the booted switched world (VoxFoundGate8): a 2 x 2 x 2 m Gold fill above the ground.
k3: ApiTestGame copy WITHOUT its DB -- the game.json structures (3 fills) can only reach the switched world through
the deferred structures path (GameDefinitionLoader::applyDeferred after the boot). Every artifact: the engine's fill
path on the switched world; nothing hand-placed by this driver except K2's job request."""
import json, os, shutil, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")


def call(m, p, b=None, timeout=600):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        return {"http_status": e.code, "body": e.read().decode()[:300]}
    except Exception as e:
        return {"exception": str(e)}


def vox(x, y, z):
    return call("GET", f"/api/world/voxel?x={x}&y={y}&z={z}").get("voxfound") or {}


def main():
    mode = sys.argv[1]
    out = {"mode": mode, "today_before": call("POST", P, {"today_status": True})}
    if mode == "k2":
        cam = call("GET", "/api/camera").get("position") or {"x": 8150, "z": 200}
        x, z = int(cam["x"]), int(cam["z"])
        th = call("GET", f"/api/world/terrain_height?x={x}&z={z}")
        y = int(th.get("surface_y", 20)) + 2
        job = call("POST", "/api/job/submit", {"type": "fill_region", "params": {
            "x1": x, "y1": y, "z1": z, "x2": x + 1, "y2": y + 1, "z2": z + 1, "material": "Gold"}})
        out["job"] = job
        time.sleep(3)
        out["at"] = [x, y, z]
        out["cubes"] = {f"{dx},{dy},{dz}": vox(x + dx, y + dy, z + dz) for dx in (0, 1) for dy in (0, 1) for dz in (0, 1)}
    else:
        b = call("POST", P, {"boot_status": True}).get("report") or {}
        out["deferred_content"] = b.get("deferred_content")
        out["platform_11_17_11"] = vox(11, 17, 11)
        out["hut_wall_12_19_16"] = vox(12, 19, 16)
        out["hut_inside_16_19_16"] = vox(16, 19, 16)
        out["doorway_16_19_12"] = vox(16, 19, 12)
        call("POST", "/api/camera", {"position": {"x": 30.0, "y": 30.0, "z": 30.0}, "yaw": -135.0, "pitch": -25.0})
        time.sleep(3)
        shot = call("GET", "/api/screenshot")
        if shot.get("path"):
            dst = os.path.join(EVID, "d3c1_k3_apitestgame.png")
            shutil.copyfile(os.path.join(REPO, shot["path"]), dst)
            out["shot"] = os.path.relpath(dst, REPO)
    out["today_after"] = call("POST", P, {"today_status": True})
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_d3c1_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
