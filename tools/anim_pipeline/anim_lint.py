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
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import parse, AnimFile, Clip  # noqa: E402
import clip_meta_schema  # noqa: E402  (A3 item 1: shared typed schema)

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

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qrot(q, v):
    """Rotate vector v by unit quaternion q (x, y, z, w)."""
    qx, qy, qz, qw = q
    x, y, z = v
    # t = 2 * cross(q.xyz, v)
    tx, ty, tz = 2 * (qy * z - qz * y), 2 * (qz * x - qx * z), 2 * (qx * y - qy * x)
    # v' = v + w*t + cross(q.xyz, t)
    return (x + qw * tx + (qy * tz - qz * ty),
            y + qw * ty + (qz * tx - qx * tz),
            z + qw * tz + (qx * ty - qy * tx))


def pose_world_positions(af: AnimFile, clip: Clip, t: float) -> dict:
    """{bone_id: world (x, y, z)} at time t: sampled channels, bind where un-keyed."""
    local = {b.id: (tuple(b.pos), tuple(b.rot)) for b in af.bones}
    for ch in clip.channels:
        p0, r0 = local[ch.bone_id]
        p = sample_position(ch.pos_keys, t) if ch.pos_keys else p0
        r = sample_rotation(ch.rot_keys, t) if ch.rot_keys else r0
        local[ch.bone_id] = (tuple(p), tuple(r))
    gp, gr = {}, {}
    for b in af.bones:  # bones are parent-before-child in .anim files
        lp, lr = local[b.id]
        if b.parent_id < 0:
            gp[b.id], gr[b.id] = lp, lr
        else:
            pp, pr = gp[b.parent_id], gr[b.parent_id]
            rp = qrot(pr, lp)
            gp[b.id] = (pp[0] + rp[0], pp[1] + rp[1], pp[2] + rp[2])
            gr[b.id] = qmul(pr, lr)
    return gp


# Foot-slide metric. Locomotion clips ship IN PLACE (Hips X/Z stripped —
# tests/test_humanoid_anim_integrity.py) and the controller translates the body
# at the clip's Speed. A planted foot therefore moves at -bodyVelocity in
# character space during stance, so the stance feet MEASURE the body velocity
# the clip was authored for: est = -(mean stance-foot XZ velocity). Two checks:
#   speed mismatch  |est| vs the Speed line (a wrong Speed line = skating feet)
#   residual        spread of stance velocities around the mean (jitter/skate
#                   inside the clip itself, independent of Speed)
# Direction is measured, not assumed: strafes travel along X, backward along -Z.
# Stance = toe within STANCE_HEIGHT of the clip's lowest toe sample. 0.06 was
# measured to admit swing samples (walk est 0.70 vs authored 1.68); 0.03
# recovers every shipped locomotion Speed within ~5% (walk 1.67/1.68, run
# 3.66/3.77, fast_run 5.41/5.66, strafes 3.37/3.62). Added 2026-09-29 as the
# red-first gate for generated (UniMate) drafts — docs/UniMateIntegrationPlan.md M1.
STANCE_HEIGHT = 0.03          # rig units (~m)
FOOT_SUFFIXES = ("ToeBase", "Foot")
SPEED_MISMATCH_WARN = 0.25    # |est - Speed| / Speed   (shipped cyclic clips: 1-14%)
SPEED_MISMATCH_ERROR = 0.50
RESIDUAL_RATIO_WARN = 0.40    # mean residual / est speed (shipped cyclic clips: 0.05-0.37,
                              # walking_backward is the 0.37)
MIN_STANCE_SAMPLES = 6
# The gate judges CYCLIC locomotion only. Many action clips carry a small Speed
# line as a root-travel offset (sword combos 0.256, jump_down 0.357, deaths) and
# transitions (run_to_stop, crouch_to_stand) decelerate by design — their stance
# feet cannot agree with a constant Speed and are not meant to.
LOCOMOTION_NAME_RE = re.compile(r"(walk|run|strafe|jog|sprint|gallop|trot)", re.I)
TRANSITION_NAME_RE = re.compile(r"(start|stop|_to_)", re.I)
NON_LOCOMOTION_TYPES = {"combat", "jump", "stair", "reaction", "death", "interaction"}


