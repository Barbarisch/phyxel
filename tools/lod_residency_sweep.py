"""Residency / draw-cost sweep: what does raising view distance actually cost today?

Sweeps maxChunkRenderDistance and records, per step:
  * chunks resident + chunks actually drawn      (residency vs draw work)
  * total chunk faces
  * process working set (RAM) and GPU arena allocations
  * frame time from the render stats

The point is to locate the wall that C3 (persisted LOD pyramid) has to beat. On a BAKED world
every chunk is resident regardless of render distance, so RAM is expected to stay FLAT while
draw work grows -- that flatness is the finding, not a bug.

Usage: python tools/lod_residency_sweep.py --out docs/evidence/<name>.jsonl
"""
import argparse, json, subprocess, sys, time, urllib.request

def req(base, path, body=None, timeout=90):
    r = (urllib.request.Request(base + path) if body is None else
         urllib.request.Request(base + path, data=json.dumps(body).encode(),
                                headers={"Content-Type": "application/json"}))
    with urllib.request.urlopen(r, timeout=timeout) as f:
        return json.loads(f.read().decode())

def working_set_mb():
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "(Get-Process phyxel -EA SilentlyContinue|Select -First 1).WorkingSet64"],
                         capture_output=True, text=True).stdout.strip()
    try:
        return round(int(out) / (1024*1024), 1)
    except Exception:
        return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8090)
    ap.add_argument("--out", required=True)
    ap.add_argument("--settle", type=float, default=10.0)
    ap.add_argument("--distances", default="192,384,768,1536,3072")
    a = ap.parse_args()
    base = f"http://localhost:{a.port}"
    out = open(a.out, "w", encoding="utf-8")
    def emit(r):
        out.write(json.dumps(r) + "\n"); out.flush()

    emit({"event": "run_start", "note": "baked LodBench; every chunk resident regardless of "
                                        "render distance -- RAM flatness is expected"})
    # Pin the view so frustum culling is comparable across steps.
    req(base, "/api/camera", {"mode": "free"})
    time.sleep(1)
    req(base, "/api/camera", {"position": {"x": 0, "y": 420, "z": 620}, "yaw": -90, "pitch": -34})
    req(base, "/api/daynight/set", {"paused": True, "timeOfDay": 9.5})
    time.sleep(2)

    print(f"{'dist':>6} {'resident':>9} {'faces':>10} {'RAM_MB':>8} {'gpu_allocs':>11} {'mesh_avg':>9}")
    for d in [int(x) for x in a.distances.split(",")]:
        req(base, "/api/debug/render_distance", {"distance": d})
        time.sleep(a.settle)
        st = req(base, "/api/render/stats")
        lod = req(base, "/api/debug/distance_lod", {})
        by = lod.get("chunks_by_level", {})
        resident = sum(by.values()) if by else None
        rec = {"event": "sample", "render_distance": d,
               "chunks_resident": resident,
               "chunks_by_level": by,
               "total_chunk_faces": lod.get("total_chunk_faces"),
               "ram_mb": working_set_mb(),
               "gpu_chunk_allocs_live": st.get("gpu_chunk_allocs_live"),
               "far_tiles_resident": st.get("far_tiles_resident"),
               "far_tiles_drawn": st.get("far_tiles_drawn"),
               "mesh_avg_ms": st.get("mesh_timing", {}).get("avg_ms"),
               "mesh_max_ms": st.get("mesh_timing", {}).get("max_ms")}
        emit(rec)
        print(f"{d:>6} {str(resident):>9} {str(rec['total_chunk_faces']):>10} "
              f"{str(rec['ram_mb']):>8} {str(rec['gpu_chunk_allocs_live']):>11} "
              f"{str(round(rec['mesh_avg_ms'],2) if rec['mesh_avg_ms'] else None):>9}")
    out.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())
