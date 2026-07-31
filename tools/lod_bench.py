"""LodBench harness — reproducible render/LOD perf measurement.

Targets the dedicated LodBench project (baked deterministic Perlin-hills world with a
density ladder). See docs/ContinuousLodPlan.md §7b and PhyxelProjects/LodBench/README.md.

Built to satisfy the method defects a solution-auditor found in the first (failed) M1 run
on 2026-07-29:
  D1  pose validity was RECORDED but never enforced -> here a pose_ok:false sample is
      REJECTED, retried, and if it still fails the point is marked invalid. It never
      silently enters a comparison.
  D2  the shadow-cull toggle and render distance were unlogged confounds -> both are
      pinned and written into every record.
  D3  n=3 medians on a bimodal distribution are a coin flip -> default n=15, and the
      high-mode fraction is reported so bimodality is visible, not hidden by a median.
  D4  raw samples were discarded, leaving summary claims uncheckable -> every sample is
      archived as JSONL.
  D5  scene provenance was prose-only -> --provenance records the generator calls that
      produced the world, in the evidence file itself.

Usage:
  python tools/lod_bench.py --port 8097 --out docs/evidence/<name>.jsonl
  python tools/lod_bench.py --port 8097 --sweep --out docs/evidence/<name>.jsonl
"""
import argparse, json, statistics, sys, time, urllib.error, urllib.request

# Region centres + measured surface heights (probed 2026-07-29 on seed 777001,
# heightScale 14 / freq 0.03 / oct 4 / persist 0.5). Surface ~y53 => CHUNK y=1.
POSES = {
    "bare":     {"center": [-200, -200], "surface": 40, "desc": "bare hills, no structures"},
    "tavern":   {"center": [-200,  100], "surface": 40, "desc": "one v2 tavern, 2 stories"},
    "village":  {"center": [   0,    0], "surface": 51, "desc": "medieval village, 8 buildings"},
    "town":     {"center": [ 150,  150], "surface": 61, "desc": "medieval town, 13 buildings"},
}
OVERVIEW = {"position": {"x": 0.0, "y": 220.0, "z": 320.0}, "yaw": -90.0, "pitch": -32.0}
POSE_TOL = 0.5


