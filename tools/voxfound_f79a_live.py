"""F7.9a live (docs/evidence/voxfound_f7_prediction.txt G1-G3): the generation rate. VoxFoundGate (port 8120).
Builds the gate region (32 chunks, flora, editable) with the reference path (gen_threads 1 + gen_legacy -- the red /
the control's numbers), then the new terrain path at 1, 4, 8 and 16 worker threads; per build: generate_ms,
build_wall_ms, faces and the 32 store hashes (G1: every build equal to the reference). Writes
docs/evidence/voxfound_f79_live.jsonl. Every artifact: WorldGenerator Perlin s7 + flora (the 1/8 m region)."""
import json, os, time, urllib.request

API = "http://127.0.0.1:8120"
P = "/api/debug/voxfound_preview"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
REGION = {"type": "Perlin", "seed": 7, "flora": True, "editable": True, "hide_world": False,
          "region": {"from": [254, 0, 2], "to": [257, 1, 5]}}


def call(m, p, b=None):
    r = urllib.request.Request(API + p, data=json.dumps(b).encode() if b is not None else None, method=m,
                               headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(r, timeout=3600) as resp:
        return json.loads(resp.read().decode())


def build(**kw):
    b = dict(REGION)
    b.update(kw)
    t0 = time.time()
    r = call("POST", P, b)
    wall = time.time() - t0
    h = {",".join(map(str, c["chunk"])): c["hash"] for c in call("POST", P, {"store_hash": True})["chunks"]}
    n = r.get("chunks", 0)
    return {"knobs": kw, "chunks": n, "faces": r.get("faces"), "generate_ms": round(r.get("generate_ms", 0), 1),
            "chunks_per_s_generation": round(n / max(1e-9, r.get("generate_ms", 0) / 1000.0), 2),
            "build_wall_ms": round(r.get("build_wall_ms", 0), 1), "request_wall_s": round(wall, 2), "hashes": h}


def main():
    runs = [build(gen_threads=1, gen_legacy=True)]
    for t in (1, 4, 8, 16):
        runs.append(build(gen_threads=t))
    ref = runs[0]["hashes"]
    for r in runs:
        r["G1_hashes_equal_reference"] = sum(r["hashes"][k] == ref[k] for k in ref)
        r["G1_faces_equal_reference"] = r["faces"] == runs[0]["faces"]
        del r["hashes"]
        print(json.dumps(r))
    with open(os.path.join(REPO, "docs", "evidence", "voxfound_f79_live.jsonl"), "a") as f:
        f.write(json.dumps({"mode": "red + green sweep", "runs": runs}) + "\n")


if __name__ == "__main__":
    main()
