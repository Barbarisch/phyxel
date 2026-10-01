#!/usr/bin/env python3
"""Import a UniMate-generated animated GLB/FBX as a clip into a Phyxel .anim rig.

UniMate (github.com/Friedrich-M/UniMate, text-to-motion for arbitrary skeletons)
writes its output as an animated rigged asset (stage 5 of its data pipeline,
``data_process/mesh_animation/animate_motion.py``). When that stage is driven with
the SAME source asset the Phyxel rig was built from (the Mixamo FBX behind
``humanoid.anim``), the clip already lives on the rig's bone names and bind pose, so
this importer is deliberately the same shape as ``tools/batch_import_mixamo.py``:

  1. GLB/FBX -> temporary .anim via ``tools/asset_pipeline/extract_animation.py --style box``
  2. bone-ID remap BY NAME onto the target rig; a low name-match ratio means the
     export did not preserve names and the import is REFUSED (``--min-match``)
  3. root motion: locomotion clips ship in place (pinned by
     tests/test_humanoid_anim_integrity.py) — Hips X/Z travel is stripped and the
     Speed line is set from the stance feet (anim_lint.foot_slide_metrics), which
     recovers every shipped mocap Speed within ~5%; ``--keep-root`` disables this
  4. provenance: a ``# clip_meta: <name> source=unimate ckpt= seed= cfg= rep=`` line
     (the engine splits values on whitespace, so the prompt goes in its own
     ``# unimate_prompt: <name> <text>`` header line) — a generated clip can never
     be mistaken for mocap later
  5. clip names MUST start with ``unimate_`` (``--force`` overrides) so a draft can
     never silently replace shipped mocap
  6. a JSON report next to the source (``<source>.import.json``) with match counts,
     slide metrics and anim_lint findings, so per-clip verdicts are machine-readable

Design + gates: docs/UniMateIntegrationPlan.md (M1).

Usage:
    python tools/anim_pipeline/unimate_import.py <clip.glb> --name unimate_walk_r0 \
        --prompt "a person walks forward" --seed 10 --cfg 3.0 --ckpt 100000 --rep 0 \
        [--target resources/animated_characters/humanoid.anim] [--dry-run]
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import AnimFile, Clip, Channel, parse, write  # noqa: E402
import anim_lint  # noqa: E402
import unimate_ledger  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
EXTRACT_SCRIPT = REPO / "tools" / "asset_pipeline" / "extract_animation.py"
DEFAULT_TARGET = REPO / "resources" / "animated_characters" / "humanoid.anim"
REQUIRED_PREFIX = "unimate_"
PROMPT_HEADER = "# unimate_prompt:"


class ImportRefused(Exception):
    pass


def convert_to_anim(src: Path, dst: Path, scale: float) -> None:
    cmd = [sys.executable, str(EXTRACT_SCRIPT), str(src), str(dst),
           "--style", "box", "--scale", str(scale)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout[-2000:] + "\n" + res.stderr[-2000:] + "\n")
        raise ImportRefused(f"extract_animation.py failed on {src}")


def build_bone_remap(source: AnimFile, target: AnimFile) -> dict:
    """source bone id -> target bone id, matched by exact bone name."""
    target_ids = target.bone_map()
    return {b.id: target_ids[b.name] for b in source.bones if b.name in target_ids}


def remap_clip(clip: Clip, remap: dict, new_name: str) -> tuple[Clip, list]:
    out = Clip(name=new_name, duration=clip.duration, speed=clip.speed,
               root_motion=clip.root_motion)
    dropped = []
    for ch in clip.channels:
        tid = remap.get(ch.bone_id)
        if tid is None:
            dropped.append(ch.bone_id)
            continue
        out.channels.append(Channel(bone_id=tid, pos_keys=list(ch.pos_keys),
                                    rot_keys=list(ch.rot_keys),
                                    scale_keys=list(ch.scale_keys)))
    return out, dropped


def root_travel(clip: Clip, root_id: int) -> tuple[float, float] | None:
    """(distance, mean speed) of the root bone's XZ travel over the clip, or None."""
    for ch in clip.channels:
        if ch.bone_id == root_id and len(ch.pos_keys) >= 2:
            (t0, p0), (t1, p1) = ch.pos_keys[0], ch.pos_keys[-1]
            dt = t1 - t0
            if dt <= 1e-6:
                return None
            dist = math.hypot(p1[0] - p0[0], p1[2] - p0[2])
            return dist, dist / dt
    return None


def strip_root_xz(clip: Clip, root_id: int) -> bool:
    """Pin the root bone's X/Z to its first key (keep Y). Returns True if changed."""
    for ch in clip.channels:
        if ch.bone_id == root_id and ch.pos_keys:
            x0, _, z0 = ch.pos_keys[0][1]
            ch.pos_keys = [(t, (x0, p[1], z0)) for t, p in ch.pos_keys]
            return True
    return False


