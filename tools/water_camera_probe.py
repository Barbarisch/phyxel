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
  3. classify every column of the rect. "Resident" is judged in 3-D (WaterCore Phase D3,
     2026-10-08): the chunk(s) at the rect's water LEVEL BAND must be loaded in that column - at a
     high far pose the sea-level chunk of a column is often absent while a higher chunk of the
     same column is present, and the 2-D column list called that "resident" (1,888 false
     violations on Coast). Reads happen only after residency has held still for 2 s plus the
     span grid's 0.5 s cooldown (864 false near-pose mismatches came from reading mid-landing):
       VIOLATION  resident at BOTH poses, rendered wet/dry differs, or level differs by > eps;
       VIOLATION  at either pose, resident, and rendered wet/dry disagrees with the stored spans
                  (the sheet must show span truth; a disagreement is the bake-upload reversion
                  hazard or a stale grid);
       COVERAGE   resident at one pose only (residency moved: allowed - the rule terrain obeys -
                  but counted and reported, never silently dropped);
       SOURCE     the sheet's placement source changed between poses (e.g. grounded -> bake).

A run passes only with zero VIOLATION columns and an unchanged source.

Self-test (`--inject-water-look`, name kept): at the far pose, after the clean read, POST
water_render_inject {dry:true} - the renderer uploads an all-dry grid and holds it, a placement
change under a STILL camera. The probe re-reads the SAME pose and must report a source change
and/or rendered-vs-spans disagreements; if it reports a clean read, the probe is blind and the
run fails. (Until WaterCore Phase D5 the injection was water_look, which reverted the sheet to
the coarse bake upload; that upload - the universal water level - is deleted.)

    python tools/water_camera_probe.py coast                 # poses from game.json waterBench.cameraProbe
    python tools/water_camera_probe.py coast --rect 37 708 293 964 --eps 0.01
    python tools/water_camera_probe.py coast --inject-water-look   # run LAST (see below)

⚑ The self-test leaves the engine clean since Phase D5: the restore forces a span-grid rebuild. (Before
D5 the water_look reversion polluted the placement until the next residency change - 50,787
violations seen 2026-10-08 - and the self-test had to run last.)

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


def settle(api, max_s=240, rect=None):
    """Wait for streaming + remesh to go idle (the perf harness's settle keys), then - when a rect
    is given - for the RESIDENT CHUNK SET touching it to hold still across two reads 2 s apart,
    then the span grid's 30-frame cooldown. Bounded. (Reading while chunks were still landing
    produced 864 rendered!=spans false mismatches at the Coast near pose, 2026-10-08.)"""
    t0 = time.time()
    last = None
    while time.time() - t0 < max_s:
        try:
            st = api.get("/api/debug/load_state")
        except Exception:
            st = {}
        pend = sum(int(st.get(k, 0) or 0) for k in ("generation_pending", "remesh_pending", "remesh_idle_pending"))
        if pend == 0 and last == 0:
            break
        last = pend
        time.sleep(2.0)
    if rect is not None:
        x1, z1, x2, z2 = rect
        prev = None
        while time.time() - t0 < max_s:
            n = api.debug("water_spans_stored", {"x1": x1, "z1": z1, "x2": x2, "z2": z2}).get("chunks_loaded")
            if n == prev:
                break
            prev = n
            time.sleep(2.0)
        time.sleep(1.0)   # the span grid rebuilds at most every 30 frames after the last change
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
        self.resident3 = {(c[0], c[1], c[2]) for c in self.spans.get("resident_chunks", [])}
        self.source = self.grid.get("source")
        n = self.spans.get("spans") or 0
        # the world-data level band of this read (stored span tops), for the vertical residency test
        self.band = (self.spans["min_top"], self.spans["max_top"]) if n else None

    def is_resident(self, x, z, band=None):
        """band = (minLevel, maxLevel) world Y: every vertical chunk from floor((min-1)/32) to
        floor(max/32) must be loaded in this column. Without a band (no water seen at either pose)
        the 2-D column test is all there is."""
        cx, cz = x // 32, z // 32
        if band is None or not self.resident3:
            return (cx, cz) in self.resident
        lo, hi = int((band[0] - 1.0) // 32), int(band[1] // 32)
        return all((cx, cy, cz) in self.resident3 for cy in range(lo, hi + 1))

    def summary(self):
        return {"source": self.source, "rendered_wet": self.grid["wet"], "rendered_dry": self.grid["dry"],
                "off_grid": self.grid["off_grid"], "spans": self.spans.get("spans"),
                "span_chunks": self.spans.get("chunks_with_spans"), "resident_chunk_columns": len(self.resident),
                "min_level": self.grid["min_level"], "max_level": self.grid["max_level"],
                "span_columns_truncated": self.spans.get("span_columns_truncated"),
                "wet_columns_truncated": self.grid.get("wet_columns_truncated")}


def union_band(*reads):
    bands = [r.band for r in reads if r.band]
    return (min(b[0] for b in bands), max(b[1] for b in bands)) if bands else None


def rendered_vs_spans(read, rect, eps, band=None):
    """Resident columns where the renderer and the stored spans disagree."""
    x1, z1, x2, z2 = rect
    out = []
    for z in range(z1, z2 + 1):
        for x in range(x1, x2 + 1):
            if not read.is_resident(x, z, band):
                continue
            r, s = read.rendered.get((x, z)), read.stored.get((x, z))
            if (r is None) != (s is None):
                out.append((x, z, "rendered wet/dry != spans", r, s))
            elif r is not None and abs(r - s) > eps:
                out.append((x, z, "rendered level != span top", r, s))
    return out


def far_vs_near(rect, far, near, eps, band=None):
    x1, z1, x2, z2 = rect
    viol, cover, both = [], 0, 0
    for z in range(z1, z2 + 1):
        for x in range(x1, x2 + 1):
            rf, rn = far.is_resident(x, z, band), near.is_resident(x, z, band)
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
        waited = settle(api, rect=rect)
        rd = Read(api, rect)
        mism = rendered_vs_spans(rd, rect, args.eps, rd.band)
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
        api.debug("water_render_inject", {"dry": True})   # Phase D5: an all-dry grid held under a still camera
        seen = []
        rd2, mism2 = far, far_mism
        for _ in range(12):                               # the flip lands within a frame; poll 3 s
            time.sleep(0.25)
            rd2 = Read(api, rect)
            mism2 = rendered_vs_spans(rd2, rect, args.eps)
            seen.append(rd2.source)
            if rd2.source != far.source or len(mism2) > len(far_mism):
                break
        api.debug("water_render_inject", {"dry": False})
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
    band = union_band(far, near)                       # one vertical residency test for both poses
    far_mism = rendered_vs_spans(far, rect, args.eps, band)
    near_mism = rendered_vs_spans(near, rect, args.eps, band)
    viol, cover, both = far_vs_near(rect, far, near, args.eps, band)
    row["level_band"] = band
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