class Bench:
    def __init__(self, port, out):
        self.base = f"http://localhost:{port}"
        self.out = open(out, "w", encoding="utf-8")

    def req(self, path, body=None, timeout=30):
        url = self.base + path
        r = (urllib.request.Request(url) if body is None else
             urllib.request.Request(url, data=json.dumps(body).encode(),
                                    headers={"Content-Type": "application/json"}))
        with urllib.request.urlopen(r, timeout=timeout) as f:
            return json.loads(f.read().decode())

    def emit(self, rec):
        self.out.write(json.dumps(rec) + "\n")
        self.out.flush()

    # -- config pinning: every confound the audit named is set AND recorded -------------
    def pin_config(self, render_distance):
        cfg = {
            "pipeline_stats": self.req("/api/debug/pipeline_stats", {"enabled": False}),
            "shadow_cull":    self.req("/api/debug/shadow_cull", {"enabled": False}),
            "quad_draw":      self.req("/api/debug/quad_draw", {"enabled": True}),
            "fine_merge":     self.req("/api/debug/fine_merge", {"enabled": True}),
            "render_distance": self.req("/api/debug/render_distance", {"distance": render_distance}),
        }
        self.emit({"event": "config_pinned", "config": cfg})
        return cfg

    def set_pose(self, pose):
        # mode FIRST, in its own call: set_camera applies `position` before `mode`.
        self.req("/api/camera", {"mode": "free"})
        time.sleep(0.4)
        self.req("/api/camera", pose)
        time.sleep(0.8)

    def pose_holds(self, pose):
        cam = self.req("/api/camera")
        want = pose["position"]
        drift = max(abs(cam["position"][a] - want[a]) for a in "xyz")
        ok = (drift < POSE_TOL and abs(cam["yaw"] - pose["yaw"]) < POSE_TOL
              and abs(cam["pitch"] - pose["pitch"]) < POSE_TOL)
        return ok, drift, cam

    @staticmethod
    def scope_ms(scopes, name):
        for s in scopes:
            if s.get("name") == name:
                return s.get("ms")
        return None

    def measure(self, label, pose, n, settle=4.0):
        """Sample a pose n times. A pose that will not hold is reported INVALID, not used."""
        self.set_pose(pose)
        time.sleep(settle)
        ok, drift, cam = self.pose_holds(pose)
        if not ok:                       # one retry, then give up honestly
            self.emit({"event": "pose_retry", "label": label, "drift": drift})
            self.set_pose(pose)
            time.sleep(settle)
            ok, drift, cam = self.pose_holds(pose)
        if not ok:
            self.emit({"event": "pose_INVALID", "label": label, "drift": drift,
                       "want": pose, "got": cam,
                       "note": "samples NOT collected; this point must not enter a comparison"})
            print(f"  {label:<10} POSE INVALID (drift {drift:.1f}) -- skipped", flush=True)
            return None

        rows = []
        for i in range(n):
            g = self.req("/api/debug/gpu_scopes")
            r = self.req("/api/render/stats")
            ok_i, drift_i, _ = self.pose_holds(pose)
            rec = {"event": "sample", "label": label, "sample": i,
                   "pose_ok": ok_i, "pose_drift": drift_i,
                   "shadow_ms": self.scope_ms(g["scopes"], "Shadow Pass"),
                   "static_ms": self.scope_ms(g["scopes"], "Static Geometry"),
                   "shadow_chunks_drawn": g.get("shadow_chunks_drawn"),
                   "shadow_instances_drawn": g.get("shadow_instances_drawn"),
                   "visible_chunks": g.get("visible_chunks"),
                   "total_visible_faces": r.get("total_visible_faces"),
                   "scopes_raw": g["scopes"]}
            self.emit(rec)
            rows.append(rec)
            time.sleep(0.4)

        valid = [r for r in rows if r["pose_ok"]]
        dropped = len(rows) - len(valid)
        sh = [r["shadow_ms"] for r in valid if r["shadow_ms"] is not None]
        if not sh:
            print(f"  {label:<10} no valid samples", flush=True)
            return None
        lo = statistics.median(sh)
        # Report the high-mode fraction so a bimodal distribution stays VISIBLE.
        hi_frac = sum(1 for v in sh if v > 1.5 * min(sh)) / len(sh)
        summary = {
            "event": "summary", "label": label, "n_valid": len(valid), "n_dropped": dropped,
            "shadow_ms_median": lo, "shadow_ms_min": min(sh), "shadow_ms_max": max(sh),
            "shadow_ms_stdev": statistics.pstdev(sh) if len(sh) > 1 else 0.0,
            "high_mode_fraction": hi_frac,
            "static_ms_median": statistics.median([r["static_ms"] for r in valid if r["static_ms"] is not None]),
            "shadow_chunks_drawn": sorted({r["shadow_chunks_drawn"] for r in valid}),
            "shadow_instances_drawn": sorted({r["shadow_instances_drawn"] for r in valid}),
            "visible_chunks": sorted({r["visible_chunks"] for r in valid}),
            "total_visible_faces": sorted({r["total_visible_faces"] for r in valid}),
        }
        self.emit(summary)
        print(f"  {label:<10} faces={summary['total_visible_faces']} "
              f"shadow_med={lo:.2f} (min {min(sh):.2f} max {max(sh):.2f} hi{hi_frac*100:.0f}%) "
              f"draws={summary['shadow_chunks_drawn']} vis={summary['visible_chunks']} "
              f"dropped={dropped}", flush=True)
        return summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8097)
    ap.add_argument("--out", required=True)
    ap.add_argument("-n", type=int, default=15, help="samples per point (audit: >=15)")
    ap.add_argument("--render-distance", type=float, default=256.0)
    ap.add_argument("--sweep", action="store_true", help="also sweep render distance from the overview pose")
    ap.add_argument("--provenance", help="path to a JSON file describing how the world was generated")
    a = ap.parse_args()

    b = Bench(a.port, a.out)
    b.emit({"event": "run_start", "port": a.port, "n": a.n,
            "render_distance": a.render_distance,
            "status": b.req("/api/status")})
    if a.provenance:
        b.emit({"event": "provenance", "data": json.load(open(a.provenance, encoding="utf-8"))})
    b.pin_config(a.render_distance)

    print(f"=== density ladder (n={a.n}, render_distance={a.render_distance}) ===")
    for label, r in POSES.items():
        cx, cz = r["center"]
        pose = {"position": {"x": float(cx), "y": float(r["surface"] + 35), "z": float(cz + 70)},
                "yaw": -90.0, "pitch": -25.0}
        b.measure(label, pose, a.n)
    print("=== overview ===")
    b.measure("overview", OVERVIEW, a.n)

    if a.sweep:
        print("=== render-distance sweep @ overview ===")
        for d in (64, 96, 128, 160, 192, 224, 256, 320, 384, 448, 512):
            b.req("/api/debug/render_distance", {"distance": d})
            b.measure(f"sweep_{d}", OVERVIEW, a.n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