def is_locomotion_clip(af: AnimFile, clip: Clip) -> bool:
    meta = af.clip_meta(clip.name) or {}
    ctype = str(meta.get("type", "")).lower()
    if ctype == "locomotion":
        return True
    if ctype in NON_LOCOMOTION_TYPES:
        return False
    if TRANSITION_NAME_RE.search(clip.name):
        return False
    return bool(LOCOMOTION_NAME_RE.search(clip.name))


def find_feet(af: AnimFile) -> dict:
    """{'L': bone_id, 'R': bone_id} preferring *ToeBase, else *Foot."""
    feet = {}
    for suffix in FOOT_SUFFIXES:
        for b in af.bones:
            short = b.name.split(":")[-1]
            if not short.endswith(suffix):
                continue
            side = "L" if short.startswith("Left") or short.startswith("L_") else \
                   "R" if short.startswith("Right") or short.startswith("R_") else None
            if side and side not in feet:
                feet[side] = b.id
        if len(feet) == 2:
            break
    return feet


def foot_slide_metrics(af: AnimFile, clip: Clip) -> dict:
    """Body velocity implied by the stance feet, and the residual skate.

    Returns {
      'body_vel': (vx, vz)   estimated character-space body velocity, units/s
      'est_speed': float     |body_vel|
      'residual': float      mean |v_stance - mean| over stance samples, units/s
      'stance_samples': int, 'stance_frac': {'L': f, 'R': f},
      'speed': clip.speed or None,
      'mismatch': |est - Speed| / Speed   (None when no Speed line)
    } or {'error': msg} when the rig has no identifiable feet.
    """
    feet = find_feet(af)
    if len(feet) < 2:
        return {"error": "rig has no Left/Right *ToeBase or *Foot bones"}
    dt = 1.0 / SAMPLE_HZ
    n = max(3, int(clip.duration * SAMPLE_HZ) + 1)
    tracks = {side: [] for side in feet}
    for i in range(n):
        gp = pose_world_positions(af, clip, min(i * dt, clip.duration))
        for side, bid in feet.items():
            tracks[side].append(gp[bid])
    vels, stance_frac = [], {}
    for side, pts in tracks.items():
        min_y = min(p[1] for p in pts)
        stance = [p[1] < min_y + STANCE_HEIGHT for p in pts]
        stance_frac[side] = sum(stance) / len(stance)
        for i in range(len(pts) - 1):
            if stance[i] and stance[i + 1]:
                vels.append(((pts[i + 1][0] - pts[i][0]) / dt,
                             (pts[i + 1][2] - pts[i][2]) / dt))
    out = {"speed": clip.speed or None, "stance_samples": len(vels), "stance_frac": stance_frac}
    if not vels:
        out.update(body_vel=(0.0, 0.0), est_speed=0.0, residual=0.0, mismatch=None)
        return out
    mx = sum(v[0] for v in vels) / len(vels)
    mz = sum(v[1] for v in vels) / len(vels)
    est = math.hypot(mx, mz)
    resid = sum(math.hypot(v[0] - mx, v[1] - mz) for v in vels) / len(vels)
    out.update(body_vel=(-mx, -mz), est_speed=est, residual=resid,
               mismatch=(abs(est - clip.speed) / clip.speed) if clip.speed else None)
    return out


def stance_markers(af: AnimFile, clip: Clip) -> dict:
    """Gait phase markers for a cyclic locomotion clip: the normalized time (0-1) at which each
    foot ENTERS stance (first sample inside the stance band after a swing), keyed 'L'/'R'.

    Written to clip_meta as stanceL/stanceR (A3 transition graph); the engine enters a
    phase-synced transition so the new clip's left plant lines up with the old clip's.
    Returns {'L': t, 'R': t} (a side is omitted when that foot never leaves the band — no
    swing means no plant event) or {'error': msg}.
    """
    feet = find_feet(af)
    if len(feet) < 2:
        return {"error": "rig has no Left/Right *ToeBase or *Foot bones"}
    dt = 1.0 / SAMPLE_HZ
    n = max(3, int(clip.duration * SAMPLE_HZ) + 1)
    tracks = {side: [] for side in feet}
    for i in range(n):
        gp = pose_world_positions(af, clip, min(i * dt, clip.duration))
        for side, bid in feet.items():
            tracks[side].append(gp[bid][1])
    out = {}
    for side, ys in tracks.items():
        min_y = min(ys)
        stance = [y < min_y + STANCE_HEIGHT for y in ys]
        # A foot can enter the band twice per cycle (heel-toe roll lifts it out mid-stance),
        # so "first entry" is not the plant. The plant is the entry that begins the LONGEST
        # cyclic stance run. Walk read 0.26 cycle between feet with first-entry; longest-run
        # reads ~0.5, as a symmetric gait must.
        n_s = len(stance)
        if all(stance) or not any(stance):
            continue                      # never leaves / never enters the band: no plant event
        best_start, best_len = None, -1
        for i in range(n_s):
            prev = stance[i - 1] if i > 0 else stance[-1]
            if stance[i] and not prev:
                run = 0
                while run < n_s and stance[(i + run) % n_s]:
                    run += 1
                if run > best_len:
                    best_start, best_len = i, run
        if best_start is not None:
            out[side] = round(min(best_start * dt, clip.duration) / clip.duration, 4) if clip.duration > 0 else 0.0
    return out


