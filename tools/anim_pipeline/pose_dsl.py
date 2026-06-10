"""Pose DSL: author sparse-keyframe clips as (time, named-pose, ease) sequences.

Why this exists: generating raw quaternion keyframes directly produces janky
motion (sign flips, non-unit quats, uncanny full-body poses). This DSL makes
those failure modes impossible by construction:

- Poses are per-bone rotation DELTAS (Euler degrees, or raw quats) applied on
  top of a base stance sampled from a known-good clip (default: idle @ t=0).
- A clip is a list of (time, pose, ease) keys. Every channel gets a key at
  every pose time, so interpolation is fully predictable.
- Quaternions are normalized and sign-aligned with the previous key, so the
  engine's shortest-path slerp always takes the intended arc.
- Easing is implemented by smoothstep-subdividing segments (the engine only
  does linear-time slerp between keys).

Conventions:
- Bone names are the short Mixamo names ("RightArm", not "mixamorig:RightArm").
- Euler angles are degrees, intrinsic XYZ: q = qx * qy * qz.
- Delta application is in the bone's local frame: q_final = q_base * q_delta.
- mirror_pose() maps a right-side pose to the left by negating Y and Z.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import AnimFile, Clip, Channel  # noqa: E402
from anim_lint import qslerp, qnorm, qdot, sample_rotation, sample_position  # noqa: E402


# ---------------------------------------------------------------------------
# Quaternion / Euler helpers (x, y, z, w order)
# ---------------------------------------------------------------------------

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def qinv(q):
    return (-q[0], -q[1], -q[2], q[3])


def qnormalize(q):
    n = qnorm(q)
    return tuple(x / n for x in q) if n > 1e-12 else (0.0, 0.0, 0.0, 1.0)


def axis_angle(axis, deg):
    rad = math.radians(deg)
    s = math.sin(rad / 2.0)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(rad / 2.0))


def euler_to_quat(x_deg, y_deg, z_deg):
    """Intrinsic XYZ: rotate about X, then the new Y, then the new Z."""
    qx = axis_angle((1, 0, 0), x_deg)
    qy = axis_angle((0, 1, 0), y_deg)
    qz = axis_angle((0, 0, 1), z_deg)
    return qnormalize(qmul(qmul(qx, qy), qz))


def smoothstep(t):
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------------------
# Poses
# ---------------------------------------------------------------------------

def resolve_delta(value):
    """A pose entry is either an euler tuple (x,y,z) in degrees, or
    {"quat": (x,y,z,w)} for an exact rotation delta."""
    if isinstance(value, dict):
        return qnormalize(tuple(value["quat"]))
    return euler_to_quat(*value)


def mirror_pose(pose: dict) -> dict:
    """Right-side pose -> left-side pose. Bone names Right*->Left* and the
    standard X-mirror euler mapping (x, -y, -z). Quats entries are mirrored
    the same way: (x, -y, -z, w)."""
    out = {}
    for bone, val in pose.items():
        if bone.startswith("Right"):
            mbone = "Left" + bone[len("Right"):]
        elif bone.startswith("Left"):
            mbone = "Right" + bone[len("Left"):]
        else:
            mbone = bone
        if isinstance(val, dict):
            q = val["quat"]
            out[mbone] = {"quat": (q[0], -q[1], -q[2], q[3])}
        else:
            out[mbone] = (val[0], -val[1], -val[2])
    return out


def merge_poses(*poses: dict) -> dict:
    """Later poses override earlier ones per-bone."""
    out = {}
    for p in poses:
        out.update(p)
    return out


# ---------------------------------------------------------------------------
# Base stance
# ---------------------------------------------------------------------------

class BaseStance:
    """Per-bone base rotation (and root position) sampled from a reference
    clip at a reference time. Poses are deltas on top of this."""

    def __init__(self, af: AnimFile, ref_clip: str = "idle", ref_time: float = 0.0):
        clip = af.clip(ref_clip)
        if clip is None:
            raise ValueError(f"reference clip '{ref_clip}' not found")
        self.af = af
        self.rotations = {}  # bone_id -> quat
        self.positions = {}  # bone_id -> vec3 (only bones with pos channels)
        for ch in clip.channels:
            if ch.rot_keys:
                self.rotations[ch.bone_id] = qnormalize(sample_rotation(ch.rot_keys, ref_time))
            if ch.pos_keys:
                self.positions[ch.bone_id] = sample_position(ch.pos_keys, ref_time)
        # name -> id map using short names
        self.short_to_id = {}
        for b in af.bones:
            self.short_to_id[b.name.split(":")[-1]] = b.id

    def bone_id(self, short_name: str) -> int:
        if short_name not in self.short_to_id:
            raise KeyError(f"unknown bone '{short_name}'; known: {sorted(self.short_to_id)}")
        return self.short_to_id[short_name]

    def resolve_pose(self, pose: dict) -> dict:
        """pose {short_name: euler/quat-delta} -> {bone_id: absolute local quat}."""
        out = dict(self.rotations)  # start at base stance for ALL bones
        for short_name, val in pose.items():
            bid = self.bone_id(short_name)
            base = self.rotations.get(bid, self.af.bones[bid].rot)
            out[bid] = qnormalize(qmul(base, resolve_delta(val)))
        return out


# ---------------------------------------------------------------------------
# Clip building
# ---------------------------------------------------------------------------

def build_clip(stance: BaseStance, name: str, duration: float, keys: list,
               poses: dict, subdivisions: int = 3) -> Clip:
    """keys: [(time, pose_name_or_dict, ease), ...] with ease in
    {"smooth", "linear"}. Pose may be a name into `poses` or an inline dict.
    Returns a full-body Clip: every base-stance bone gets a rotation channel
    keyed at every (possibly subdivided) time; root bones keep their base
    position as a single constant PosKey.
    """
    if not keys:
        raise ValueError("clip needs at least one key")
    resolved = []  # (time, {bone_id: quat}, ease)
    for entry in keys:
        t, pose, ease = entry if len(entry) == 3 else (*entry, "smooth")
        pose_dict = poses[pose] if isinstance(pose, str) else pose
        resolved.append((float(t), stance.resolve_pose(pose_dict), ease))
    resolved.sort(key=lambda e: e[0])

    # Build the global key timeline with smoothstep subdivision per segment.
    # Subdivided samples are placed at linear times with smoothstep-blended
    # values, which approximates eased interpolation through linear slerp.
    timeline = [resolved[0][:2]]  # [(time, {bone_id: quat})]
    for i in range(len(resolved) - 1):
        t0, p0, _ = resolved[i]
        t1, p1, ease = resolved[i + 1]
        if t1 - t0 < 1e-6:
            continue
        if ease == "smooth" and subdivisions > 0:
            for s in range(1, subdivisions + 1):
                f = s / (subdivisions + 1)
                blend = smoothstep(f)
                t = t0 + f * (t1 - t0)
                blended = {bid: qslerp(p0[bid], p1[bid], blend) for bid in p0}
                timeline.append((t, blended))
        timeline.append((t1, p1))

    clip = Clip(name=name, duration=float(duration))
    for bid in sorted(stance.rotations):
        ch = Channel(bone_id=bid)
        prev_q = None
        for t, pose_map in timeline:
            q = qnormalize(pose_map[bid])
            if prev_q is not None and qdot(prev_q, q) < 0.0:
                q = tuple(-x for x in q)  # keep sign-continuous for clean slerp
            ch.rot_keys.append((t, q))
            prev_q = q
        if bid in stance.positions:
            ch.pos_keys.append((0.0, stance.positions[bid]))
        clip.channels.append(ch)
    return clip


# ---------------------------------------------------------------------------
# Pose snapshots: seed poses from real clips
# ---------------------------------------------------------------------------

def snapshot_deltas(stance: BaseStance, clip: Clip, t: float, bones: list) -> dict:
    """Extract {short_name: {"quat": delta}} for the given bones at time t of
    an existing clip — the delta that, applied to the base stance, reproduces
    that clip's pose. Use to seed cast poses from proven animation frames."""
    by_bone = {ch.bone_id: ch for ch in clip.channels}
    out = {}
    for short_name in bones:
        bid = stance.bone_id(short_name)
        if bid not in by_bone or not by_bone[bid].rot_keys:
            continue
        q_clip = qnormalize(sample_rotation(by_bone[bid].rot_keys, t))
        base = stance.rotations.get(bid, stance.af.bones[bid].rot)
        delta = qnormalize(qmul(qinv(base), q_clip))
        out[short_name] = {"quat": tuple(round(x, 6) for x in delta)}
    return out
