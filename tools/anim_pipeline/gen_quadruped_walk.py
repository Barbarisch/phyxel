#!/usr/bin/env python3
"""Rig-agnostic procedural quadruped walk generator.

Replaces a creature's walk clip with a species-tunable procedural gait. Gait is
species-specific -- applying one canned dog walk to a bear/horse/deer always
looks wrong -- so this BUILDS the walk from a parametric model instead of
retargeting a clip.

Rig-agnostic: legs are found GEOMETRICALLY (the four lowest chain tips are the
feet), not by bone name, so it works on Meshy's Smart-Rig output (generic
Bone_000.. names, 6-joint legs) as well as any other quadruped skeleton.

Per leg, per frame:
  * the foot follows a controlled ground-contact path -- planted and sweeping
    backward during stance, lifting in a forward arc during swing -- with the
    four feet phase-offset in the lateral-sequence footfall order
    (LH -> LF -> RH -> RF), the normal quadruped walk.
  * 2-bone analytic IK (law of cosines, in the leg's fore-aft plane) reaches
    the foot target, so feet never penetrate the ground and lift is exact.
  * intermediate joints stay at bind; the body gets a subtle bob (2x stride
    frequency). Un-keyed bones hold bind pose, so tails/spines never warp.

Usage:
  python gen_quadruped_walk.py <in.anim> <out.anim> [--species horse|bear|...]
"""
from __future__ import annotations
import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_format import parse, write, Clip, Channel  # noqa: E402

# ---- vector / quaternion helpers (quat = x,y,z,w to match the .anim format) -

