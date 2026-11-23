"""D3-core part 3 live (docs/evidence/voxfound_f7_prediction.txt S1-S3): the asset editor on the switched world.
Usage: voxfound_d3c3_live.py <template-path> (the engine runs `--asset-editor <path> --port 8120`).
Every artifact: the template FILE painted by the switched spawn path (grid 8 as is, C/S/M via loadCsmTemplate) onto
the Flat-generator stage; the save is the switched world's cells written by VoxFoundWorld::saveGrid8Template."""
import hashlib, json, os, shutil, sys, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EVID = os.path.join(REPO, "docs", "evidence")
sys.path.insert(0, os.path.join(REPO, "tools"))


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


def grid_counts(path):
    """per-material cell counts of a `# grid:` B/V template"""
    n = {}
    for line in open(path, encoding="utf-8"):
        t = line.split()
        if not t or t[0] not in ("B", "V"):
            continue
        if t[0] == "V":
            n[t[4]] = n.get(t[4], 0) + 1
        else:
            n[t[7]] = n.get(t[7], 0) + int(t[4]) * int(t[5]) * int(t[6])
    return n


def main():
    src = sys.argv[1]
    out = {"template": src}
    before = hashlib.sha256(open(os.path.join(REPO, src), "rb").read()).hexdigest()
    out["today"] = call("POST", P, {"today_status": True})
    out["stream"] = call("POST", P, {"stream_status": True})
    # S1: the floor top at 16 m beside the origin (x 4, z 4 m, clear of the template): the metre cube at y 15 full
    # (512 voxels), y 16 empty -- the stage is cleared above 16 m
    out["floor"] = {str(y): (call("GET", f"/api/world/voxel?x=4&y={y}&z=4").get("voxfound") or {}) for y in (15, 16)}
    time.sleep(2)
    shot = call("GET", "/api/screenshot")
    if shot.get("path"):
        dst = os.path.join(EVID, "d3c3_" + os.path.splitext(os.path.basename(src))[0] + ".png")
        shutil.copyfile(os.path.join(REPO, shot["path"]), dst)
        out["shot"] = os.path.relpath(dst, REPO)
    # S2 / S3: save round trip
    sv = call("POST", "/api/asset-editor/save", {})
    out["save"] = {k: sv.get(k) for k in ("success", "error", "path", "cells", "per_material", "legacy_source")}
    out["spawn"] = {k: (sv.get("spawn") or {}).get(k) for k in ("success", "error", "cells", "verify_bad", "converted")}
    if sv.get("success"):
        saved = os.path.join(REPO, sv["path"])
        out["saved_counts"] = grid_counts(saved)
        pristine = os.path.join(REPO, "resources", "templates_g8", "furniture", os.path.basename(src))
        if not out["save"].get("legacy_source") and os.path.exists(pristine):   # the library original, not the copy
            out["input_counts"] = grid_counts(pristine)
            out["S2_equal"] = out["saved_counts"] == out["input_counts"]
    out["source_unchanged"] = hashlib.sha256(open(os.path.join(REPO, src), "rb").read()).hexdigest() == before
    print(json.dumps(out))
    with open(os.path.join(EVID, "voxfound_d3c3_live.jsonl"), "a") as fh:
        fh.write(json.dumps(out) + "\n")


if __name__ == "__main__":
    main()
