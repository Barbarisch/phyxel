#!/usr/bin/env python3
"""Generate the Bestiary Hall roster: one entry per DISTINCT rig.

The hall is a demo/regression stage that shows every creature rig in the
bestiary at once, each labelled, each able to play every clip. Its roster is
GENERATED rather than hand-written for the same reason bindings.json is: 336
stat blocks collapse onto ~46 rigs, and any hand-maintained list of those rigs
silently goes stale the moment a rig is added, renamed, or retired.

Two things here are measured rather than guessed:

* **Footprint.** Each rig's bind-pose AABB is computed by forward kinematics
  over the actual .anim (bones are local-to-parent; boxes are bone-local).
  The hall spaces creatures by their real width and puts each nameplate just
  above its real height. Guessing these from `target_height` would work for
  forge rigs and fail for every hand-authored one, and a tarrasque is 7 units
  next to a 0.3-unit swarm.

* **Clips.** Which of idle/walk/attack/death a rig can actually play, resolved
  the way the ENGINE resolves them (literal clip, else the humanoid
  death_front/death_back and unarmed-moveset fallbacks). The panel greys out
  what a rig genuinely cannot do instead of firing a state change that
  silently leaves it in T-pose.

Usage:  python tools/creature_forge/gen_hall.py [--out PATH] [--check]
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from anim_pipeline import anim_format  # noqa: E402

BINDINGS_MAP = ROOT / "tools" / "creature_forge" / "bindings_map.json"
BESTIARY = ROOT / "tools" / "creature_forge" / "bestiary.json"
DEFAULT_OUT = ROOT / "resources" / "monsters" / "visuals" / "bestiary_hall.json"

#: Archetype id prefix/substring -> hall category. First match wins, so the
#: order is meaningful. Categories only group the panel's tree; they carry no
#: engine meaning.
CATEGORY_RULES = [
    # "dragon turtle" contains "dragon", so the turtle rule MUST precede it.
    ("turtle", "Exotics"),
    ("dragon", "Dragons"),
    ("hydra", "Exotics"),
    ("tarrasque", "Exotics"),
    ("xorn", "Exotics"),
    ("otyugh", "Exotics"),
    ("devil", "Fiends & Celestials"),
    ("demon", "Fiends & Celestials"),
    ("angel", "Fiends & Celestials"),
    ("hag", "Fiends & Celestials"),
    ("mushroom", "Oozes & Plants"),
    ("frog", "Beasts"),
    ("dino", "Beasts"),
    ("yeti", "Humanoids"),
    ("tribal", "Humanoids"),
    ("elemental", "Constructs & Elementals"),
    ("golem", "Constructs & Elementals"),
    ("ooze", "Oozes & Plants"),
    ("plant", "Oozes & Plants"),
    ("swarm", "Vermin & Swarms"),
    ("beetle", "Vermin & Swarms"),
    ("scorpion", "Vermin & Swarms"),
    ("spider", "Vermin & Swarms"),
    ("rodent", "Vermin & Swarms"),
    ("bat", "Vermin & Swarms"),
    ("shark", "Aquatic"),
    ("cephalopod", "Aquatic"),
    ("serpent", "Aquatic"),
    ("humanoid", "Humanoids"),
    ("goblin", "Humanoids"),
    ("orc", "Humanoids"),
    ("skeleton", "Humanoids"),
    ("zombie", "Humanoids"),
    ("troll", "Humanoids"),
    ("giant", "Humanoids"),
    ("ogre", "Humanoids"),
    ("taur", "Humanoids"),
]
FALLBACK_CATEGORY = "Beasts"

#: The four states the hall can drive, and the engine's fallback chain for
#: each. Mirrors the resolution the binding generator already validates.
CLIP_FALLBACKS = {
    "Idle":   [["idle"]],
    "Walk":   [["walk"], ["move"], ["run"]],
    "Attack": [["attack"], ["attack_unarmed"], ["punch"]],
    "Death":  [["death"], ["death_front"], ["death_back"]],
}


# ---------------------------------------------------------------------------
# geometry


def _quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def _quat_rot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    # t = 2 * cross(q.xyz, v)
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx))


def bind_footprint(af) -> dict:
    """Bind-pose AABB of the rig, in rig units.

    Bones are local-to-parent, so this walks the hierarchy accumulating
    rotation and translation; boxes are bone-local centers with full sizes.
    Bone scale is applied to the offset chain because variant rigs (the
    humanoid family) express body shape through per-bone scale.
    """
    world = {}   # bone_id -> (pos, rot, scale)
    for b in af.bones:
        if b.parent_id < 0 or b.parent_id not in world:
            world[b.id] = (b.pos, b.rot, b.scale)
            continue
        ppos, prot, pscale = world[b.parent_id]
        scaled = (b.pos[0] * pscale[0], b.pos[1] * pscale[1], b.pos[2] * pscale[2])
        off = _quat_rot(prot, scaled)
        world[b.id] = (
            (ppos[0] + off[0], ppos[1] + off[1], ppos[2] + off[2]),
            _quat_mul(prot, b.rot),
            (pscale[0] * b.scale[0], pscale[1] * b.scale[1], pscale[2] * b.scale[2]),
        )

    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    for box in af.boxes:
        if box.bone_id not in world:
            continue
        bpos, brot, bscale = world[box.bone_id]
        c = (box.center[0] * bscale[0], box.center[1] * bscale[1], box.center[2] * bscale[2])
        c = _quat_rot(brot, c)
        cx, cy, cz = bpos[0] + c[0], bpos[1] + c[1], bpos[2] + c[2]
        # Half-extents stay axis-aligned: an exact OBB is overkill for spacing,
        # and this errs LARGE, which is the safe direction for a layout bound.
        hx = abs(box.size[0] * bscale[0]) * 0.5
        hy = abs(box.size[1] * bscale[1]) * 0.5
        hz = abs(box.size[2] * bscale[2]) * 0.5
        for i, (c_i, h_i) in enumerate(((cx, hx), (cy, hy), (cz, hz))):
            lo[i] = min(lo[i], c_i - h_i)
            hi[i] = max(hi[i], c_i + h_i)

    if any(math.isinf(v) for v in lo + hi):
        return {"width": 1.0, "height": 1.8, "depth": 1.0, "footY": 0.0}
    return {
        "width":  round(hi[0] - lo[0], 3),
        "height": round(hi[1] - lo[1], 3),
        "depth":  round(hi[2] - lo[2], 3),
        # Distance from the rig origin down to its lowest voxel. The hall adds
        # this back so a rig whose origin is not at its feet still stands ON
        # the floor instead of sinking into or hovering above it.
        "footY":  round(lo[1], 3),
    }


# ---------------------------------------------------------------------------
# roster


def category_for(arch_id: str, anim_stem: str) -> str:
    hay = (arch_id + " " + anim_stem).lower()
    for needle, cat in CATEGORY_RULES:
        if needle in hay:
            return cat
    return FALLBACK_CATEGORY


def display_names() -> dict:
    """anim path -> human display name, from the bestiary manifest."""
    out = {}
    for e in json.loads(BESTIARY.read_text(encoding="utf-8")):
        if isinstance(e, dict) and "out" in e and e.get("display"):
            out[e["out"].replace("\\", "/")] = e["display"]
    return out


def titleize(stem: str) -> str:
    s = stem[len("forge_"):] if stem.startswith("forge_") else stem
    return " ".join(w.capitalize() for w in s.split("_"))


def resolve_clips(af, mapping=None) -> dict:
    """Resolve each hall state to a playable clip.

    `mapping` is the binding archetype's animationMapping -- it must be
    consulted FIRST, because that is what the engine applies at spawn. The
    roster once ignored it and reported the stag as unable to attack while the
    staged stag could headbutt fine (its binding maps Attack ->
    Attack_Headbutt); playState then skipped a perfectly capable rig.
    """
    have = {c.name.lower(): c.name for c in af.clips}
    out = {}
    for state, chains in CLIP_FALLBACKS.items():
        pick = ""
        mapped = (mapping or {}).get(state, "")
        if mapped and mapped.lower() in have:
            pick = have[mapped.lower()]
        for chain in ([] if pick else chains):
            for name in chain:
                if name in have:
                    pick = have[name]
                    break
            if pick:
                break
        out[state] = pick
    return out


def build() -> tuple[dict, list]:
    spec = json.loads(BINDINGS_MAP.read_text(encoding="utf-8"))
    names = display_names()
    errors = []

    # One hall entry per distinct rig. Several archetypes can share a rig
    # (giants ride the ogre); the FIRST archetype naming it wins the label,
    # and its member count is summed across all of them so the panel can show
    # how much of the bestiary each rig actually carries.
    by_anim: dict[str, dict] = {}
    for arch_id, arch in spec["archetypes"].items():
        anim = arch.get("animFile", "").replace("\\", "/")
        members = [m for m in arch.get("members", {})
                   if not arch["members"][m].get("skip")]
        if not anim:
            errors.append(f"archetype '{arch_id}' has no animFile")
            continue
        e = by_anim.setdefault(anim, {"archetypes": [], "monsters": [], "repr": "",
                                      "_repr_scale": -1.0, "mapping": {}})
        # archetype-level mapping (member-level merges are per-monster; the
        # hall stages one creature per rig, so archetype level is the truth
        # that matters here)
        e["mapping"].update(arch.get("animationMapping") or {})
        e["archetypes"].append(arch_id)
        e["monsters"].extend(members)
        # Representative = the LARGEST member, not the first one listed. It
        # supplies the hall's tint, and an archetype named "Great Cat" whose
        # representative is the housecat gets a housecat's colouring. Size is
        # the best available proxy for "most characteristic of this archetype".
        for m in members:
            s = float(arch["members"][m].get("scale", 1.0))
            if s > e["_repr_scale"]:
                e["_repr_scale"], e["repr"] = s, m

    entries = []
    unbound = []
    for anim, info in sorted(by_anim.items()):
        if not info["monsters"]:
            # A rig in the library that no live stat block uses (placeholder
            # archetypes whose members are all `skip`). The hall shows THE
            # BESTIARY, so these stay out of the stage — but they get reported,
            # because a rig nothing references is either dead weight or a
            # binding someone forgot to write.
            unbound.append(Path(anim).stem)
            continue
        path = ROOT / anim
        if not path.exists():
            errors.append(f"rig file missing: {anim}")
            continue
        try:
            af = anim_format.parse(path)
        except Exception as exc:                       # noqa: BLE001
            errors.append(f"{anim}: unparseable ({exc})")
            continue

        stem = Path(anim).stem
        clips = resolve_clips(af, info.get("mapping"))
        missing = [s for s, c in clips.items() if not c]
        entries.append({
            "id": stem,
            "name": names.get(anim, titleize(stem)),
            "category": category_for(info["archetypes"][0], stem),
            "animFile": anim,
            "representative": info["repr"],
            "statBlocks": len(info["monsters"]),
            "archetypes": sorted(info["archetypes"]),
            "bind": bind_footprint(af),
            "clips": clips,
            "boxes": len(af.boxes),
            "bones": len(af.bones),
        })
        if missing:
            # Not fatal: the hall greys these out. It IS worth reporting,
            # because a rig that cannot die is a rig no encounter should use.
            errors.append(f"{stem}: cannot play {', '.join(missing)} (will be greyed out)")

    entries.sort(key=lambda e: (e["category"], e["name"]))
    roster = {
        "_comment": ("GENERATED by tools/creature_forge/gen_hall.py -- do not hand-edit. "
                     "One entry per distinct rig; 'bind' is the measured bind-pose AABB "
                     "in rig units and 'clips' is the ENGINE-resolved clip per state."),
        "entries": entries,
    }
    return roster, errors, unbound


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--check", action="store_true",
                    help="verify the checked-in roster matches a fresh build")
    args = ap.parse_args(argv)

    roster, errors, unbound = build()
    hard = [e for e in errors if "greyed out" not in e]

    print(f"bestiary hall: {len(roster['entries'])} rigs "
          f"({sum(e['statBlocks'] for e in roster['entries'])} stat blocks)")
    by_cat: dict[str, int] = {}
    for e in roster["entries"]:
        by_cat[e["category"]] = by_cat.get(e["category"], 0) + 1
    for cat, n in sorted(by_cat.items()):
        print(f"  {n:3d}  {cat}")
    if unbound:
        print(f"  note: {len(unbound)} rig(s) in the library carry no stat block "
              f"and are not staged: {', '.join(sorted(unbound))}")
    for e in errors:
        print(f"  WARN: {e}")
    if hard:
        print(f"REFUSED: {len(hard)} unusable rig(s)")
        return 1

    text = json.dumps(roster, indent=2) + "\n"
    out = Path(args.out)
    if args.check:
        if not out.exists():
            print(f"REFUSED: {out} does not exist")
            return 1
        if out.read_text(encoding="utf-8") != text:
            print(f"REFUSED: {out} is stale -- re-run gen_hall.py")
            return 1
        print("roster is current")
        return 0

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