def v_sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def v_add(a, b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def v_scale(a, s): return (a[0]*s, a[1]*s, a[2]*s)
def v_dot(a, b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def v_cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def v_len(a): return math.sqrt(v_dot(a, a))
def v_norm(a):
    n = v_len(a)
    return (a[0]/n, a[1]/n, a[2]/n) if n > 1e-12 else (0.0, 0.0, 0.0)

def q_mul(a, b):
    x1, y1, z1, w1 = a
    x2, y2, z2, w2 = b
    return (w1*x2+x1*w2+y1*z2-z1*y2, w1*y2-x1*z2+y1*w2+z1*x2,
            w1*z2+x1*y2-y1*x2+z1*w2, w1*w2-x1*x2-y1*y2-z1*z2)
def q_conj(q): return (-q[0], -q[1], -q[2], q[3])
def q_rot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    cx = y*vz-z*vy+w*vx
    cy = z*vx-x*vz+w*vy
    cz = x*vy-y*vx+w*vz
    return (vx+2*(y*cz-z*cy), vy+2*(z*cx-x*cz), vz+2*(x*cy-y*cx))
def q_from_to(a, b):
    """minimal rotation taking unit vector a to unit vector b."""
    d = v_dot(a, b)
    if d > 0.999999:
        return (0.0, 0.0, 0.0, 1.0)
    if d < -0.999999:
        # 180 deg about any axis perpendicular to a
        ax = v_cross((1, 0, 0), a)
        if v_len(ax) < 1e-6:
            ax = v_cross((0, 1, 0), a)
        ax = v_norm(ax)
        return (ax[0], ax[1], ax[2], 0.0)
    ax = v_cross(a, b)
    s = math.sqrt((1.0 + d) * 2.0)
    return (ax[0]/s, ax[1]/s, ax[2]/s, s * 0.5)

# ---- FK ---------------------------------------------------------------------

def bind_fk(af):
    gp, gr = {}, {}
    for b in af.bones:
        if b.parent_id < 0:
            gp[b.id], gr[b.id] = tuple(b.pos), tuple(b.rot)
        else:
            pp, pr = gp[b.parent_id], gr[b.parent_id]
            gp[b.id] = v_add(pp, q_rot(pr, b.pos))
            gr[b.id] = q_mul(pr, b.rot)
    return gp, gr

# ---- geometric leg detection ------------------------------------------------

def detect_legs(af):
    """Return one dict per leg: chain [hip..foot] ids, hip/knee/foot ids, rest
    foot pos, gait phase, group. Legs = the 4 lowest chain tips."""
    kids = {}
    for b in af.bones:
        kids.setdefault(b.parent_id, []).append(b.id)
    by_id = {b.id: b for b in af.bones}
    gp, _ = bind_fk(af)
    leaves = [b.id for b in af.bones if b.id not in kids]
    feet = sorted(leaves, key=lambda i: gp[i][1])[:4]   # 4 lowest tips

    legs = []
    for foot in feet:
        chain = [foot]
        cur = foot
        while True:
            par = by_id[cur].parent_id
            if par < 0 or len(kids.get(par, [])) != 1:  # stop at body branch
                break
            chain.append(par)
            cur = par
        chain = list(reversed(chain))            # [hip .. foot]
        hip, foot_id = chain[0], chain[-1]
        hy, fy = gp[hip][1], gp[foot_id][1]
        mid_y = (hy + fy) * 0.5
        interior = chain[1:-1] or [chain[len(chain)//2]]
        knee = min(interior, key=lambda i: abs(gp[i][1] - mid_y))
        rest = gp[foot_id]
        front = rest[2] > 0                       # +Z is forward
        left = rest[0] > 0                        # +X is left
        # lateral-sequence walk footfall: LH, LF, RH, RF
        phase = {(False, True): 0.00, (True, True): 0.25,
                 (False, False): 0.50, (True, False): 0.75}[(front, left)]
        legs.append(dict(chain=chain, hip=hip, knee=knee, foot=foot_id,
                         rest=rest, phase=phase,
                         group="front" if front else "back"))
    return legs

# ---- gait parameters --------------------------------------------------------

SPECIES = {
    "horse": dict(period=1.10, duty=0.62, stride=0.34, lift=0.11, bob=0.03),
    "bear":  dict(period=1.35, duty=0.68, stride=0.26, lift=0.08, bob=0.04),
    "deer":  dict(period=1.00, duty=0.58, stride=0.34, lift=0.13, bob=0.04),
    "boar":  dict(period=1.15, duty=0.66, stride=0.24, lift=0.08, bob=0.03),
    "wolf":  dict(period=1.05, duty=0.60, stride=0.30, lift=0.10, bob=0.03),
}
SAMPLES = 32

def foot_target(rest, ph, p):
    """Foot world position at cycle phase ph in [0,1)."""
    stride, lift, duty = p["stride"], p["lift"], p["duty"]
    if ph < duty:                                 # stance: planted, sweeps back
        s = ph / duty                             # 0..1
        dz = stride * (0.5 - s)                    # +half (front) -> -half (back)
        dy = 0.0
    else:                                          # swing: lifts, arcs forward
        s = (ph - duty) / (1.0 - duty)             # 0..1
        dz = stride * (-0.5 + s)                    # back -> front
        dy = lift * math.sin(math.pi * s)          # up-and-down arc
    return (rest[0], rest[1] + dy, rest[2] + dz)

def solve_two_bone(P, F, L1, L2, bend_axis, bend_sign):
    """Knee position for a 2-bone chain P->K->F in the plane defined by
    bend_axis (the leg's fore-aft plane normal ~ body left/right)."""
    d = v_len(v_sub(F, P))
    d = max(min(d, L1 + L2 - 1e-4), abs(L1 - L2) + 1e-4)
    a = (L1*L1 - L2*L2 + d*d) / (2*d)
    h = math.sqrt(max(0.0, L1*L1 - a*a))
    dirPF = v_norm(v_sub(F, P))
    bend = v_norm(v_cross(bend_axis, dirPF))
    return v_add(v_add(P, v_scale(dirPF, a)), v_scale(bend, h * bend_sign))

def generate(af, p):
    gp, gr = bind_fk(af)
    by_id = {b.id: b for b in af.bones}
    legs = detect_legs(af)
    period = p["period"]
    bend_axis = (1.0, 0.0, 0.0)                    # body left/right
    channels = []

    for leg in legs:
        hip, knee, foot = leg["hip"], leg["knee"], leg["foot"]
        P = gp[hip]
        Kb, Fb = gp[knee], gp[foot]
        L1 = v_len(v_sub(Kb, P))
        L2 = v_len(v_sub(Fb, Kb))
        # Anatomical bend direction. Forelimb and hindlimb knees fold in
        # OPPOSITE directions in a quadruped; the bind-pose guess is unreliable
        # for near-straight legs (it flipped the forelimbs). bend = cross(X,
        # dirPF) points -Z for a downward leg, so +1 folds the knee backward,
        # -1 forward. Forelimb "elbow" folds forward, hind "stifle" backward.
        bend_sign = -1.0 if leg["group"] == "front" else 1.0

        hip_parent_gr = gr[by_id[hip].parent_id]
        hip_bind_local = tuple(by_id[hip].rot)
        knee_bind_dir = v_norm(v_sub(Fb, Kb))      # knee->foot in bind

        hip_keys, knee_keys = [], []
        for i in range(SAMPLES + 1):
            t = i / SAMPLES
            ph = (t - leg["phase"]) % 1.0
            F = foot_target(leg["rest"], ph, p)
            K = solve_two_bone(P, F, L1, L2, bend_axis, bend_sign)

            # aim hip: bind hip->knee dir -> new P->K dir
            d_hip = q_from_to(v_norm(v_sub(Kb, P)), v_norm(v_sub(K, P)))
            hip_new_global = q_mul(d_hip, gr[hip])
            hip_local = q_mul(q_conj(hip_parent_gr), hip_new_global)

            # aim knee: after hip rotates rigidly, the knee's frame turns by
            # d_hip too; add the delta so knee->foot points to F.
            cur_knee_dir = q_rot(d_hip, knee_bind_dir)
            d_knee = q_from_to(cur_knee_dir, v_norm(v_sub(F, K)))
            knee_new_global = q_mul(q_mul(d_knee, d_hip), gr[knee])
            knee_parent_new = q_mul(d_hip, gr[by_id[knee].parent_id])
            knee_local = q_mul(q_conj(knee_parent_new), knee_new_global)

            hip_keys.append((t * period, hip_local))
            knee_keys.append((t * period, knee_local))

        channels.append(Channel(bone_id=hip, rot_keys=hip_keys))
        channels.append(Channel(bone_id=knee, rot_keys=knee_keys))

    # body bob on the root (feet plant twice per cycle -> 2x freq)
    root = next(b for b in af.bones if b.parent_id < 0)
    bob_keys = []
    for i in range(SAMPLES + 1):
        t = i / SAMPLES
        dy = p["bob"] * math.sin(2 * math.pi * 2 * t)
        bob_keys.append((t * period, (root.pos[0], root.pos[1] + dy, root.pos[2])))
    channels.append(Channel(bone_id=root.id, pos_keys=bob_keys))

    return Clip(name="walk", duration=period, speed=None,
                root_motion=None, channels=channels), legs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--species", default="horse", choices=list(SPECIES))
    args = ap.parse_args()

    af = parse(args.input)
    clip, legs = generate(af, SPECIES[args.species])
    af.clips = [c for c in af.clips if c.name != "walk"]
    af.clips.append(clip)
    write(af, args.output)

    print(f"procedural {args.species} walk -> {args.output}")
    print(f"  detected {len(legs)} legs:")
    for lg in legs:
        print(f"    {lg['group']:5s} phase {lg['phase']:.2f}  "
              f"chain {lg['chain']}  (hip {lg['hip']} knee {lg['knee']} foot {lg['foot']})")
    print(f"  {clip.duration}s, {len(clip.channels)} channels, {SAMPLES+1} keys")


if __name__ == "__main__":
    main()
