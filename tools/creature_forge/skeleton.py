"""Skeleton build: resolved joints + chains/attach/mirror -> ordered bone list.

Port of anyCreature engine/core/skeleton.js buildSkeleton. Parenting order:
  1. root chains (chains absent from `attach`), in spec order — first joint
     of the first root chain is the skeleton root (parent -1); each chain
     joint parents to the previous joint in its chain
  2. attached chains: first joint parents to the attach host joint
  3. mirrored chains: R twins at [-x, y, z] (or joints_R override), R root
     parented to the SAME host as the L root; registered as chains so the
     animation mirrorer can find them
  4. loose joints (in `joints` but in no chain): require an attach entry;
     an L-prefixed loose joint gets an auto R twin on the same host

Bind rotations are identity everywhere; local translation = child - parent.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .joints import mirror_name
from .spec import SpecError


@dataclass
class Skel:
    names: list = field(default_factory=list)        # bone name by index
    parents: list = field(default_factory=list)      # parent index by index (-1 root)
    world: dict = field(default_factory=dict)        # name -> (x, y, z) bind world pos
    index: dict = field(default_factory=dict)        # name -> bone index
    chains: dict = field(default_factory=dict)       # chain name -> [joint names] (incl. R twins)
    mirror_chain: dict = field(default_factory=dict)  # L chain name -> R chain name
    mirror_joint: dict = field(default_factory=dict)  # L joint name -> R joint name

    def add(self, name, parent_idx, pos):
        if name in self.index:
            raise SpecError(f"duplicate joint/bone name '{name}'")
        if " " in name:
            raise SpecError(f"joint name '{name}' contains a space (.anim names are single tokens)")
        self.index[name] = len(self.names)
        self.names.append(name)
        self.parents.append(parent_idx)
        self.world[name] = pos


def build(spec: dict, joints: dict, joints_r: dict) -> Skel:
    chains = spec["chains"]
    attach = spec.get("attach", {})
    mirror = spec.get("mirror", [])
    sk = Skel()
    sk.chains = {cn: list(names) for cn, names in chains.items()}

    def add_chain(names, first_parent):
        prev = first_parent
        for jn in names:
            if jn in sk.index:
                # a joint may appear in more than one chain (shared spine hub);
                # first placement wins, later chains just walk through it
                prev = sk.index[jn]
                continue
            sk.add(jn, prev, joints[jn])
            prev = sk.index[jn]

    # 1. root chains
    for cn in chains:
        if cn in attach:
            continue
        add_chain(chains[cn], -1)

    # 2. attached chains
    for cn in chains:
        if cn not in attach:
            continue
        host = attach[cn]
        if host not in sk.index:
            raise SpecError(
                f"chain '{cn}' attaches to '{host}' which is not indexed yet "
                f"(host joints must live in a root chain or an earlier chain)")
        add_chain(chains[cn], sk.index[host])

    # 3. mirrored chains
    for cn in mirror:
        lnames = chains[cn]
        rnames = [mirror_name(n) for n in lnames]
        rcn = mirror_name(cn)
        first_parent = sk.parents[sk.index[lnames[0]]]
        prev = first_parent
        for ln, rn in zip(lnames, rnames):
            lx, ly, lz = joints[ln]
            pos = joints_r.get(rn, (-lx, ly, lz))
            sk.add(rn, prev, pos)
            prev = sk.index[rn]
            sk.mirror_joint[ln] = rn
        sk.chains[rcn] = rnames
        sk.mirror_chain[cn] = rcn

    # 4. loose joints
    in_chain = {jn for names in sk.chains.values() for jn in names}
    for jn in spec["joints"]:
        if jn in sk.index or jn in in_chain:
            continue
        host = attach.get(jn)
        if host is None:
            raise SpecError(f"loose joint '{jn}' is in no chain and has no attach entry")
        if host not in sk.index:
            raise SpecError(f"loose joint '{jn}' attaches to un-indexed '{host}'")
        sk.add(jn, sk.index[host], joints[jn])
        if jn.startswith("L"):
            rn = mirror_name(jn)
            lx, ly, lz = joints[jn]
            sk.add(rn, sk.index[host], joints_r.get(rn, (-lx, ly, lz)))
            sk.mirror_joint[jn] = rn

    return sk


# ---------------------------------------------------------------------------
# Morphology advisor (mirror of engine CharacterAppearance detectMorphology)
# ---------------------------------------------------------------------------

def detect_morphology(bone_names) -> str:
    low = [n.lower() for n in bone_names]

    def has(sub):
        return any(sub in n for n in low)

    def exact(w):
        return w in low

    if has("leg1_coxa") and (exact("thorax") or exact("abdomen")):
        return "arachnid"
    if has("wing") and has("tail") and exact("neck_1"):
        return "dragon"
    if exact("pelvis") and (has("paw") or has("tail")) and exact("chest") and not has("wing"):
        return "quadruped"
    if has("frontleg") and has("backleg"):
        return "quadruped"
    if exact("hips") or exact("mixamorighips"):
        return "humanoid"
    return "unknown"
