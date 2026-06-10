"""Offline quality linter for Phyxel .anim clips.

Two layers of checks:

1. Absolute (always on) — mechanical correctness:
   - rotation keys are unit quaternions
   - no raw quaternion sign flips between consecutive keys (dot < 0)
   - no ambiguous key segments (geodesic angle > 120 deg between
     consecutive keys — slerp direction becomes unpredictable)
   - keys sorted in time, within [0, duration]

2. Calibrated (needs a calibration JSON built from known-good clips) —
   motion naturalness envelope:
   - per-bone peak angular velocity vs the good-clip envelope
   - per-bone peak angular acceleration (pop detector)
   - hips linear velocity

Workflow:
   python anim_lint.py calibrate humanoid.anim --out calibration.json
   python anim_lint.py lint humanoid.anim --clips cast_standard --calibration calibration.json
   python anim_lint.py report humanoid.anim --clips walk        # raw metrics, no judgement
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import parse, AnimFile, Clip  # noqa: E402

SAMPLE_HZ = 60.0
AMBIGUOUS_SEGMENT_DEG = 120.0
NORM_TOL = 1e-3
# Default calibration clip set: user-vetted good clips spanning slow (idle),
# locomotion (walk/run/fast_run), and fast deliberate arm action (attack, boxing, point).
DEFAULT_GOOD_CLIPS = ["idle", "walk", "run", "fast_run", "attack", "boxing", "point", "wave"]
# Safety headroom over the good-clip envelope before a metric becomes a finding.
ENVELOPE_FACTOR = 1.5


# ---------------------------------------------------------------------------
# Quaternion helpers (x, y, z, w order, matching the .anim file)
# ---------------------------------------------------------------------------

def qdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]


def qnorm(q):
    return math.sqrt(qdot(q, q))


def qangle(a, b):
    """Geodesic angle (radians) between two unit quaternions, short path."""
    d = min(1.0, abs(qdot(a, b)))
    return 2.0 * math.acos(d)


def qslerp(a, b, t):
    """Shortest-path slerp, mirroring glm::slerp used by the engine."""
    d = qdot(a, b)
    if d < 0.0:
        b = tuple(-x for x in b)
        d = -d
    if d > 0.9995:  # nearly parallel: nlerp
        out = tuple(a[i] + t * (b[i] - a[i]) for i in range(4))
        n = qnorm(out)
        return tuple(x / n for x in out) if n > 0 else a
    theta = math.acos(min(1.0, d))
    s = math.sin(theta)
    wa = math.sin((1.0 - t) * theta) / s
    wb = math.sin(t * theta) / s
    return tuple(wa * a[i] + wb * b[i] for i in range(4))


def sample_rotation(keys, t):
    """Engine-equivalent rotation sampling: clamp outside, slerp between."""
    if not keys:
        return (0.0, 0.0, 0.0, 1.0)
    if len(keys) == 1 or t <= keys[0][0]:
        return keys[0][1]
    if t >= keys[-1][0]:
        return keys[-1][1]
    for i in range(len(keys) - 1):
        if t < keys[i + 1][0]:
            t0, q0 = keys[i]
            t1, q1 = keys[i + 1]
            f = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
            return qslerp(q0, q1, f)
    return keys[-1][1]


def sample_position(keys, t):
    if not keys:
        return (0.0, 0.0, 0.0)
    if len(keys) == 1 or t <= keys[0][0]:
        return keys[0][1]
    if t >= keys[-1][0]:
        return keys[-1][1]
    for i in range(len(keys) - 1):
        if t < keys[i + 1][0]:
            t0, p0 = keys[i]
            t1, p1 = keys[i + 1]
            f = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
            return tuple(p0[j] + f * (p1[j] - p0[j]) for j in range(3))
    return keys[-1][1]


def vdist(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


# ---------------------------------------------------------------------------
# Per-clip metrics
# ---------------------------------------------------------------------------

def clip_metrics(af: AnimFile, clip: Clip) -> dict:
    """Compute raw quality metrics for one clip. Returns a dict:
    {
      'bones': { bone_name: {'peak_ang_vel': deg/s, 'peak_ang_acc': deg/s^2,
                              'loop_gap_deg': deg} },
      'hips_peak_lin_vel': units/s,
      'issues': [ (severity, message) ]   # absolute findings only
    }
    """
    issues = []
    bones = {}
    hips_peak_lin_vel = 0.0
    dt = 1.0 / SAMPLE_HZ

    for ch in clip.channels:
        bone_name = af.bones[ch.bone_id].name if ch.bone_id < len(af.bones) else f"bone_{ch.bone_id}"
        short = bone_name.split(":")[-1]

        # --- absolute key checks -----------------------------------------
        for t, q in ch.rot_keys:
            n = qnorm(q)
            if abs(n - 1.0) > NORM_TOL:
                issues.append(("ERROR", f"{short}: non-unit quaternion at t={t:.3f} (|q|={n:.4f})"))
        prev_t = -1.0
        for t, _ in ch.rot_keys:
            if t < prev_t:
                issues.append(("ERROR", f"{short}: rotation keys not sorted at t={t:.3f}"))
            if t < -1e-6 or t > clip.duration + 1e-3:
                issues.append(("ERROR", f"{short}: key time {t:.3f} outside clip duration {clip.duration:.3f}"))
            prev_t = t
        for i in range(len(ch.rot_keys) - 1):
            t0, q0 = ch.rot_keys[i]
            t1, q1 = ch.rot_keys[i + 1]
            # Raw sign flips (dot < 0) are NOT reported: the engine's slerp is
            # shortest-path, so they play back correctly. Only the geodesic
            # segment angle below matters.
            seg = math.degrees(qangle(q0, q1))
            if seg > AMBIGUOUS_SEGMENT_DEG:
                issues.append(("ERROR",
                               f"{short}: {seg:.0f} deg rotation in one key segment "
                               f"(t={t0:.3f}->{t1:.3f}) — slerp direction ambiguous, add a midpoint key"))

        # --- sampled motion metrics ----------------------------------------
        if len(ch.rot_keys) >= 2 and clip.duration > 0:
            n_samples = max(2, int(clip.duration * SAMPLE_HZ) + 1)
            qs = [sample_rotation(ch.rot_keys, i * dt) for i in range(n_samples)]
            vels = [math.degrees(qangle(qs[i], qs[i + 1])) / dt for i in range(len(qs) - 1)]
            peak_vel = max(vels) if vels else 0.0
            peak_acc = max((abs(vels[i + 1] - vels[i]) / dt for i in range(len(vels) - 1)), default=0.0)
            loop_gap = math.degrees(qangle(ch.rot_keys[0][1], ch.rot_keys[-1][1]))
            entry = bones.setdefault(bone_name, {})
            entry["peak_ang_vel"] = max(entry.get("peak_ang_vel", 0.0), peak_vel)
            entry["peak_ang_acc"] = max(entry.get("peak_ang_acc", 0.0), peak_acc)
            entry["loop_gap_deg"] = max(entry.get("loop_gap_deg", 0.0), loop_gap)

        # --- hips linear velocity -----------------------------------------
        if "Hips" in bone_name and len(ch.pos_keys) >= 2 and clip.duration > 0:
            n_samples = max(2, int(clip.duration * SAMPLE_HZ) + 1)
            ps = [sample_position(ch.pos_keys, i * dt) for i in range(n_samples)]
            for i in range(len(ps) - 1):
                hips_peak_lin_vel = max(hips_peak_lin_vel, vdist(ps[i], ps[i + 1]) / dt)

    return {"bones": bones, "hips_peak_lin_vel": hips_peak_lin_vel, "issues": issues}


# ---------------------------------------------------------------------------
# Calibration
# ---------------------------------------------------------------------------

def build_calibration(af: AnimFile, clip_names) -> dict:
    """Envelope of per-bone peaks across the given known-good clips."""
    env = {}  # bone -> {peak_ang_vel, peak_ang_acc}
    hips_lin = 0.0
    used = []
    for name in clip_names:
        clip = af.clip(name)
        if clip is None:
            print(f"  (calibration clip '{name}' not found — skipped)")
            continue
        used.append(name)
        m = clip_metrics(af, clip)
        hips_lin = max(hips_lin, m["hips_peak_lin_vel"])
        for bone, stats in m["bones"].items():
            e = env.setdefault(bone, {"peak_ang_vel": 0.0, "peak_ang_acc": 0.0})
            e["peak_ang_vel"] = max(e["peak_ang_vel"], stats["peak_ang_vel"])
            e["peak_ang_acc"] = max(e["peak_ang_acc"], stats["peak_ang_acc"])
    return {
        "source_clips": used,
        "sample_hz": SAMPLE_HZ,
        "envelope_factor": ENVELOPE_FACTOR,
        "hips_peak_lin_vel": hips_lin,
        "bones": env,
    }


def lint_clip(af: AnimFile, clip: Clip, calibration: dict = None, looping: bool = False) -> list:
    """Returns findings: [(severity, message)]. Absolute checks + calibrated envelope."""
    m = clip_metrics(af, clip)
    findings = list(m["issues"])

    if looping:
        for bone, stats in m["bones"].items():
            gap = stats.get("loop_gap_deg", 0.0)
            if gap > 15.0:
                findings.append(("ERROR", f"{bone.split(':')[-1]}: loop gap {gap:.0f} deg (first vs last key)"))
            elif gap > 5.0:
                findings.append(("WARN", f"{bone.split(':')[-1]}: loop gap {gap:.0f} deg (first vs last key)"))

    if calibration:
        factor = calibration.get("envelope_factor", ENVELOPE_FACTOR)
        cal_bones = calibration["bones"]
        # Fallback envelope for bones absent from calibration: global max across calibrated bones.
        global_vel = max((b["peak_ang_vel"] for b in cal_bones.values()), default=720.0)
        global_acc = max((b["peak_ang_acc"] for b in cal_bones.values()), default=20000.0)
        for bone, stats in m["bones"].items():
            cal = cal_bones.get(bone)
            limit_vel = (cal["peak_ang_vel"] if cal else global_vel) * factor
            limit_acc = (cal["peak_ang_acc"] if cal else global_acc) * factor
            short = bone.split(":")[-1]
            if stats["peak_ang_vel"] > limit_vel:
                findings.append(("WARN",
                                 f"{short}: peak angular velocity {stats['peak_ang_vel']:.0f} deg/s "
                                 f"exceeds good-clip envelope ({limit_vel:.0f})"))
            if stats["peak_ang_acc"] > limit_acc:
                findings.append(("WARN",
                                 f"{short}: peak angular accel {stats['peak_ang_acc']:.0f} deg/s^2 "
                                 f"exceeds good-clip envelope ({limit_acc:.0f}) — possible pop"))
        cal_hips = calibration.get("hips_peak_lin_vel", 0.0)
        if cal_hips > 0 and m["hips_peak_lin_vel"] > cal_hips * factor:
            findings.append(("WARN",
                             f"Hips: peak linear velocity {m['hips_peak_lin_vel']:.2f} u/s "
                             f"exceeds envelope ({cal_hips * factor:.2f})"))
    return findings


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _select_clips(af: AnimFile, clips_arg):
    if clips_arg:
        names = clips_arg.split(",")
        missing = [n for n in names if af.clip(n) is None]
        if missing:
            print(f"clips not found: {missing}")
            sys.exit(1)
        return [af.clip(n) for n in names]
    return af.clips


def _cmd_calibrate(args):
    af = parse(args.file)
    clip_names = args.clips.split(",") if args.clips else DEFAULT_GOOD_CLIPS
    print(f"calibrating from: {clip_names}")
    cal = build_calibration(af, clip_names)
    Path(args.out).write_text(json.dumps(cal, indent=2), encoding="utf-8")
    print(f"wrote {args.out} ({len(cal['bones'])} bones, "
          f"hips lin vel {cal['hips_peak_lin_vel']:.2f} u/s)")
    return 0


def _cmd_lint(args):
    af = parse(args.file)
    calibration = None
    if args.calibration:
        calibration = json.loads(Path(args.calibration).read_text(encoding="utf-8"))
    clips = _select_clips(af, args.clips)
    total_errors = 0
    for clip in clips:
        findings = lint_clip(af, clip, calibration, looping=args.looping)
        errors = sum(1 for s, _ in findings if s == "ERROR")
        warns = sum(1 for s, _ in findings if s == "WARN")
        total_errors += errors
        status = "FAIL" if errors else ("WARN" if warns else "PASS")
        print(f"[{status}] {clip.name} ({clip.duration:.2f}s): {errors} errors, {warns} warnings")
        shown = findings if args.all else findings[:15]
        for sev, msg in shown:
            print(f"    {sev}: {msg}")
        if len(findings) > len(shown):
            print(f"    ... {len(findings) - len(shown)} more (use --all)")
    return 1 if total_errors else 0


def _cmd_report(args):
    af = parse(args.file)
    clips = _select_clips(af, args.clips)
    for clip in clips:
        m = clip_metrics(af, clip)
        print(f"{clip.name} ({clip.duration:.2f}s, {len(clip.channels)} channels)")
        print(f"  hips peak linear velocity: {m['hips_peak_lin_vel']:.2f} u/s")
        rows = sorted(m["bones"].items(), key=lambda kv: -kv[1]["peak_ang_vel"])
        for bone, stats in rows[: args.top]:
            print(f"  {bone.split(':')[-1]:24s} vel {stats['peak_ang_vel']:7.0f} deg/s   "
                  f"acc {stats['peak_ang_acc']:9.0f} deg/s^2   loop gap {stats['loop_gap_deg']:5.1f} deg")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="Phyxel .anim quality linter")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("calibrate", help="build per-bone motion envelope from known-good clips")
    p.add_argument("file")
    p.add_argument("--clips", help=f"comma-separated good clips (default: {','.join(DEFAULT_GOOD_CLIPS)})")
    p.add_argument("--out", default="tools/anim_pipeline/calibration.json")
    p.set_defaults(fn=_cmd_calibrate)

    p = sub.add_parser("lint", help="lint clips (absolute checks + optional calibrated envelope)")
    p.add_argument("file")
    p.add_argument("--clips", help="comma-separated clip names (default: all)")
    p.add_argument("--calibration", help="calibration JSON from the calibrate command")
    p.add_argument("--looping", action="store_true", help="also require loop closure (first==last pose)")
    p.add_argument("--all", action="store_true", help="show all findings, not just the first 15")
    p.set_defaults(fn=_cmd_lint)

    p = sub.add_parser("report", help="print raw per-bone metrics, no judgement")
    p.add_argument("file")
    p.add_argument("--clips", help="comma-separated clip names (default: all)")
    p.add_argument("--top", type=int, default=10, help="show top-N fastest bones")
    p.set_defaults(fn=_cmd_report)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
