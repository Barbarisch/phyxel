#!/usr/bin/env python3
"""Rig-agnostic idle/attack/death generator for quadrupeds.

Companion to gen_quadruped_walk.py, and the missing half of the Meshy lane:
Meshy Smart-Rig models arrive with beautiful meshes, generic bone names
(Bone_000..) and NO combat clips, which is why five high-quality quadrupeds
(bear/wolf/elk/horse/boar) sat unbindable while blockier forge rigs carried
their stat blocks — the bestiary refuses any rig that cannot idle, attack and
die. This closes that gap the same way the walk generator did: everything is
found GEOMETRICALLY, nothing by bone name.

Discovery (bind-pose FK):
  * legs/feet  — reused verbatim from gen_quadruped_walk.detect_legs
                 (the four lowest chain tips).
  * head chain — the non-foot chain tip with the greatest y + z (up-and-
                 forward); the chain walks back to the body branch. That is a
                 neck on every quadruped tried, horned or not.
  * tail chain — the non-foot tip with the most NEGATIVE z, accepted only if
                 it sits behind the root; absent tails are simply skipped.
  * root       — the skeleton root carries lunges and the death fall, because
                 translation keyed mid-chain tears a bone off its parent
                 (creature-forge rule, learned on the swarm rig).

The clips (engine FSM vocabulary, lowercase):
  idle    3.2s loop — breathing bob scaled to body height, head sway, tail
          sway. Phases deliberately unsynced so it never reads mechanical.
  attack  0.7s      — root lunge forward + neck/head wind-up-and-strike, with
          hit_fraction 0.45 stamped as clip meta.
  death   1.3s      — the body rolls onto its side and DROPS by its own
          measured shoulder height, legs folding, head falling last. The drop
          is measured, not guessed: the engine freezes the final frame, and a
          death that ends standing reads as alive (bestiary death_pose rule).

Verification is built in, not hoped for: after writing, the tool re-parses its
own output and runs a posed-FK check on the death clip's final frame — the
body centroid must end below 55% of its bind height. Same convention used to
write and to check, so the check cannot silently pass on a broken transform.

Usage:
  python gen_quadruped_clips.py <rig.anim> [--out OUT] [--species horse|bear|deer|boar|wolf]
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write, Clip, Channel  # noqa: E402
from gen_quadruped_walk import (  # noqa: E402
    bind_fk, detect_legs, q_mul, q_rot, v_add, v_len, v_sub,
)


# ---- small helpers ----------------------------------------------------------

def q_axis_angle(axis, deg):
    a = math.radians(deg) * 0.5
    s = math.sin(a)
    n = math.sqrt(sum(c * c for c in axis)) or 1.0
    return (axis[0] / n * s, axis[1] / n * s, axis[2] / n * s, math.cos(a))


def parent_frame(bone, deg_x=0.0, deg_y=0.0, deg_z=0.0):
    """Bone's local rotation with a parent-frame rotation applied on top."""
    q = bone.rot
    if deg_x:
        q = q_mul(q_axis_angle((1, 0, 0), deg_x), q)
    if deg_y:
        q = q_mul(q_axis_angle((0, 1, 0), deg_y), q)
    if deg_z:
        q = q_mul(q_axis_angle((0, 0, 1), deg_z), q)
    return q


# ---- discovery --------------------------------------------------------------

