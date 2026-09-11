"""Report full-body and sword-wrist motion energy for clip-splitting decisions."""

from __future__ import annotations

import argparse
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import parse  # noqa: E402
from anim_lint import qangle, sample_position, sample_rotation  # noqa: E402
from pose_dsl import qmul  # noqa: E402


def qrotate(q, v):
    x, y, z, w = q
    qv = (v[0], v[1], v[2], 0.0)
    qi = (-x, -y, -z, w)
    r = qmul(qmul(q, qv), qi)
    return r[:3]


def add(a, b):
    return tuple(a[i] + b[i] for i in range(3))


def wrist_position(af, clip, t, wrist_id):
    channels = {ch.bone_id: ch for ch in clip.channels}
    positions = {}
    rotations = {}
    for bone in af.bones:
        ch = channels.get(bone.id)
        local_p = sample_position(ch.pos_keys, t) if ch and ch.pos_keys else bone.pos
        local_q = sample_rotation(ch.rot_keys, t) if ch and ch.rot_keys else bone.rot
        if bone.parent_id < 0:
            positions[bone.id], rotations[bone.id] = local_p, local_q
        else:
            parent_p = positions[bone.parent_id]
            parent_q = rotations[bone.parent_id]
            positions[bone.id] = add(parent_p, qrotate(parent_q, local_p))
            rotations[bone.id] = qmul(parent_q, local_q)
    return positions[wrist_id]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("anim")
    ap.add_argument("clip")
    ap.add_argument("--fps", type=float, default=30.0)
    args = ap.parse_args()

    af = parse(args.anim)
    clip = af.clip(args.clip)
    if clip is None:
        raise SystemExit(f"clip not found: {args.clip}")
    names = {b.id: b.name.split(":")[-1] for b in af.bones}
    wrist_id = next(b.id for b in af.bones if names[b.id] == "RightHand")
    body_names = {"Hips", "Spine", "Spine1", "Spine2", "RightArm",
                  "RightForeArm", "LeftArm", "LeftForeArm"}
    body = [ch for ch in clip.channels
            if names.get(ch.bone_id) in body_names and ch.rot_keys]

    dt = 1.0 / args.fps
    prior_q = None
    prior_wrist = None
    rows = []
    for frame in range(int(math.ceil(clip.duration * args.fps)) + 1):
        t = min(frame * dt, clip.duration)
        qs = [sample_rotation(ch.rot_keys, t) for ch in body]
        wrist = wrist_position(af, clip, t, wrist_id)
        angular = 0.0 if prior_q is None else sum(
            math.degrees(qangle(a, b)) / dt for a, b in zip(prior_q, qs))
        wrist_speed = 0.0 if prior_wrist is None else math.dist(wrist, prior_wrist) / dt
        rows.append((t, angular, wrist_speed))
        prior_q, prior_wrist = qs, wrist

    print(" time  body-deg/s  wrist-u/s")
    for t, angular, wrist_speed in rows:
        print(f"{t:5.2f} {angular:11.1f} {wrist_speed:10.3f}")


if __name__ == "__main__":
    main()
