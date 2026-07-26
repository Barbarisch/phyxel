#!/usr/bin/env python3
"""Finalize an imported quadruped mocap rig for the engine.

Two deterministic corrections every voxelized mocap animal needs (worked out
the hard way on the Quaternius horse), applied automatically:

  1. GROUNDING: the engine grounds a character on its lowest *bone*, but voxel
     hooves hang below the skeleton -- so the animal sinks. We add an invisible
     `ground_ref` bone at the true deepest-hoof height (measured with bone
     ROTATION applied to the voxel offsets, across all locomotion clips) so the
     engine grounds on where the feet actually are.

  2. WALK SPEED: an NPC that translates faster/slower than the walk clip's
     stride slides its feet. The no-slide speed is the foot's backward sweep
     during STANCE / stance duration (NOT stride/cycle -- stance is only ~60%
     of the cycle, which is why naive stride/duration under-shoots ~2x). We
     measure it and record it as the Walk clip's `Speed`, and print the value
     to pass as the NPC walkSpeed.

Rig-agnostic: feet are found geometrically (lowest, most-Z-travelling animated
bones), so it works on Quaternius, Meshy, etc.

Usage: python finalize_quadruped.py <rig.anim>
"""
from __future__ import annotations
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write, Bone            # noqa: E402
from gen_quadruped_walk import (bind_fk, q_mul, q_rot,  # noqa: E402
                                v_add)

LOCO_CLIPS = ("Walk", "Trot", "Gallop", "Run", "Idle", "walk", "run")
# Grounding samples only the AMBIENT clips an NPC actually stands/walks in. A
# deep Gallop/jump swing frame reaches lower than the walk stance and, used as
# the ground reference, lifts the standing animal off the floor (the wolf-float
# bug -- worse on small animals where the gap is a big % of height).
GROUND_CLIPS = ("Walk", "Idle", "walk")
# The engine draws the model at visualOrigin.y - footOffset + K_MODEL_VISUAL_LIFT.
# That fixed lift keeps a BONE-grounded model's foot voxels off the floor, but a
# ground_ref grounds on the real deepest VOXEL, so the lift becomes pure float.
# Bake the lift into the ref (ground_ref = deepest_hoof + lift) to cancel it, so
# the hoof sits exactly on the ground regardless of model size.
K_MODEL_VISUAL_LIFT = 0.05


def _fk_fn(af, clip_name):
    clip = next((c for c in af.clips if c.name == clip_name), None)
    ch = {c.bone_id: c for c in clip.channels} if clip else {}

    def nr(k, t):
        return min(k, key=lambda kv: abs(kv[0] - t))[1]

    def fk(t):
        gp, gr = {}, {}
        for b in af.bones:
            lp, lr = list(b.pos), list(b.rot)
            c = ch.get(b.id)
            if c:
                if c.rot_keys:
                    lr = nr(c.rot_keys, t)
                if c.pos_keys:
                    lp = nr(c.pos_keys, t)
            if b.parent_id < 0:
                gp[b.id], gr[b.id] = tuple(lp), tuple(lr)
            else:
                pp, pr = gp[b.parent_id], gr[b.parent_id]
                gp[b.id] = v_add(pp, q_rot(pr, lp))
                gr[b.id] = q_mul(pr, lr)
        return gp, gr
    return fk, (clip.duration if clip else 0.0)


def deepest_hoof(af):
    """Lowest voxel-bottom world(model)-Y across all locomotion clips, with
    bone rotation applied to each voxel offset."""
    boxes_by_bone = {}
    for bx in af.boxes:
        boxes_by_bone.setdefault(bx.bone_id, []).append(bx)
    mn = 1e9
    clips = [c.name for c in af.clips if c.name in GROUND_CLIPS] or \
            [c.name for c in af.clips if c.name in LOCO_CLIPS] or [af.clips[0].name]
    for cn in clips:
        fk, dur = _fk_fn(af, cn)
        if dur <= 0:
            continue
        for i in range(25):
            gp, gr = fk(i / 24 * dur)
            for bid, bxs in boxes_by_bone.items():
                if bid not in gp:
                    continue
                for bx in bxs:
                    c = q_rot(gr[bid], bx.center)
                    mn = min(mn, gp[bid][1] + c[1] - bx.size[1] / 2)
    return mn