def skeleton_map(af):
    """Locate root, head chain, tail chain, legs and body metrics."""
    by_id = {b.id: b for b in af.bones}
    kids = {}
    for b in af.bones:
        kids.setdefault(b.parent_id, []).append(b.id)
    gp, _ = bind_fk(af)

    legs = detect_legs(af)
    leg_bones = {i for leg in legs for i in leg["chain"]}
    root = next(b.id for b in af.bones if b.parent_id < 0)

    leaves = [b.id for b in af.bones if b.id not in kids and b.id not in leg_bones]

    def chain_of(tip):
        chain = [tip]
        cur = tip
        while True:
            par = by_id[cur].parent_id
            if par < 0 or len(kids.get(par, [])) != 1:
                break
            chain.append(par)
            cur = par
        return list(reversed(chain))              # [base .. tip]

    head_chain, tail_chain = [], []
    if leaves:
        head_tip = max(leaves, key=lambda i: gp[i][1] + gp[i][2])
        head_chain = chain_of(head_tip)
        rest = [i for i in leaves if i not in set(head_chain)]
        behind = [i for i in rest if gp[i][2] < gp[root][2]]
        if behind:
            tail_chain = chain_of(min(behind, key=lambda i: gp[i][2]))

    # Spine: the bones BETWEEN the root and the neck base. They belong to no
    # leaf chain, so chain-based transfer skips them — and on mocap rigs the
    # whole body motion (a death fall, an attack coil) is authored exactly
    # there. Found by walking the head chain's base back up to the root.
    spine = []
    if head_chain:
        cur = by_id[head_chain[0]].parent_id
        while cur >= 0 and cur != root:
            spine.append(cur)
            cur = by_id[cur].parent_id
        spine.reverse()                           # [after-root .. neck-base-parent]

    foot_y = min(gp[leg["foot"]][1] for leg in legs) if legs else 0.0
    top_y = max(p[1] for p in gp.values())
    return dict(root=root, by_id=by_id, gp=gp, legs=legs,
                head=head_chain, tail=tail_chain, spine=spine,
                body_h=top_y - foot_y, shoulder_h=gp[root][1] - foot_y)


# ---- clip builders ----------------------------------------------------------

def _rot_channel(bone, keys):
    """keys: [(t, dx, dy, dz)] parent-frame degree offsets -> Channel."""
    return Channel(bone.id, rot_keys=[
        (t, parent_frame(bone, dx, dy, dz)) for (t, dx, dy, dz) in keys])


def _pos_channel(bone, keys):
    """keys: [(t, ox, oy, oz)] offsets from bind local position."""
    p = bone.pos
    return Channel(bone.id, pos_keys=[
        (t, (p[0] + ox, p[1] + oy, p[2] + oz)) for (t, ox, oy, oz) in keys])


def build_idle(sk, dur=3.2):
    ch = []
    root = sk["by_id"][sk["root"]]
    bob = 0.012 * sk["body_h"]
    ch.append(_pos_channel(root, [(0, 0, 0, 0), (dur / 2, 0, bob, 0), (dur, 0, 0, 0)]))
    # head sway on the first two neck joints, phased apart
    for k, bid in enumerate(sk["head"][:2]):
        b = sk["by_id"][bid]
        amp = 3.5 - k
        ch.append(Channel(b.id, rot_keys=[
            (0.0, parent_frame(b, -amp, amp * 1.4, 0)),
            (dur * (0.30 + 0.12 * k), parent_frame(b, amp, -amp * 1.4, 0)),
            (dur * (0.72 + 0.08 * k), parent_frame(b, -amp * 0.5, amp, 0)),
            (dur, parent_frame(b, -amp, amp * 1.4, 0)),
        ]))
    for bid in sk["tail"][:2]:
        b = sk["by_id"][bid]
        ch.append(Channel(b.id, rot_keys=[
            (0.0, parent_frame(b, 0, -6, 0)),
            (dur * 0.5, parent_frame(b, 0, 6, 0)),
            (dur, parent_frame(b, 0, -6, 0)),
        ]))
    return Clip("idle", dur, channels=ch)


def build_attack(sk, dur=0.7):
    ch = []
    root = sk["by_id"][sk["root"]]
    lunge = 0.22 * sk["body_h"]
    ch.append(_pos_channel(root, [
        (0.0, 0, 0, 0),
        (dur * 0.20, 0, 0.02 * sk["body_h"], -lunge * 0.25),   # coil back
        (dur * 0.45, 0, -0.01 * sk["body_h"], lunge),          # strike
        (dur * 0.70, 0, 0, lunge * 0.85),
        (dur, 0, 0, 0),
    ]))
    # neck/head: wind up, snap down-and-forward
    for k, bid in enumerate(sk["head"][:3]):
        b = sk["by_id"][bid]
        w = 1.0 - 0.25 * k
        ch.append(Channel(b.id, rot_keys=[
            (0.0, b.rot),
            (dur * 0.20, parent_frame(b, -22 * w, 0, 0)),
            (dur * 0.45, parent_frame(b, 26 * w, 0, 0)),
            (dur * 0.70, parent_frame(b, 18 * w, 0, 0)),
            (dur, b.rot),
        ]))
    return Clip("attack", dur, channels=ch)