def foot_slide_findings(af: AnimFile, clip: Clip) -> list:
    """Lint findings for a cyclic locomotion clip with a Speed line."""
    if not clip.speed or not is_locomotion_clip(af, clip):
        return []
    m = foot_slide_metrics(af, clip)
    if "error" in m:
        return [("WARN", f"foot slide not measured: {m['error']}")]
    if m["stance_samples"] < MIN_STANCE_SAMPLES:
        return [("WARN", f"foot slide not measured: only {m['stance_samples']} stance samples "
                         f"(toes never within {STANCE_HEIGHT} of their lowest point)")]
    findings = []
    vx, vz = m["body_vel"]
    desc = (f"stance feet imply body velocity ({vx:+.2f}, {vz:+.2f}) = {m['est_speed']:.3f} u/s "
            f"vs Speed {clip.speed:.3f} ({m['mismatch']:.0%} off)")
    if m["mismatch"] > SPEED_MISMATCH_ERROR:
        findings.append(("ERROR", desc + " — foot skates; the Speed line does not match the clip"))
    elif m["mismatch"] > SPEED_MISMATCH_WARN:
        findings.append(("WARN", desc))
    if m["est_speed"] > 1e-6 and m["residual"] / m["est_speed"] > RESIDUAL_RATIO_WARN:
        findings.append(("WARN", f"stance velocity residual {m['residual']:.3f} u/s = "
                                 f"{m['residual'] / m['est_speed']:.0%} of body speed — feet jitter/skate in place"))
    return findings


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
    findings.extend(foot_slide_findings(af, clip))
    # A3 item 1: the clip_meta line must satisfy the shared typed schema.
    findings.extend(clip_meta_schema.validate_meta(af.clip_meta(clip.name) or {}))

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


def _cmd_slide(args):
    af = parse(args.file)
    clips = _select_clips(af, args.clips) if args.clips else [c for c in af.clips if c.speed]
    for clip in clips:
        m = foot_slide_metrics(af, clip)
        if "error" in m:
            print(f"{clip.name}: {m['error']}")
            continue
        vx, vz = m["body_vel"]
        spd = f"{m['speed']:.3f}" if m["speed"] else "  -  "
        mis = f"{m['mismatch']:.0%}" if m["mismatch"] is not None else "  -"
        print(f"{clip.name:24s} Speed {spd}  est {m['est_speed']:.3f} ({vx:+.2f},{vz:+.2f})  "
              f"mismatch {mis:>4s}  residual {m['residual']:.3f}  "
              f"stance L {m['stance_frac'].get('L', 0):.0%} R {m['stance_frac'].get('R', 0):.0%} n={m['stance_samples']}")
    return 0