def set_prompt_header(af: AnimFile, clip_name: str, prompt: str) -> None:
    line = f"{PROMPT_HEADER} {clip_name} {prompt}"
    prefix = f"{PROMPT_HEADER} {clip_name} "
    for i, h in enumerate(af.header_comments):
        if h.startswith(prefix):
            af.header_comments[i] = line
            return
    af.header_comments.append(line)


def import_clip(source_path: Path, target_path: Path, name: str, *,
                scale: float = 1.0, speed: float | None = None, keep_root: bool = False,
                root_bone: str = "mixamorig:Hips", min_match: float = 0.9,
                force: bool = False, prompt: str = "", seed=None, cfg=None, ckpt=None, rep=None,
                dry_run: bool = False,
                ledger_path: Path | None = unimate_ledger.DEFAULT_LEDGER) -> dict:
    """Import one clip. Returns the report dict; raises ImportRefused on any gate."""
    if not source_path.exists():
        raise ImportRefused(f"missing source: {source_path}")
    if not name.startswith(REQUIRED_PREFIX) and not force:
        raise ImportRefused(
            f"clip name {name!r} must start with {REQUIRED_PREFIX!r} — generated drafts are "
            f"namespaced so they can never replace shipped mocap (use --force to override)")
    target = parse(target_path)

    with tempfile.TemporaryDirectory() as td:
        tmp_anim = Path(td) / "unimate_clip.anim"
        convert_to_anim(source_path, tmp_anim, scale)
        source = parse(tmp_anim)
    if not source.clips:
        raise ImportRefused("source has no animation clips")

    src_clip = source.clips[0]
    remap = build_bone_remap(source, target)
    clip, dropped = remap_clip(src_clip, remap, name)
    match = len(remap) / max(1, len(source.bones))
    report = {
        "source": str(source_path), "target": str(target_path), "clip": name,
        "source_clip": src_clip.name, "duration_s": round(src_clip.duration, 4),
        "source_bones": len(source.bones), "matched_bones": len(remap),
        "match_ratio": round(match, 4),
        "dropped_source_bones": [source.bones[i].name for i in dropped],
        "driven_target_bones": len({ch.bone_id for ch in clip.channels}),
        "provenance": {"source": "unimate", "ckpt": ckpt, "seed": seed, "cfg": cfg,
                       "rep": rep, "prompt": prompt},
    }
    if match < min_match:
        report["refused"] = (f"only {match:.0%} of source bones matched target names "
                             f"(< {min_match:.0%}); bone names did not survive the export")
        raise ImportRefused(report["refused"])

    # --- root motion -> Speed -------------------------------------------------
    # extract_animation.py already detects a travelling root, STRIPS its X/Z and writes
    # the travel speed as the clip's Speed line (+ RootMotion flags) — so by the time the
    # clip reaches us, clip.speed IS the root-travel speed and the hips are in place.
    # The strip below is only a fallback for extractors that do not do that.
    root_id = target.bone_map().get(root_bone)
    extractor_speed = clip.speed if clip.speed else None
    travel = root_travel(clip, root_id) if root_id is not None else None
    stripped = False
    if travel and travel[0] > 0.02 and not keep_root:
        stripped = strip_root_xz(clip, root_id)
        if extractor_speed is None:
            extractor_speed = travel[1]
    report["root_travel"] = ({"speed": round(extractor_speed, 4),
                              "source": "extract_animation" if not stripped else "importer_strip"}
                             if extractor_speed else None)
    report["root_xz_stripped"] = stripped or (extractor_speed is not None)

    # Stance-foot estimate of the body speed the clip was authored for.
    probe = AnimFile(header_comments=list(target.header_comments), bones=target.bones,
                     boxes=target.boxes, clips=[clip])
    slide = anim_lint.foot_slide_metrics(probe, clip)
    report["stance_estimate"] = ({k: (round(v, 4) if isinstance(v, float) else v)
                                  for k, v in slide.items() if k != "stance_frac"}
                                 if "error" not in slide else slide)
    est = slide.get("est_speed", 0.0) if "error" not in slide else 0.0
    stance_ok = est >= 0.05 and slide.get("stance_samples", 0) >= anim_lint.MIN_STANCE_SAMPLES
    if speed is not None:
        clip.speed = speed
        report["speed_source"] = "explicit"
    elif extractor_speed is not None:
        # A travelling clip. For LOCOMOTION the stance feet are the ground truth for the
        # speed the controller must use (a root that outruns the feet = skating); keep
        # the root-travel figure in the report so the disagreement stays visible. For
        # actions (sword swings, jumps) the small root drift is kept as an authored
        # offset exactly like the shipped mocap imports (sword1h_* Speed 0.256).
        if stance_ok and anim_lint.is_locomotion_clip(probe, clip):
            clip.speed = round(est, 3)
            report["speed_source"] = "stance_feet"
        else:
            clip.speed = round(extractor_speed, 3)
            report["speed_source"] = "root_travel"
        report["root_vs_feet_mismatch"] = (round(abs(extractor_speed - est) / extractor_speed, 4)
                                           if stance_ok else None)
    else:
        clip.speed = None
        report["speed_source"] = "none (in-place clip)"
    report["speed"] = clip.speed

    # --- provenance -----------------------------------------------------------
    meta = {"source": "unimate"}
    for k, v in (("ckpt", ckpt), ("seed", seed), ("cfg", cfg), ("rep", rep)):
        if v is not None:
            meta[k] = v
    target.set_clip_meta(name, meta)
    if prompt:
        set_prompt_header(target, name, prompt)

    # --- lint on the target rig (rotations were never altered, only remapped) ---
    # Calibrated envelope (per-bone peak angular velocity/accel from vetted mocap) when
    # the repo's calibration.json is present; absolute checks + slide gate always.
    cal_path = Path(__file__).parent / "calibration.json"
    calibration = (json.loads(cal_path.read_text(encoding="utf-8"))
                   if cal_path.exists() else None)
    findings = anim_lint.lint_clip(probe, clip, calibration)
    report["calibrated"] = calibration is not None
    report["lint"] = [{"severity": s, "message": m} for s, m in findings]
    report["lint_errors"] = sum(1 for s, _ in findings if s == "ERROR")

    if dry_run:
        report["written"] = False
        return report
    replaced = target.set_clip(clip)
    write(target, target_path)
    report["written"] = True
    report["replaced_existing"] = replaced

    # Review ledger: every written generated clip gets a `pending` entry the editor panel and
    # unimate_promote.py work from (docs/UniMateIntegrationPlan.md M1b).
    if ledger_path is not None:
        ledger = unimate_ledger.load(ledger_path)
        unimate_ledger.upsert_pending(
            ledger, name, unimate_ledger.entry_from_import_report(report, target_path.name))
        unimate_ledger.save(ledger, ledger_path)
        report["ledger"] = str(ledger_path)
    return report


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("source", type=Path, help="animated GLB/FBX from UniMate stage 5")
    ap.add_argument("--name", required=True, help=f"clip name; must start with {REQUIRED_PREFIX!r}")
    ap.add_argument("--target", type=Path, default=DEFAULT_TARGET)
    ap.add_argument("--scale", type=float, default=1.0,
                    help="extract_animation --scale (keep equal to the rig's original import)")
    ap.add_argument("--speed", type=float, default=None, help="explicit Speed line (units/s)")
    ap.add_argument("--keep-root", action="store_true",
                    help="keep Hips X/Z root motion (default: strip it and set Speed from the stance feet)")
    ap.add_argument("--root-bone", default="mixamorig:Hips")
    ap.add_argument("--min-match", type=float, default=0.9,
                    help="refuse if fewer than this fraction of source bones match target names")
    ap.add_argument("--force", action="store_true", help=f"allow a name without the {REQUIRED_PREFIX!r} prefix")
    ap.add_argument("--prompt", default="", help="generation prompt (provenance)")
    ap.add_argument("--seed", type=int)
    ap.add_argument("--cfg", type=float)
    ap.add_argument("--ckpt", type=int, help="checkpoint step")
    ap.add_argument("--rep", type=int, help="repetition index")
    ap.add_argument("--report", type=Path, help="JSON report path (default: <source>.import.json)")
    ap.add_argument("--ledger", type=Path, default=unimate_ledger.DEFAULT_LEDGER,
                    help="review ledger to add the pending entry to")
    ap.add_argument("--dry-run", action="store_true", help="report only, do not write the target")
    args = ap.parse_args()

    report_path = args.report or args.source.with_suffix(args.source.suffix + ".import.json")
    try:
        report = import_clip(args.source, args.target, args.name, scale=args.scale,
                             speed=args.speed, keep_root=args.keep_root, root_bone=args.root_bone,
                             min_match=args.min_match, force=args.force, prompt=args.prompt,
                             seed=args.seed, cfg=args.cfg, ckpt=args.ckpt, rep=args.rep,
                             dry_run=args.dry_run, ledger_path=args.ledger)
    except ImportRefused as exc:
        print(f"REFUSED: {exc}")
        return 2
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"clip '{report['clip']}': {report['duration_s']}s, bone match "
          f"{report['matched_bones']}/{report['source_bones']} ({report['match_ratio']:.0%}), "
          f"root xz stripped={report['root_xz_stripped']}, Speed={report['speed']} "
          f"({report['speed_source']}), lint errors={report['lint_errors']}")
    for f in report["lint"]:
        print(f"    {f['severity']}: {f['message']}")
    print(("dry run — target not modified" if args.dry_run else
           f"{'replaced' if report.get('replaced_existing') else 'added'} clip in {args.target}")
          + f"; report -> {report_path}")
    return 1 if report["lint_errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
