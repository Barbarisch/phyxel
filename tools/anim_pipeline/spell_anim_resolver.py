"""Reference resolver: SpellDefinition -> casting animation plan.

This is the Python mirror of what the engine-side integration (Phase 3 C++)
will do: given a spell id + caster proficiency, produce the concrete clip
sequence, playback speeds, loop count, and the absolute release time at which
the VFX/projectile should fire.

Usage:
  python spell_anim_resolver.py                 # table for every known spell
  python spell_anim_resolver.py fireball        # plan for one spell
  python spell_anim_resolver.py fireball --proficiency 5
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from anim_format import parse  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
SPELL_DIR = REPO / "resources" / "spells"
FAMILIES_JSON = SPELL_DIR / "anim" / "spell_anim_families.json"
ANIM_FILE = REPO / "resources" / "animated_characters" / "humanoid.anim"


def load_spells() -> dict:
    spells = {}
    for f in sorted(SPELL_DIR.glob("*.json")):
        if f.name == "spell_anim_families.json":
            continue
        for s in json.loads(f.read_text(encoding="utf-8")):
            spells[s["id"]] = s
    return spells


def rule_matches(cond: dict, spell: dict) -> bool:
    ct = spell.get("castingTime", "Action")
    if "castingTimeIn" in cond and ct not in cond["castingTimeIn"]:
        return False
    if "castingTime" in cond and ct != cond["castingTime"]:
        return False
    if "isTouch" in cond and bool(spell.get("isTouch", False)) != cond["isTouch"]:
        return False
    if "isSelf" in cond and bool(spell.get("isSelf", False)) != cond["isSelf"]:
        return False
    if "resolutionType" in cond and spell.get("resolutionType") != cond["resolutionType"]:
        return False
    if "minRangeFeet" in cond and spell.get("rangeInFeet", 0) < cond["minRangeFeet"]:
        return False
    if "heals" in cond:
        heals = bool(spell.get("healDice") or spell.get("healBase"))
        if heals != cond["heals"]:
            return False
    return True


def resolve_family(spell: dict, cfg: dict) -> str:
    override = cfg.get("spellOverrides", {}).get(spell["id"])
    if override:
        return override
    for rule in cfg["rules"]:
        if rule_matches(rule.get("if", {}), spell):
            return rule["family"]
    return "thrust"


def resolve_plan(spell: dict, cfg: dict, clip_durations: dict, clip_release: dict,
                 proficiency_bonus: int = 2) -> dict:
    """Returns {family, segments: [{clip, speed, loops}], totalSeconds, releaseAtSeconds}."""
    family = resolve_family(spell, cfg)
    fam = cfg["families"][family]
    target = cfg["castingTimeTargets"].get(spell.get("castingTime", "Action"), 1.6)
    lo, hi = cfg["playbackRateRange"]
    skill = 1.0 + cfg.get("skillRatePerProficiency", 0.0) * (proficiency_bonus - 2)

    if "loop" in fam:  # ritual: structural fit, fixed-speed bookends
        windup_d = clip_durations[fam["windup"]]
        loop_d = clip_durations[fam["loop"]]
        release_d = clip_durations[fam["release"]]
        speed = max(lo, min(hi, skill))
        loops = max(1, round((target - (windup_d + release_d) / speed) / (loop_d / speed)))
        total = (windup_d + loops * loop_d + release_d) / speed
        release_at = (windup_d + loops * loop_d) / speed + \
            clip_release.get(fam["release"], 0.5) * release_d / speed
        segments = [
            {"clip": fam["windup"], "speed": round(speed, 3), "loops": 1},
            {"clip": fam["loop"], "speed": round(speed, 3), "loops": loops},
            {"clip": fam["release"], "speed": round(speed, 3), "loops": 1},
        ]
    else:
        clip = fam["cast"]
        d = clip_durations[clip]
        speed = max(lo, min(hi, (d / target) * skill))
        total = d / speed
        release_at = clip_release.get(clip, 0.5) * total
        segments = [{"clip": clip, "speed": round(speed, 3), "loops": 1}]

    return {
        "family": family,
        "segments": segments,
        "totalSeconds": round(total, 2),
        "releaseAtSeconds": round(release_at, 2),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description="Resolve spell -> casting animation plan")
    ap.add_argument("spell", nargs="?", help="spell id (default: table of all spells)")
    ap.add_argument("--proficiency", type=int, default=2, help="caster proficiency bonus (2-6)")
    args = ap.parse_args(argv)

    cfg = json.loads(FAMILIES_JSON.read_text(encoding="utf-8"))
    spells = load_spells()
    af = parse(ANIM_FILE)
    clip_durations = {c.name: c.duration for c in af.clips}
    clip_release = {}
    for c in af.clips:
        meta = af.clip_meta(c.name)
        if meta and "releaseFrame" in meta:
            clip_release[c.name] = float(meta["releaseFrame"])

    targets = [args.spell] if args.spell else sorted(spells)
    for sid in targets:
        if sid not in spells:
            print(f"unknown spell '{sid}'")
            return 1
        plan = resolve_plan(spells[sid], cfg, clip_durations, clip_release, args.proficiency)
        seg_str = " + ".join(
            (f"{s['clip']}x{s['loops']}" if s["loops"] > 1 else s["clip"]) + f"@{s['speed']}"
            for s in plan["segments"])
        print(f"{sid:22s} {plan['family']:10s} {plan['totalSeconds']:5.2f}s "
              f"release@{plan['releaseAtSeconds']:5.2f}s  {seg_str}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
