"""A2 (docs/AnimationSystemV3Plan.md): no literal bone or clip names in the character runtime.

Owner decision A2-1 (a): the HUMANOID CLIP TABLE stays in code — inside the member-driven clip
pickers listed in ALLOWED_FUNCTIONS (clipForState's legacy switch, the dodge/hit/death pickers,
state<->string parsing). Everywhere else a bone or clip is named by its ROLE through the
BodyPlan / the resolved clip, never by a literal: `mixamorig`, "Hips", "Spine", "Head",
"idle", "walk", "sitting_idle", ... are the two-roots / three-selection-sites / humanoid-only
bugs waiting to recur. RED on 2026-09-29: 18 lines.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AVC = ROOT / "engine" / "src" / "scene" / "AnimatedVoxelCharacter.cpp"

# The legacy humanoid table, by decision (a): literals are allowed INSIDE these members only.
ALLOWED_FUNCTIONS = {
    "clipForState", "selectDodgeClip", "selectHitClip", "selectDodgeClipFor", "die",
    "stateToString", "stringToState", "parseState", "stateFromString", "configureAnimationFixes",
    "drawSegmentBoxDebug",   # debug colouring of segment boxes by name — not runtime behaviour
}

LITERALS = re.compile(
    r'mixamorig|"Hips"|"Spine"|"Head"|"idle"|"walk"|"walking"|"unarmed_walk"|'
    r'"sitting_idle"|"stand_to_sit"|"sit_to_stand"|"Standing"|"standing"|"boxing"|"run"|'
    r'find\("(?:idle|walk|run|boxing|Spine|Head|Hips)"\)'
)
MEMBER_DEF = re.compile(r"^\s{4}\S.*\bAnimatedVoxelCharacter::(\w+)\s*\(")


def _offending_lines():
    current = None
    for n, line in enumerate(AVC.read_text(encoding="utf-8").splitlines(), 1):
        m = MEMBER_DEF.match(line)
        if m:
            current = m.group(1)
        code = line.split("//")[0]
        if not LITERALS.search(code):
            continue
        if current in ALLOWED_FUNCTIONS:
            continue
        yield n, current, line.strip()


def test_no_literal_bone_or_clip_names_outside_the_legacy_table():
    hits = [f"{n} ({fn}): {txt[:110]}" for n, fn, txt in _offending_lines()]
    assert not hits, "literal bone/clip names in the character runtime:\n" + "\n".join(hits)


# A3 item 3 fold-in (2026-09-30): the posture lean must walk the PLAN's spine chain, not the
# lower-case substring "spine". The appearance-PROPORTION heuristics (getLimbScales, the belly
# shaping in buildBodiesFromModel, buildSegmentBoxes minimums) still key on lower-case name
# substrings by design of the preset system — logged as an open item in AnimationSystemV3Plan.md
# §4b, not covered here. RED 2026-09-30.
LOWER_LITERAL = re.compile(r'find\("(?:spine|head|hips|neck)"\)')


def _function_body(name: str) -> str:
    lines = AVC.read_text(encoding="utf-8").splitlines()
    out, inside = [], False
    for line in lines:
        m = MEMBER_DEF.match(line)
        if m:
            if inside:
                break
            inside = (m.group(1) == name)
        if inside:
            out.append(line.split("//")[0])
    return "\n".join(out)


def test_posture_lean_reads_the_plan_spine_chain_not_a_name_substring():
    body = _function_body("applyPostureLean")
    assert body, "applyPostureLean not found"
    assert not LOWER_LITERAL.search(body), "applyPostureLean still finds the spine by name substring"