def _cmd_stance(args):
    """Report (and with --write, store) stanceL/stanceR markers for locomotion clips."""
    from anim_format import write as write_anim
    af = parse(args.file)
    clips = _select_clips(af, args.clips)
    changed = 0
    for clip in clips:
        if not is_locomotion_clip(af, clip):
            # Combat/cast/draft clips carry Speed lines too (root travel), but a gait phase is
            # meaningless there — strip stale markers so a phase-synced edge can never pick them.
            meta = af.clip_meta(clip.name) or {}
            if args.write and ("stanceL" in meta or "stanceR" in meta):
                meta.pop("stanceL", None); meta.pop("stanceR", None)
                af.set_clip_meta(clip.name, meta)
                changed += 1
                print(f"[PRUNE] {clip.name}: not a locomotion clip — stance markers removed")
            continue
        m = stance_markers(af, clip)
        if "error" in m:
            print(f"[SKIP] {clip.name}: {m['error']}")
            continue
        if "L" not in m or "R" not in m:
            print(f"[WARN] {clip.name}: a foot never leaves the stance band (L={m.get('L')} R={m.get('R')}) — not written")
            continue
        print(f"[OK]   {clip.name}: stanceL={m['L']:.4f} stanceR={m['R']:.4f}")
        if args.write:
            meta = af.clip_meta(clip.name) or {}
            meta["stanceL"] = f"{m['L']:.4f}"
            meta["stanceR"] = f"{m['R']:.4f}"
            af.set_clip_meta(clip.name, meta)
            changed += 1
    if args.write and changed:
        write_anim(af, args.file)
        print(f"wrote {changed} clip_meta lines to {args.file}")
    return 0


def _cmd_metacheck(args):
    total_errors = 0
    for f in args.files:
        findings = clip_meta_schema.validate_file(Path(f))
        errors = sum(1 for s, _ in findings if s == "ERROR")
        warns = sum(1 for s, _ in findings if s == "WARN")
        total_errors += errors
        status = "FAIL" if errors else ("WARN" if warns else "PASS")
        print(f"[{status}] {f}: {errors} errors, {warns} warnings")
        for sev, msg in findings:
            print(f"    {sev}: {msg}")
    return 1 if total_errors else 0


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


def _cmd_clipcheck(args):
    from clip_metric import clip_metric, load_preset, DEFAULT_APPEARANCE
    import json as _json

    doc = _json.loads((Path(__file__).resolve().parents[2] /
                       "resources" / "appearance_presets.json").read_text(encoding="utf-8"))
    all_ids = [e["presetId"] for e in doc["presets"]]
    ids = [args.preset] if args.preset else all_ids

    base = clip_metric(args.file, DEFAULT_APPEARANCE, args.clip)
    print(f"standard baseline: {base['overlap_pct']:.2f}% overlap "
          f"(worst frame {base['frame']})")

    failed = False
    for pid in ids:
        m = clip_metric(args.file, load_preset(pid), args.clip)
        delta = m["overlap_pct"] - base["overlap_pct"]
        flag = ""
        if args.max_delta is not None and delta > args.max_delta:
            flag = "  <-- EXCEEDS BAND"
            failed = True
        print(f"{pid:12s} overlap={m['overlap_pct']:6.2f}%  delta=+{max(delta,0):5.2f}pt  "
              f"worst={m['frame']}{flag}")
        for v, a, b in m["pairs"][:3]:
            if v > 0.0005:
                print(f"{'':14s}{a} x {b}: {v:.4f}")
    return 1 if failed else 0


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

    p = sub.add_parser("slide", help="foot-slide report: stance velocity of each foot vs the clip Speed")
    p.add_argument("file")
    p.add_argument("--clips", help="comma-separated clip names (default: clips with a Speed line)")
    p.set_defaults(fn=_cmd_slide)

    p = sub.add_parser("stance", help="gait phase markers (stanceL/stanceR) for locomotion clips; "
                                      "--write stores them in clip_meta (A3 phase-synced transitions)")
    p.add_argument("file")
    p.add_argument("--clips", help="comma-separated clip names (default: clips with a Speed line)")
    p.add_argument("--write", action="store_true")
    p.set_defaults(fn=_cmd_stance)

    p = sub.add_parser("metacheck",
                       help="validate every '# clip_meta:' header line against "
                            "resources/anim/clip_meta_schema.json (header only, fast)")
    p.add_argument("files", nargs="+")
    p.set_defaults(fn=_cmd_metacheck)

    p = sub.add_parser("clipcheck",
                       help="bone-box interpenetration for an appearance preset "
                            "(where does the scale band break?)")
    p.add_argument("file")
    p.add_argument("--preset", default=None,
                   help="preset id from resources/appearance_presets.json "
                        "(default: all presets, table output)")
    p.add_argument("--clip", default="walk", help="clip to pose through (default: walk)")
    p.add_argument("--max-delta", type=float, default=None,
                   help="fail (exit 1) if overlap_pct exceeds standard's by more "
                        "than this many points")
    p.set_defaults(fn=_cmd_clipcheck)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
