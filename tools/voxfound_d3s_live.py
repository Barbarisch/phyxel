"""D3-scenes live (docs/evidence/voxfound_f7_prediction.txt C1-C3): a multi-scene game on the switched world.
A COPY of WaterBench_Scenes (8120). Every artifact: each scene's region built by VoxFoundWorld::sceneLoad (the default
rule: `dry` typed Flat -> generator + DB overlay; `pond` DB-only -> its stored chunks); the Gold block is placed by the
voxel API (routed to the switched world) and must come back from `dry`'s DB."""
import json, os, shutil, time, urllib.request

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


def state(tag):
    st = call("POST", P, {"scene_status": True})
    td = call("POST", P, {"today_status": True})
    act = call("GET", "/api/scene/active")
    cam = call("GET", "/api/camera")
    time.sleep(2)
    shot = call("GET", "/api/screenshot")
    out = {"scene": act.get("id") or act.get("scene_id") or act, "switched": st.get("switched"),
           "region_chunks": st.get("region_chunks"), "last_log": (st.get("log") or [None])[-1],
           "today": td.get("today_chunks_resident"), "camera": (cam.get("position") if isinstance(cam, dict) else None)}
    if shot.get("path"):
        dst = os.path.join(EVID, f"d3s_{tag}.png")
        shutil.copyfile(os.path.join(REPO, shot["path"]), dst)
        out["shot"] = os.path.relpath(dst, REPO)
    return out


def go(scene):
    r = call("POST", "/api/scene/transition", {"scene_id": scene})
    for _ in range(60):
        time.sleep(2)
        a = call("GET", "/api/scene/active")
        if (a.get("id") or a.get("scene_id")) == scene and not a.get("transitioning", False):
            break
    time.sleep(6)
    return r


def voxel(x, y, z):
    return (call("GET", f"/api/world/voxel?x={x}&y={y}&z={z}").get("voxfound") or {})


def main():
    out = {"C1": state("start")}
    out["to_dry"] = go("dry")
    out["C2_dry"] = state("dry")
    cam = out["C2_dry"].get("camera") or {"x": 104, "y": 20, "z": 16}
    gx, gz = int(cam["x"]), int(cam["z"]) + 3
    th = call("GET", f"/api/world/terrain_height?x={gx}&z={gz}")
    gy = int(th.get("surface_y", 16)) + 3
    out["gold_at"] = [gx, gy, gz]
    out["place"] = call("POST", "/api/world/voxel", {"x": gx, "y": gy, "z": gz, "material": "Gold"})
    out["gold_before"] = voxel(gx, gy, gz)
    out["to_pond"] = go("pond")
    out["C2_pond"] = state("pond")
    out["gold_in_pond"] = voxel(gx, gy, gz)
    out["back_to_dry"] = go("dry")
    out["C3_dry_again"] = state("dry_again")
    out["gold_after"] = voxel(gx, gy, gz)
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_d3s_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
