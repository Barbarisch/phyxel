#!/usr/bin/env python3
"""Role-based quadruped animation retargeting: our mocap packs -> any skeleton.

The repo owns a 12-rig quadruped mocap library (the Quaternius animal pack:
Idle/Gallop/Attack/Death and nine more per animal) and, separately, meshes
worth shipping whose skeletons those clips were never authored for -- Meshy
Smart-Rig models with generic Bone_000 names and six-joint legs. Bone-name
mapping is dead on arrival there, and Meshy's own animate feature was ruled
out. This transfers motion by ROLE instead:

  * Roles are found GEOMETRICALLY on both skeletons -- legs via
    gen_quadruped_walk.detect_legs (the four lowest chain tips), head and tail
    chains via gen_quadruped_clips.skeleton_map. Nothing reads a bone name.

  * LEGS transfer as foot WORLD TRAJECTORIES: the source foot's path is
    sampled per frame, expressed relative to its own first-frame position,
    scaled by the leg-length ratio, and replayed on the target with the walk
    generator's 2-bone IK. Feet stay grounded by construction, and a 3-joint
    source leg drives a 6-joint Meshy leg without ever pairing bones.

  * BODY/NECK/HEAD/TAIL transfer as per-frame rotation DELTAS RELATIVE TO THE
    CLIP'S OWN FIRST FRAME, not the bind pose. That choice is load-bearing:
    the imported packs carry large constant offsets against their bind pose
    (the stag's Idle holds -69 deg on its Back bone for all 101 keys -- the
    vertical-stag bug), and first-frame deltas cancel any constant offset
    exactly. Chains of different lengths map by normalized index.

  * ROOT translation transfers as a world delta scaled by body-height ratio.

Self-checking like its siblings: the tool re-parses its own output and runs
posed-FK gates -- a retargeted death must end with the body below 60% of bind
height, and a retargeted walk must keep some foot near the ground on every
sampled frame. A transfer that silently mangles a transform fails loudly here
instead of standing in the hall in a T-pose.

Usage:
  python retarget_quadruped.py SOURCE.anim TARGET.anim \
      [--clips "Idle=idle,Walk=walk,Gallop=run,Attack_Headbutt=attack,Death=death"] \
      [--out OUT.anim]

The --clips list is "SourceClip=engine_name" pairs; the default covers the
Quaternius pack vocabulary. Existing target clips with the same engine name
are REPLACED (mocap beats the procedural fallback that unlocked these rigs).
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write, Clip, Channel  # noqa: E402
from gen_quadruped_walk import (  # noqa: E402
    bind_fk, detect_legs, q_conj, q_from_to, q_mul, q_rot,
    solve_two_bone, v_add, v_len, v_norm, v_scale, v_sub,
)
from gen_quadruped_clips import skeleton_map  # noqa: E402

SAMPLES = 28

DEFAULT_CLIPS = ("Idle=idle,Walk=walk,Gallop=run,"
                 "Attack_Headbutt=attack,Attack=attack,Death=death")


# ---- pose sampling ----------------------------------------------------------

def _lerp(a, b, w):
    return tuple(a[i] + (b[i] - a[i]) * w for i in range(len(a)))


def _nlerp(a, b, w):
    # shortest-path normalized lerp; plenty for dense mocap keys
    if sum(a[i] * b[i] for i in range(4)) < 0.0:
        b = tuple(-c for c in b)
    q = _lerp(a, b, w)
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / n for c in q)


def _sample_keys(keys, t, blend):
    if not keys:
        return None
    if t <= keys[0][0]:
        return keys[0][1]
    if t >= keys[-1][0]:
        return keys[-1][1]
    for i in range(len(keys) - 1):
        t0, v0 = keys[i]
        t1, v1 = keys[i + 1]
        if t0 <= t <= t1:
            w = 0.0 if t1 <= t0 else (t - t0) / (t1 - t0)
            return blend(v0, v1, w)
    return keys[-1][1]


def sample_pose(af, clip, t):
    """{bone_id: (local_pos, local_rot)} at time t, bind where un-keyed."""
    pose = {b.id: (tuple(b.pos), tuple(b.rot)) for b in af.bones}
    for ch in clip.channels:
        p0, r0 = pose[ch.bone_id]
        p = _sample_keys(ch.pos_keys, t, _lerp) or p0
        r = _sample_keys(ch.rot_keys, t, _nlerp) or r0
        pose[ch.bone_id] = (tuple(p), tuple(r))
    return pose


def posed_fk(af, pose):
    gp, gr = {}, {}
    for b in af.bones:
        lp, lr = pose[b.id]
        if b.parent_id < 0:
            gp[b.id], gr[b.id] = lp, lr
        else:
            pp, pr = gp[b.parent_id], gr[b.parent_id]
            gp[b.id] = v_add(pp, q_rot(pr, lp))
            gr[b.id] = q_mul(pr, lr)
    return gp, gr


# ---- role pairing -----------------------------------------------------------

def leg_key(leg):
    return (leg["group"], "L" if leg["rest"][0] > 0 else "R")


def pair_legs(src_sk, dst_sk):
    src = {leg_key(l): l for l in src_sk["legs"]}
    dst = {leg_key(l): l for l in dst_sk["legs"]}
    if set(src) != set(dst):
        raise SystemExit(f"leg role mismatch: src {sorted(src)} vs dst {sorted(dst)}")
    return [(src[k], dst[k]) for k in sorted(src)]


def chain_segments(src_chain, dst_chain):
    """dst joint j -> the SLICE of source joints it spans.

    Sampling one source joint per target joint loses bend: a fall spread over
    a 7-joint mocap spine at ~10 deg each is 70 deg of collapse, and a 2-joint
    Meshy spine that samples joints #0 and #6 receives 20 of them — the
    retargeted wolf "died" standing at 95% of bind height. Each target joint
    must instead COMPOSE every source delta in its slice, so the chain's total
    bend is conserved whatever the joint counts are.
    """
    if not src_chain or not dst_chain:
        return []
    m, n = len(src_chain), len(dst_chain)
    bounds = [round(k * m / n) for k in range(n + 1)]
    return [(src_chain[bounds[j]:max(bounds[j + 1], bounds[j] + 1)], dst_chain[j])
            for j in range(n)]


# ---- transfer ---------------------------------------------------------------

def retarget_clip(src_af, src_sk, clip, dst_af, dst_sk, out_name):
    dur = clip.duration
    times = [dur * i / SAMPLES for i in range(SAMPLES + 1)]

    # sample the whole source clip once
    frames = [posed_fk(src_af, sample_pose(src_af, clip, t)) for t in times]
    poses = [sample_pose(src_af, clip, t) for t in times]
    ref_pose = poses[0]

    src_by = src_sk["by_id"]
    dst_by = dst_sk["by_id"]
    dst_gp, dst_gr = bind_fk(dst_af)
    _, src_gr = bind_fk(src_af)
    h_ratio = dst_sk["body_h"] / max(src_sk["body_h"], 1e-6)

    def world_rot_of(bone_map, gr, bid):
        par = bone_map[bid].parent_id
        return gr[par] if par >= 0 else (0.0, 0.0, 0.0, 1.0)

    channels = []

    # ---- root: world-delta translation + first-frame-relative rotation ----
    s_root, d_root = src_sk["root"], dst_sk["root"]
    root_b = dst_by[d_root]
    ref_rp, ref_rr = frames[0][0][s_root], frames[0][1][s_root]
    pos_keys, rot_keys = [], []
    for i, t in enumerate(times):
        gp_i, gr_i = frames[i]
        dp = v_scale(v_sub(gp_i[s_root], ref_rp), h_ratio)
        d_rot = q_mul(gr_i[s_root], q_conj(ref_rr))          # world delta
        pos_keys.append((t, v_add(root_b.pos, dp)))
        rot_keys.append((t, q_mul(d_rot, root_b.rot)))
    channels.append(Channel(d_root, pos_keys=pos_keys, rot_keys=rot_keys))

    # ---- body chains: spine, head, tail ------------------------------------
    # Spine matters most: on mocap rigs the death fall and attack coil are
    # authored on the bones between root and neck, not on the root itself —
    # without this transfer a retargeted death "plays" and the body never falls.
    for chain_name in ("spine", "head", "tail"):
        for s_slice, d_id in chain_segments(src_sk[chain_name], dst_sk[chain_name]):
            db = dst_by[d_id]
            # Deltas are conjugated through WORLD axes on both sides. A delta
            # expressed in the source's parent frame lands on target axes that
            # can point anywhere (Meshy bakes a 90-deg X into every bone frame
            # from the GLB import), turning bend into twist — the wolf "died"
            # UP to 106% of bind height that way. World frame is the only
            # shared language between two arbitrary skeletons.
            R_dp = world_rot_of(dst_by, dst_gr, d_id)
            keys = []
            for i, t in enumerate(times):
                seg = (0.0, 0.0, 0.0, 1.0)          # accumulated, world frame
                for s_id in s_slice:
                    d_local = q_mul(poses[i][s_id][1], q_conj(ref_pose[s_id][1]))
                    R_sp = world_rot_of(src_by, src_gr, s_id)
                    d_world = q_mul(q_mul(R_sp, d_local), q_conj(R_sp))
                    seg = q_mul(seg, d_world)
                d_dst = q_mul(q_mul(q_conj(R_dp), seg), R_dp)
                keys.append((t, q_mul(d_dst, db.rot)))
            channels.append(Channel(d_id, rot_keys=keys))

    # ---- legs ---------------------------------------------------------------
    # Two transfer modes, chosen by what the clip IS:
    #
    #  * locomotion/idle -> foot-trajectory IK (ground contact by construction)
    #  * death           -> rotation-chain transfer, like the spine
    #
    # The distinction is load-bearing. A mocap death keeps the feet PLANTED
    # and collapses the body onto them, so its foot trajectories are ~zero —
    # replaying them through IK holds the target's legs straight and props the
    # corpse upright (the wolf "died" standing at 95% of bind height). The
    # fold lives in the hip/knee rotations, so for death those transfer
    # directly and ground contact stops being the invariant that matters.
    if out_name == "death":
        for s_leg, d_leg in pair_legs(src_sk, dst_sk):
            src_chain = s_leg["chain"]
            for s_slice, d_id in chain_segments(src_chain,
                                                [d_leg["hip"], d_leg["knee"]]):
                db = dst_by[d_id]
                R_dp = world_rot_of(dst_by, dst_gr, d_id)
                keys = []
                for i, t in enumerate(times):
                    seg = (0.0, 0.0, 0.0, 1.0)
                    for s_id in s_slice:
                        d_local = q_mul(poses[i][s_id][1],
                                        q_conj(ref_pose[s_id][1]))
                        R_sp = world_rot_of(src_by, src_gr, s_id)
                        seg = q_mul(seg, q_mul(q_mul(R_sp, d_local),
                                               q_conj(R_sp)))
                    d_dst = q_mul(q_mul(q_conj(R_dp), seg), R_dp)
                    keys.append((t, q_mul(d_dst, db.rot)))
                channels.append(Channel(d_id, rot_keys=keys))

        # Height-profile match: rotations fold the limbs but nothing lowers a
        # high-set root — animation has no gravity, so a Meshy hips-at-0.65
        # rig would die in mid-air. Per frame, the target's silhouette top is
        # corrected (root Y only, only ever DOWN) to track the source's own
        # collapse ratio. Measured against both rigs' real spans, so it works
        # whether the root sits at the pelvis or at the ground.
        src_ys0 = [p[1] for p in frames[0][0].values()]
        src_floor, src_top0 = min(src_ys0), max(src_ys0)
        dst_ys0 = [p[1] for p in dst_gp.values()]
        dst_floor, dst_top0 = min(dst_ys0), max(dst_ys0)
        temp = Clip(out_name, dur, channels=channels)
        root_pos = channels[0].pos_keys           # root channel was added first
        for i, t in enumerate(times):
            src_top = max(p[1] for p in frames[i][0].values())
            ratio = (src_top - src_floor) / max(src_top0 - src_floor, 1e-6)
            gp_i, _ = posed_fk(dst_af, sample_pose(dst_af, temp, t))
            cur_top = max(p[1] for p in gp_i.values())
            want_top = dst_floor + ratio * (dst_top0 - dst_floor)
            dy = min(0.0, want_top - cur_top)
            px, py, pz = root_pos[i][1]
            root_pos[i] = (root_pos[i][0], (px, py + dy, pz))
        return Clip(out_name, dur, channels=channels)

    for s_leg, d_leg in pair_legs(src_sk, dst_sk):
        s_foot = s_leg["foot"]
        s_len = (v_len(v_sub(src_sk["gp"][s_leg["knee"]], src_sk["gp"][s_leg["hip"]])) +
                 v_len(v_sub(src_sk["gp"][s_foot], src_sk["gp"][s_leg["knee"]])))
        hip, knee, foot = d_leg["hip"], d_leg["knee"], d_leg["foot"]
        P = dst_gp[hip]
        Kb, Fb = dst_gp[knee], dst_gp[foot]
        L1, L2 = v_len(v_sub(Kb, P)), v_len(v_sub(Fb, Kb))
        scale = (L1 + L2) / max(s_len, 1e-6)
        bend_sign = -1.0 if d_leg["group"] == "front" else 1.0
        hip_parent_gr = dst_gr[dst_by[hip].parent_id]
        knee_bind_dir = v_norm(v_sub(Fb, Kb))
        rest_src = frames[0][0][s_foot]
        rest_dst = d_leg["rest"]

        hip_keys, knee_keys = [], []
        for i, t in enumerate(times):
            off = v_scale(v_sub(frames[i][0][s_foot], rest_src), scale)
            F = (rest_dst[0] + off[0],
                 max(rest_dst[1] + off[1], rest_dst[1] - 0.02),  # never dig in
                 rest_dst[2] + off[2])
            K = solve_two_bone(P, F, L1, L2, (1.0, 0.0, 0.0), bend_sign)

            d_hip = q_from_to(v_norm(v_sub(Kb, P)), v_norm(v_sub(K, P)))
            hip_local = q_mul(q_conj(hip_parent_gr), q_mul(d_hip, dst_gr[hip]))
            cur_knee_dir = q_rot(d_hip, knee_bind_dir)
            d_knee = q_from_to(cur_knee_dir, v_norm(v_sub(F, K)))
            knee_new = q_mul(q_mul(d_knee, d_hip), dst_gr[knee])
            knee_parent_new = q_mul(d_hip, dst_gr[dst_by[knee].parent_id])
            knee_local = q_mul(q_conj(knee_parent_new), knee_new)
            hip_keys.append((t, hip_local))
            knee_keys.append((t, knee_local))
        channels.append(Channel(hip, rot_keys=hip_keys))
        channels.append(Channel(knee, rot_keys=knee_keys))

    # walk speed carries over, scaled by leg proportion (no-slide contract)
    speed = None
    if clip.speed:
        speed = clip.speed * h_ratio
    return Clip(out_name, dur, speed=speed, channels=channels)


# ---- gates ------------------------------------------------------------------

def _span_y(af, sk, clip, t):
    gp, _ = posed_fk(af, sample_pose(af, clip, t))
    ys = [p[1] for p in gp.values()]
    return min(ys), max(ys)


def gate(af, sk, name):
    clip = next((c for c in af.clips if c.name == name), None)
    if clip is None:
        return True
    lo0 = min(p[1] for p in sk["gp"].values())
    hi0 = max(p[1] for p in sk["gp"].values())
    bind_h = hi0 - lo0
    if name == "death":
        _, hi = _span_y(af, sk, clip, clip.duration)
        ok = (hi - lo0) <= 0.60 * bind_h + 1e-6
        print(f"  gate death: final top {(hi - lo0):.2f} of bind {bind_h:.2f} "
              f"({100 * (hi - lo0) / max(bind_h, 1e-6):.0f}%) -> "
              f"{'DOWN' if ok else 'STILL STANDING'}")
        return ok
    if name in ("walk", "run"):
        worst = -1e9
        for i in range(9):
            lo, _ = _span_y(af, sk, clip, clip.duration * i / 8)
            worst = max(worst, lo - lo0)
        ok = worst < 0.30 * bind_h
        print(f"  gate {name}: lowest bone stays within {worst:.3f} of ground "
              f"-> {'GROUNDED' if ok else 'FLOATING'}")
        return ok
    return True


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source")
    ap.add_argument("target")
    ap.add_argument("--clips", default=DEFAULT_CLIPS,
                    help='comma list of "SourceClip=engine_name"')
    ap.add_argument("--out", help="default: overwrite target")
    args = ap.parse_args(argv)

    src_af = parse(args.source)
    dst_af = parse(args.target)
    src_sk = skeleton_map(src_af)
    dst_sk = skeleton_map(dst_af)
    if len(src_sk["legs"]) < 4 or len(dst_sk["legs"]) < 4:
        print("REFUSED: both rigs must be quadrupeds")
        return 1

    wanted = []
    for pair in args.clips.split(","):
        s, _, d = pair.strip().partition("=")
        if s and d:
            wanted.append((s, d))

    src_clips = {c.name: c for c in src_af.clips}
    done = set()
    for s_name, out_name in wanted:
        if out_name in done or s_name not in src_clips:
            continue
        new = retarget_clip(src_af, src_sk, src_clips[s_name],
                            dst_af, dst_sk, out_name)
        dst_af.clips = [c for c in dst_af.clips if c.name != out_name]
        dst_af.clips.append(new)
        done.add(out_name)
        print(f"  {s_name} -> {out_name} ({new.duration:.2f}s"
              f"{', speed %.2f' % new.speed if new.speed else ''})")
    if not done:
        print("REFUSED: no requested source clips found")
        return 1

    out = Path(args.out) if args.out else Path(args.target)
    write(dst_af, out)

    # gate our own output
    af2 = parse(out)
    sk2 = skeleton_map(af2)
    ok = all(gate(af2, sk2, n) for n in ("death", "walk", "run"))
    if not ok:
        print("REFUSED: gates failed — output written but review before binding")
        return 1
    print(f"wrote {out}  ({', '.join(sorted(done))})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
