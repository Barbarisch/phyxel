#!/usr/bin/env python3
"""The camera-walk probe, as a tool (docs/Water.md sec. 8 #8b; docs/WaterRethink.md WP0).

THE CAMERA INVARIANT: whether water exists at a column must never depend on camera position.
Resolution may follow the camera; existence may not. It was violated three times and each time a
human found it by walking toward the water. This encodes the walk:

  1. pose the camera FAR from a fixed world rect, let streaming settle, read
       (a) `water_render_grid`  - what the sea sheet would DRAW per column (the observable), and
       (b) `water_spans_stored` - what the chunks HOLD (camera-independent world data), with the
                                  set of RESIDENT chunk columns, so "dry" can be told from
                                  "not loaded";
  2. pose the camera NEAR the same rect, settle, read both again;
  3. classify every column of the rect:
       VIOLATION  resident at BOTH poses, rendered wet/dry differs, or level differs by > eps;
       VIOLATION  at either pose, resident, and rendered wet/dry disagrees with the stored spans
                  (the sheet must show span truth; a disagreement is the bake-upload reversion
                  hazard or a stale grid);
       COVERAGE   resident at one pose only (residency moved: allowed - the rule terrain obeys -
                  but counted and reported, never silently dropped);
       SOURCE     the sheet's placement source changed between poses (e.g. grounded -> bake).

A run passes only with zero VIOLATION columns and an unchanged source.

Self-test (`--inject-water-look`): at the far pose, after the clean read, POST water_look
{active:true}. That resets the level-grid upload memo and the NEXT frame re-uploads the coarse
128 m bake as placement (rendering audit, WaterRethink.md 1.4) - a placement change under a
STILL camera. The probe re-reads the SAME pose and must report a source change and/or
rendered-vs-spans disagreements; if it reports a clean read, the probe is blind and the run
fails. (The reversion is transient - a later span-grid rebuild on residency change hides it -
which is exactly why the re-read happens without moving.)

    python tools/water_camera_probe.py coast                 # poses from game.json waterBench.cameraProbe
    python tools/water_camera_probe.py coast --rect 37 708 293 964 --eps 0.01
    python tools/water_camera_probe.py coast --inject-water-look   # run LAST (see below)

⚑ The self-test POLLUTES the engine: `water_look {active:false}` re-uploads the bake too, and the
placement stays on the bake until a residency change whose chunk COUNT differs from the last
rebuild's (the span grid keys on count, not set - WaterRethink.md WP1 step 6). A normal run right
after a self-test reports tens of thousands of real violations against that polluted state (seen
2026-10-08: 50,787). Run the self-test last, or move the camera far away and back before the next
normal run.

Evidence: docs/evidence/water_v4_camera_probe.jsonl (one row per run, git head + timestamp).
Engine: the bench project's port (.phyxel/config.json) unless --url.
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from water_bench import Api, camera_set, load_def, project_url  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / "docs" / "evidence" / "water_v4_camera_probe.jsonl"


def settle(api, max_s=240):
    """Wait for streaming + remesh to go idle (the perf harness's settle keys), bounded."""
    t0 = time.time()
    last = None
    while time.time() - t0 < max_s:
        try:
            st = api.get("/api/debug/load_state")
        except Exception:
            st = {}
        pend = sum(int(st.get(k, 0) or 0) for k in ("generation_pending", "remesh_pending", "remesh_idle_pending"))
        if pend == 0 and last == 0:
            return time.time() - t0
        last = pend
        time.sleep(2.0)
    return time.time() - t0


class Read:
    """One observation of the rect: rendered wet map, stored-span top map, resident chunk columns."""

    def __init__(self, api, rect):
        x1, z1, x2, z2 = rect
        body = {"x1": x1, "z1": z1, "x2": x2, "z2": z2, "columns": True, "max_columns": 4_200_000}
        self.grid = api.debug("water_render_grid", body, timeout=300)
        if "error" in self.grid:
            raise SystemExit(f"water_render_grid: {self.grid}")
        self.spans = api.debug("water_spans_stored", body, timeout=300)
        self.rendered = {(c[0], c[1]): c[2] for c in self.grid.get("wet_columns", [])}
        self.stored = {}
        for c in self.spans.get("span_columns", []):          # topmost span per column
            k = (c[0], c[1])
            if k not in self.stored or c[2] > self.stored[k]:
                self.stored[k] = c[2]
        self.resident = {(c[0], c[1]) for c in self.spans.get("resident_chunk_columns", [])}
        self.source = self.grid.get("source")

    def is_resident(self, x, z):
        return (x // 32, z // 32) in self.resident

    def summary(self):
        return {"source": self.source, "rendered_wet": self.grid["wet"], "rendered_dry": self.grid["dry"],
                "off_grid": self.grid["off_grid"], "spans": self.spans.get("spans"),
                "span_chunks": self.spans.get("chunks_with_spans"), "resident_chunk_columns": len(self.resident),
                "min_level": self.grid["min_level"], "max_level": self.grid["max_level"],
                "span_columns_truncated": self.spans.get("span_columns_truncated"),
                "wet_columns_truncated": self.grid.get("wet_columns_truncated")}


def rendered_vs_spans(read, rect, eps):
    """Resident columns where the renderer and the stored spans disagree."""
    x1, z1, x2, z2 = rect
    out = []
    for z in range(z1, z2 + 1):
        for x in range(x1, x2 + 1):
            if not read.is_resident(x, z):
                continue
            r, s = read.rendered.get((x, z)), read.stored.get((x, z))
            if (r is None) != (s is None):
                out.append((x, z, "rendered wet/dry != spans", r, s))
            elif r is not None and abs(r - s) > eps:
                out.append((x, z, "rendered level != span top", r, s))
    return out


def far_vs_near(rect, far, near, eps):
    x1, z1, x2, z2 = rect
    viol, cover, both = [], 0, 0
    for z in range(z1, z2 + 1):
        for x in range(x1, x2 + 1):
            rf, rn = far.is_resident(x, z), near.is_resident(x, z)
            if rf != rn:
                cover += 1
                continue
            if not rf:
                continue
            both += 1
            a, b = far.rendered.get((x, z)), near.rendered.get((x, z))
            if (a is None) != (b is None):
                viol.append((x, z, "wet/dry differs between poses", a, b))
            elif a is not None and abs(a - b) > eps:
                viol.append((x, z, "level differs between poses", a, b))
    return viol, cover, both


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bench", choices=["coast", "river", "basin"])
    ap.add_argument("--rect", nargs=4, type=int, metavar=("X1", "Z1", "X2", "Z2"))
    ap.add_argument("--eps", type=float, default=1e-3)
    ap.add_argument("--url", default=os.environ.get("PHYXEL_API_URL"))
    ap.add_argument("--inject-water-look", action="store_true",
                    help="self-test: force the water_look override at the far pose and re-read the same pose")
    args = ap.parse_args()

    gdef = load_def(args.bench)
    spec = gdef["waterBench"]
    probe = spec.get("cameraProbe")
    if not probe:
        raise SystemExit("game.json waterBench has no cameraProbe {far, near}")
    rect = args.rect or probe.get("rect") or spec["spanRect"]   # the probe's own rect straddles the shore
    api = Api(project_url(args.bench, args.url))
    api.wait_status()
    status = api.get("/api/status")
    head = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT).decode().strip()
    row = {"bench": args.bench, "rect": rect, "eps": args.eps, "inject_water_look": args.inject_water_look,
           "build_config": status.get("build_config"), "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"), "git_head": head}

    def observe(name, pose):
        got = camera_set(api, pose)
        waited = settle(api)
        rd = Read(api, rect)
        mism = rendered_vs_spans(rd, rect, args.eps)
        row[name] = dict(rd.summary(), pose=got, settle_s=round(waited, 1), rendered_vs_spans=len(mism),
                         rendered_vs_spans_samples=mism[:5])
        print(f"[{name}] source={rd.source} rendered wet={rd.grid['wet']} spans={rd.spans.get('spans')} "
              f"resident chunk cols={len(rd.resident)} rendered!=spans: {len(mism)} settle={waited:.0f}s")
        return rd, mism

    far, far_mism = observe("far", probe["far"])

    if args.inject_water_look:
        # The reversion is hidden whenever residency is still changing (a span-grid rebuild lands
        # on top of the bake upload within the same second), so first wait for the chunk count to
        # hold still - the normal state in play, and the state in which the hazard is visible.
        x1, z1, x2, z2 = rect
        stable, last = 0, None
        for _ in range(60):
            n = api.debug("water_spans_stored", {"x1": x1, "z1": z1, "x2": x2, "z2": z2}).get("chunks_loaded")
            stable = stable + 1 if n == last else 0
            last = n
            if stable >= 4:
                break
            time.sleep(1.0)
        far = Read(api, rect)                             # re-baseline on the static residency
        far_mism = rendered_vs_spans(far, rect, args.eps)
        api.debug("water_look", {"active": True, "turbidity": 0.5, "roughness": 1.0})
        seen = []
        rd2, mism2 = far, far_mism
        for _ in range(12):                               # the flip lands within a frame; poll 3 s
            time.sleep(0.25)
            rd2 = Read(api, rect)
            mism2 = rendered_vs_spans(rd2, rect, args.eps)
            seen.append(rd2.source)
            if rd2.source != far.source or len(mism2) > len(far_mism):
                break
        api.debug("water_look", {"active": False})
        detected = (rd2.source != far.source) or len(mism2) > len(far_mism)
        row["self_test"] = {"chunk_count_static": last, "source_before": far.source, "source_after": rd2.source,
                            "sources_seen": seen, "rendered_wet_before": far.grid["wet"], "rendered_wet_after": rd2.grid["wet"],
                            "rendered_vs_spans_before": len(far_mism), "rendered_vs_spans_after": len(mism2),
                            "detected": detected}
        row["ok"] = detected
        EVIDENCE.parent.mkdir(parents=True, exist_ok=True)
        with EVIDENCE.open("a", encoding="utf-8") as f:
            f.write(json.dumps(row) + "\n")
        print(json.dumps(row["self_test"], indent=1))
        if not detected:
            raise SystemExit("SELF-TEST FAILED: the injected water_look reversion was NOT detected (probe is blind)")
        print("self-test PASSED: the injected placement change was detected at a still camera")
        return

    near, near_mism = observe("near", probe["near"])
    viol, cover, both = far_vs_near(rect, far, near, args.eps)
    all_viol = viol + far_mism + near_mism
    source_changed = far.source != near.source
    ok = not all_viol and not source_changed
    row.update({"violations": len(all_viol), "pose_violations": len(viol),
                "violation_samples": all_viol[:10], "coverage_changed_columns": cover,
                "resident_at_both": both, "source_changed": source_changed, "ok": ok})
    EVIDENCE.parent.mkdir(parents=True, exist_ok=True)
    with EVIDENCE.open("a", encoding="utf-8") as f:
        f.write(json.dumps(row) + "\n")
    print(json.dumps({k: v for k, v in row.items() if k not in ("far", "near")}, indent=1))
    if not ok:
        raise SystemExit(f"CAMERA INVARIANT VIOLATED: {len(all_viol)} columns "
                         f"(pose {len(viol)}, rendered!=spans far {len(far_mism)} near {len(near_mism)}), "
                         f"source_changed={source_changed}")
    print(f"camera invariant holds on {args.bench} rect {rect}: {both} columns resident at both poses, "
          f"{cover} coverage-only changes, rendered == spans at both poses")


if __name__ == "__main__":
    main()