def build_death(sk, dur=1.3):
    ch = []
    root = sk["by_id"][sk["root"]]
    # Roll onto the side and drop by the measured shoulder height — that is
    # what physically puts the barrel on the ground for THIS rig, not a stock
    # number tuned on some other creature.
    drop = 0.62 * sk["shoulder_h"]
    ch.append(Channel(root.id,
        pos_keys=[
            (0.0, root.pos),
            (dur * 0.40, (root.pos[0], root.pos[1] - drop * 0.35, root.pos[2])),
            (dur * 0.78, (root.pos[0], root.pos[1] - drop * 0.85, root.pos[2])),
            (dur, (root.pos[0], root.pos[1] - drop, root.pos[2])),
        ],
        rot_keys=[
            (0.0, root.rot),
            (dur * 0.40, parent_frame(root, 0, 0, 26)),
            (dur * 0.78, parent_frame(root, 0, 0, 62)),
            (dur, parent_frame(root, 0, 0, 78)),
        ]))
    # legs fold under
    for leg in sk["legs"]:
        hip = sk["by_id"][leg["hip"]]
        knee = sk["by_id"][leg["knee"]]
        ch.append(_rot_channel(hip, [(0, 0, 0, 0), (dur * 0.7, 24, 0, 0), (dur, 32, 0, 0)]))
        ch.append(_rot_channel(knee, [(0, 0, 0, 0), (dur * 0.7, -30, 0, 0), (dur, -40, 0, 0)]))
    # head falls last
    for k, bid in enumerate(sk["head"][:2]):
        b = sk["by_id"][bid]
        ch.append(Channel(b.id, rot_keys=[
            (0.0, b.rot),
            (dur * (0.55 + 0.1 * k), parent_frame(b, 18, 0, 10)),
            (dur, parent_frame(b, 26, 0, 16)),
        ]))
    return Clip("death", dur, channels=ch)


# ---- self-check -------------------------------------------------------------

def posed_fk_min_max_y(af, clip):
    """Bone world-Y span at the clip's FINAL frame (pose applied)."""
    pose_rot, pose_pos = {}, {}
    for c in clip.channels:
        if c.rot_keys:
            pose_rot[c.bone_id] = c.rot_keys[-1][1]
        if c.pos_keys:
            pose_pos[c.bone_id] = c.pos_keys[-1][1]
    gp = {}
    gr = {}
    for b in af.bones:
        lp = pose_pos.get(b.id, b.pos)
        lr = pose_rot.get(b.id, b.rot)
        if b.parent_id < 0:
            gp[b.id], gr[b.id] = tuple(lp), tuple(lr)
        else:
            pp, pr = gp[b.parent_id], gr[b.parent_id]
            gp[b.id] = v_add(pp, q_rot(pr, lp))
            gr[b.id] = q_mul(pr, lr)
    ys = [p[1] for p in gp.values()]
    return min(ys), max(ys)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input")
    ap.add_argument("--out", help="default: overwrite input")
    ap.add_argument("--species", default="horse",
                    help="reserved for future per-species timing; unused today")
    args = ap.parse_args(argv)

    src = Path(args.input)
    af = parse(src)
    sk = skeleton_map(af)
    if len(sk["legs"]) < 4:
        print(f"REFUSED: found {len(sk['legs'])} legs, need 4")
        return 1

    existing = {c.name.lower() for c in af.clips}
    added = []
    for clip in (build_idle(sk), build_attack(sk), build_death(sk)):
        if clip.name in existing:
            print(f"  keep existing '{clip.name}'")
            continue
        af.clips.append(clip)
        added.append(clip.name)

    out = Path(args.out) if args.out else src
    write(af, out)

    # Re-parse OUR OWN OUTPUT and check the death pose lands.
    af2 = parse(out)
    death = next((c for c in af2.clips if c.name == "death"), None)
    if death is not None:
        lo0, hi0 = posed_fk_min_max_y(af2, Clip("bind", 0.0, channels=[]))
        lo1, hi1 = posed_fk_min_max_y(af2, death)
        bind_h = hi0 - lo0
        final_h = hi1 - lo0
        verdict = "DOWN" if final_h <= 0.55 * bind_h + 1e-6 else "STILL STANDING"
        print(f"  death check: bind span {bind_h:.2f}, final top {final_h:.2f} "
              f"({100 * final_h / max(bind_h, 1e-6):.0f}%) -> {verdict}")
        if verdict != "DOWN":
            print("REFUSED: death clip does not end down")
            return 1

    print(f"wrote {out}  (+{', '.join(added) if added else 'nothing new'})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
