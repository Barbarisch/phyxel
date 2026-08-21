"""Full body-plan derivation for generated rigs.

The engine's BodyPlanRegistry::planForSkeleton (BodyPlan.cpp) scores every
same-morphology plan by EXACT-name resolution of rootBone + legs + segments
and rejects anything scoring < 3 — and the alphabetically-first plan per
morphology doubles as the registered fallback default. A minimal plan
({id, morphology, clipDefaults}) therefore scores 0, loses its own
clipDefaults, AND shadows the real default for every other creature of the
morphology. So generated plans must always be FULL: rootBone, hipAliases,
gripBone, capsule, legs, segments, clipDefaults — every bone name emitted
verbatim from the compiled skeleton so resolveAgainst always matches.

Heuristics are spec-overridable via an optional spec["body_plan"] block:
  {"root": "...", "grip": "...", "gait_class": "...",
   "leg_terminals": ["paw","hoof",...], "segments": ["...", ...]}
"""
from __future__ import annotations

_LEG_TERMINALS = ("paw", "foot", "hoof", "toe", "tibia")
_GRIP_HINTS = ("head", "skull", "fang", "muzzle", "jaw")

_GAIT_BY_MORPHOLOGY = {
    "quadruped": "quadruped_clips",
    "arachnid": "arachnid_clips",
    "dragon": "dragon_clips",
    "humanoid": "biped_fsm",
}


def _leg_chains(sk, terminals):
    out = {}
    for cn, names in sk.chains.items():
        if len(names) < 2:
            continue
        last = names[-1].lower()
        if any(t in last for t in terminals):
            out[cn] = names
    return out


def derive_plan(compiled, plan_id: str) -> dict:
    """Compiled rig -> full body-plan dict (engine BodyPlan::fromJson shape)."""
    sk = compiled.sk
    spec = compiled.spec
    override = spec.get("body_plan", {})
    from .skeleton import detect_morphology
    morphology = detect_morphology(sk.names)
    if morphology == "unknown":
        morphology = "quadruped"  # advisor already warned at compile time

    names = list(sk.names)
    lower = {n.lower(): n for n in names}

    # root: exact 'pelvis'-named bone, else the skeleton root
    root = override.get("root")
    if root is None:
        root = lower.get("pelvis") or lower.get("hips") \
            or lower.get("cephalothorax") or names[0]

    grip = override.get("grip")
    if grip is None:
        for hint in _GRIP_HINTS:
            hit = next((n for n in names if hint in n.lower()), None)
            if hit:
                grip = hit
                break
        grip = grip or root

    terminals = tuple(override.get("leg_terminals", _LEG_TERMINALS))
    legs = []
    leg_chains = _leg_chains(sk, terminals)
    for cn in sorted(leg_chains):
        chain = leg_chains[cn]
        legs.append({
            "id": cn.lower(),
            "upper": chain[0],
            "mid": chain[-2],
            "foot": chain[-1],
            "footIK": False,
        })

    # segments: the first root chain's bones + the root bone of each attached
    # non-leg chain + every leg's upper — collision-box table, order = contract
    seg_names = override.get("segments")
    if seg_names is None:
        seg_names = []
        attach = spec.get("attach", {})
        root_chains = [cn for cn in spec.get("chains", {}) if cn not in attach]
        if root_chains:
            seg_names.extend(spec["chains"][root_chains[0]])
        for cn, chain in sk.chains.items():
            if cn in leg_chains or (root_chains and cn == root_chains[0]):
                continue
            if chain and chain[0] not in seg_names:
                seg_names.append(chain[0])
        for leg in legs:
            if leg["upper"] not in seg_names:
                seg_names.append(leg["upper"])
    segments = [{"bone": n, "isArm": False} for n in seg_names if n in sk.index]

    # capsule from the voxel grid's XZ extent
    max_half = 0.3
    if compiled.grid.cells:
        vs = compiled.grid.vs
        xs = [k[0] for k in compiled.grid.cells]
        zs = [k[2] for k in compiled.grid.cells]
        half_x = (max(xs) - min(xs) + 1) * vs * 0.5
        half_z = (max(zs) - min(zs) + 1) * vs * 0.5
        max_half = max(0.3, round(max(half_x, half_z), 2))

    clips = {c.name for c in compiled.af.clips}
    clip_defaults = {}
    if "idle" in clips:
        clip_defaults["Idle"] = "idle"
    if "walk" in clips:
        clip_defaults["Walk"] = "walk"
        clip_defaults["StartWalk"] = "walk"
    if "run" in clips:
        clip_defaults["Run"] = "run"
    if "attack" in clips:
        clip_defaults["Attack"] = "attack"
    if "death" in clips:
        clip_defaults["Death"] = "death"

    return {
        "id": plan_id,
        "morphology": morphology,
        "gaitClass": override.get("gait_class",
                                  _GAIT_BY_MORPHOLOGY.get(morphology, "biped_fsm")),
        "rootBone": root,
        "hipAliases": ["pelvis", "hip"],
        "gripBone": grip,
        "capsule": {"mode": "xz_extent", "minHalfWidth": 0.2,
                    "maxHalfWidth": max_half},
        "legs": legs,
        "segments": segments,
        "clipDefaults": clip_defaults,
    }