def ensure_ground_ref(af):
    # Drop any prior ground_ref so re-finalizing corrects an earlier value.
    af.bones = [b for b in af.bones if b.name != "ground_ref"]
    gp, _ = bind_fk(af)
    ref = deepest_hoof(af)
    # footOffset that lands the deepest walk hoof exactly on the ground:
    #   hoof_world = worldPos - footOffset + LIFT + ref  == worldPos
    #   => footOffset (== ground_ref global Y) = ref + LIFT
    target = ref + K_MODEL_VISUAL_LIFT
    root = next(b for b in af.bones if b.parent_id < 0)
    local_y = target - gp[root.id][1]
    new_id = max(b.id for b in af.bones) + 1
    af.bones.append(Bone(id=new_id, name="ground_ref", parent_id=root.id,
                         pos=(0.0, local_y, 0.0), rot=(0.0, 0.0, 0.0, 1.0),
                         scale=(1.0, 1.0, 1.0)))
    return target                             # new footOffset


def measure_walk_speed(af, clip_name="Walk"):
    fk, dur = _fk_fn(af, clip_name)
    if dur <= 0:
        return None
    N = 120
    dt = dur / N
    frames = [fk(i / N * dur) for i in range(N)]
    # feet = animated bones with the most local-Z travel that also sit low
    ztrav, ylow = {}, {}
    for b in af.bones:
        zs = [frames[i][0][b.id][2] for i in range(N)]
        ys = [frames[i][0][b.id][1] for i in range(N)]
        ztrav[b.id] = max(zs) - min(zs)
        ylow[b.id] = min(ys)
    allmin = min(ylow.values())
    allmax = max(frames[0][0][b.id][1] for b in af.bones)
    lowcut = allmin + 0.35 * (allmax - allmin)
    feet = sorted((b.id for b in af.bones if ylow[b.id] < lowcut),
                  key=lambda i: -ztrav[i])[:4]
    speeds = []
    for fid in feet:
        zs = [frames[i][0][fid][2] for i in range(N)]
        ys = [frames[i][0][fid][1] for i in range(N)]
        ymin, ymax = min(ys), max(ys)
        thr = ymin + 0.3 * (ymax - ymin)
        st = [i for i in range(N) if ys[i] <= thr]        # stance = planted
        if len(st) < 4:
            continue
        zst = [zs[i] for i in st]
        stride = max(zst) - min(zst)                      # backward sweep in stance
        speeds.append(stride / (len(st) * dt))
    if not speeds:
        return None
    # The DRIVING feet (largest stance sweep) set how far the body must travel
    # per cycle. The median under-shoots when fore/hind sweeps differ (canine
    # clips: front paws barely stride), leaving the body lagging the animation.
    # Take the largest plausible per-foot speed (outliers filtered vs median).
    med = statistics.median(speeds)
    good = [s for s in speeds if 0.3 * med <= s <= 3.0 * med]
    return max(good) if good else med


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: finalize_quadruped.py <rig.anim>")
    path = sys.argv[1]
    af = parse(path)
    foot_offset = ensure_ground_ref(af)
    spd = measure_walk_speed(af)
    if spd:
        for c in af.clips:
            if c.name in ("Walk", "walk"):
                c.speed = round(spd, 3)
    write(af, path)
    print(f"finalized {path}")
    print(f"  grounding: ground_ref -> footOffset {foot_offset:.3f} "
          f"(walk hoof lands on ground, +{K_MODEL_VISUAL_LIFT} engine lift canceled)")
    print(f"  no-slide walkSpeed = {spd:.3f}" if spd else "  walk speed: (no Walk clip)")


if __name__ == "__main__":
    main()
