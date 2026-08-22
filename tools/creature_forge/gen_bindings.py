#!/usr/bin/env python3
"""Generate resources/monsters/visuals/bindings.json from bindings_map.json.

336 hand-written binding entries would drift the moment a stat block moved, so
the checked-in bindings file is GENERATED from one reuse map (archetype rig ->
the stat blocks it embodies, with per-member tint/scale/alpha overrides) and
validated on the way out:

  * every stat-block id in resources/monsters/ is bound exactly once
  * every referenced rig exists and can actually play Idle/Walk/Attack/Death
    through the engine's real resolution (literal clip, the humanoid
    death_front/back probe, the unarmed moveset, or an animationMapping) —
    this is what keeps a walk-only *_meshy or the flight-only monster_dragon
    from being bound, where the FSM would hold a stale clip while damage still
    fires (a T-posing monster that hurts you)
  * tint/alpha stay in range

`approx` on a member records that it is riding a stand-in archetype until a
later wave gives it a dedicated rig — coverage is complete now, fidelity
improves per wave. The report prints what is still approximated.

Usage:
  python tools/creature_forge/gen_bindings.py            # write + report
  python tools/creature_forge/gen_bindings.py --check    # verify, write nothing
  python tools/creature_forge/gen_bindings.py --out X    # write elsewhere
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAP_PATH = Path(__file__).parent / "bindings_map.json"
MONSTER_DIR = ROOT / "resources" / "monsters"
DEFAULT_OUT = MONSTER_DIR / "visuals" / "bindings.json"

# How the engine actually resolves each state (AnimatedVoxelCharacter::die()
# probes death_front/back; CombatBehavior installs the unarmed moveset).
STATE_CLIPS = {
    "Idle":   ("idle",),
    "Walk":   ("walk",),
    "Attack": ("attack", "boxing", "elbow_punch", "kick", "punch"),
    "Death":  ("death", "death_front", "death_back"),
}


def stat_block_ids() -> set:
    ids = set()
    for f in sorted(MONSTER_DIR.glob("*.json")):
        data = json.loads(f.read_text(encoding="utf-8"))
        entries = data if isinstance(data, list) else data.get("monsters", [])
        for m in entries:
            if isinstance(m, dict) and m.get("id"):
                ids.add(m["id"])
    return ids


def rig_clips(anim_path: Path) -> set:
    """Clip names only — the MODEL section is far too big to parse for this."""
    out = set()
    for line in anim_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("ANIMATION "):
            out.add(line.split(None, 1)[1].strip().lower())
    return out


def build(spec: dict):
    """Return (bindings, errors, approx_by_target)."""
    bindings, errors, approx = {}, [], {}
    seen = {}
    clip_cache = {}

    for arch_id, arch in sorted(spec["archetypes"].items()):
        anim_rel = arch["animFile"]
        anim_abs = ROOT / anim_rel
        if not anim_abs.exists():
            errors.append(f"archetype '{arch_id}': missing rig {anim_rel}")
            continue
        if anim_rel not in clip_cache:
            clip_cache[anim_rel] = rig_clips(anim_abs)
        clips = clip_cache[anim_rel]

        for mid, ov in sorted(arch["members"].items()):
            if ov.get("skip"):
                continue
            if mid in seen:
                errors.append(f"'{mid}' bound twice ({seen[mid]} and {arch_id})")
                continue
            seen[mid] = arch_id

            # Archetype-level mapping is the default for its members (the
            # Quaternius fauna pack names its attack Attack_Headbutt, so the
            # whole family needs the same override); member entries merge over.
            mapping = dict(arch.get("animationMapping") or {})
            mapping.update(ov.get("animationMapping") or {})
            for state, accepted in STATE_CLIPS.items():
                if any(c in clips for c in accepted):
                    continue
                if mapping.get(state, "").lower() in clips:
                    continue
                errors.append(
                    f"'{mid}' -> {Path(anim_rel).name} cannot play {state}")

            entry = {"animFile": anim_rel,
                     "faction": ov.get("faction", arch.get("faction", "monsters"))}
            if mapping:
                entry["animationMapping"] = mapping
            tint = ov.get("tint")
            if tint:
                if len(tint) != 3 or not all(0.0 <= c <= 2.0 for c in tint):
                    errors.append(f"'{mid}': tint out of range {tint}")
                entry["tint"] = tint
            if "alpha" in ov:
                a = ov["alpha"]
                if not (0.05 <= a <= 1.0):
                    errors.append(f"'{mid}': alpha out of range {a}")
                entry["alpha"] = a
            # `scale` is shorthand: the engine already applies appearance
            # heightScale/bulkScale to bone lengths, so a size ladder needs no
            # engine work — but it does NOT scale the collision capsule, which
            # is clamped by the body plan. Gargantuan creatures therefore need
            # their own per-size-tier rig build, not just a scale here.
            appearance = dict(ov.get("appearance") or {})
            if "scale" in ov:
                appearance.setdefault("heightScale", ov["scale"])
                appearance.setdefault("bulkScale", ov["scale"])
            if appearance:
                entry["appearance"] = appearance
            bindings[mid] = entry

            if ov.get("approx"):
                approx.setdefault(ov["approx"], []).append(mid)

    known = stat_block_ids()
    for mid in sorted(set(bindings) - known):
        errors.append(f"'{mid}' has a binding but no stat block")
    missing = sorted(known - set(bindings))
    return bindings, errors, approx, known, missing


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--check", action="store_true",
                    help="validate only; write nothing")
    args = ap.parse_args(argv)

    spec = json.loads(MAP_PATH.read_text(encoding="utf-8"))
    bindings, errors, approx, known, missing = build(spec)

    print(f"coverage: {len(bindings)}/{len(known)} stat blocks bound")
    if missing:
        print(f"  UNBOUND ({len(missing)}): {', '.join(missing[:20])}"
              + (" ..." if len(missing) > 20 else ""))
    if approx:
        total = sum(len(v) for v in approx.values())
        print(f"  approximations pending dedicated rigs ({total}):")
        for target, ids in sorted(approx.items()):
            print(f"    {target:18} {len(ids):3}  {', '.join(sorted(ids)[:6])}"
                  + (" ..." if len(ids) > 6 else ""))
    for e in errors:
        print(f"ERROR: {e}")
    if errors or missing:
        print("REFUSED: bindings not written")
        return 1

    out = {"_comment": ("GENERATED by tools/creature_forge/gen_bindings.py from "
                        "bindings_map.json — do not hand-edit; edit the map and "
                        "regenerate. Maps a monster stat-block id to its rig, "
                        "tint/alpha (whole-character color multiply + opacity), "
                        "appearance scale, and combat faction.")}
    out.update({k: bindings[k] for k in sorted(bindings)})
    if not args.check:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
