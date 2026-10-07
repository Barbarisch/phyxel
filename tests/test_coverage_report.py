"""Roadmap R1 (docs/CharacterAnimationRoadmap.md): the coverage scoreboard + generated checklist.

The hand-count expectations in test_hand_counted_rows were derived from the RAW data (the engine
fixture's state table, the rig clip lists, the stat blocks) before the report's rows were read,
2026-10-01. No catalogue rig uses gait class `flying_clips` (only the unbound wyvern), so the ten
rows span the four gait classes that exist."""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.anim_pipeline import coverage_report as cr  # noqa: E402

OK, DRAFT, MISSING = cr.OK, cr.DRAFT, cr.MISSING
_REP = None


def rep():
    global _REP
    if _REP is None:
        _REP = cr.build()
    return _REP


def row(kind, rid):
    return next(r for r in rep()["rows"] if r["kind"] == kind and r["id"] == rid)


def marks(r):
    return {k: v["mark"] for k, v in r["needs"].items()}


# (a) every catalogue entry gets exactly one row; nothing silently dropped
def test_one_row_per_catalogue_entry_and_nothing_dropped():
    mons = [m for p in (ROOT / "resources/monsters").glob("*.json") for m in json.loads(p.read_text(encoding="utf-8"))]
    races = list((ROOT / "resources/races").glob("*.json"))
    classes = list((ROOT / "resources/classes").glob("*.json"))
    rows = rep()["rows"]
    assert len(rows) == len(mons) + len(races) + len(classes) == 358
    assert len({(r["kind"], r["id"]) for r in rows}) == len(rows)
    t = rep()["totals"]
    assert t["needs"] == t[OK] + t[DRAFT] + t[MISSING]
    # every attack is either classified into a need or listed — count reconciles per monster
    for m in mons:
        r = row("monster", m["id"])
        kinds, uncl = cr.attack_requirements(m, cr.AttackClassifier())
        assert sorted(uncl) == sorted(r["unclassified"])
        assert {f"attack:{k}" for k in kinds} <= set(r["needs"])


# (b) RED test + control: a creature with two attack kinds on one generic clip gets one stand-in
#     and one hole; a humanoid weapon user is covered by the typed melee clips.
def test_two_attack_kinds_on_one_generic_clip_show_a_hole_and_humanoid_melee_is_covered():
    owl = marks(row("monster", "owlbear"))
    assert owl["attack:beak"] == DRAFT, "the generic `attack` clip stands in for the first kind only"
    assert owl["attack:claw"] == MISSING
    assert marks(row("monster", "guard"))["attack:weapon_melee"] == OK, "control: typed humanoid melee clips count"


# (c) hand-counted rows (expectations derived from raw data first; see module docstring)
EXPECTED = {
    ("monster", "guard"): (1.0, {"attack:weapon_melee": OK, "state:Death": OK, "state:HitReact": OK, "state:SitDown": OK}),
    ("monster", "adult-red-dragon"): (3 / 14, {"state:Idle": OK, "state:Walk": OK, "state:Death": OK, "state:Run": MISSING,
                                               "state:HitReact": MISSING, "attack:bite": DRAFT, "attack:claw": MISSING,
                                               "attack:tail": MISSING, "attack:aura": MISSING, "attack:breath": MISSING,
                                               "move:fly": MISSING, "move:climb": MISSING}),
    ("monster", "giant-spider"): (3 / 9, {"attack:bite": DRAFT, "move:climb": MISSING, "state:Death": OK}),
    # R2 import 2026-10-02: dire_wolf.anim (forge dire_wolf body, clips idle/attack/death/walk) replaced
    # wolf_meshy (idle/walk/run/attack/death, but its death never resolved): Death now OK, Run lost
    # until R3 gives the species a run clip
    ("monster", "dire-wolf"): (3 / 8, {"state:Run": MISSING, "state:Death": OK, "attack:bite": DRAFT}),
    ("monster", "owlbear"): (3 / 9, {"state:Run": MISSING, "attack:beak": DRAFT, "attack:claw": MISSING}),
    # orc: the R2 import (orc.anim, 21 / 21) was REFUSED by the oracle gate 2026-10-02 (wide stance:
    # idle foot dip + walk skate) and the binding went back to the Quaternius monster_orc
    ("monster", "orc"): (8 / 21, {"state:Jump": OK, "state:Fall": OK, "state:TurnLeft": MISSING, "state:SitDown": MISSING,
                                  "attack:weapon_melee": DRAFT, "attack:weapon_ranged": MISSING}),
    ("monster", "pseudodragon"): (3 / 10, {"attack:bite": DRAFT, "attack:sting": MISSING, "move:fly": MISSING}),
    ("monster", "giant-octopus"): (3 / 9, {"attack:tentacle": DRAFT, "move:swim": MISSING}),
    ("race", "dwarf_mountain"): (19 / 20, {"state:ClimbStairs": OK, "style": MISSING}),
    ("class", "fighter"): (2 / 3, {"attack:weapon_melee": OK, "attack:weapon_ranged": OK, "action:second_wind": MISSING}),
    ("class", "monk"): (3 / 4, {"action:unarmed_combo": OK, "action:deflect": MISSING}),
}


def test_hand_counted_rows():
    for (kind, rid), (score, some) in EXPECTED.items():
        r = row(kind, rid)
        assert abs(r["score"] - score) < 1e-3, f"{rid}: score {r['score']} vs hand {score:.4f} — needs {marks(r)}"
        for need, m in some.items():
            assert r["needs"][need]["mark"] == m, f"{rid} {need}: {r['needs'][need]['mark']} vs hand {m}"


# (d) deterministic output
def test_two_runs_are_byte_identical():
    a = cr.build(); b = cr.build()
    assert json.dumps(a, sort_keys=True, ensure_ascii=False) == json.dumps(b, sort_keys=True, ensure_ascii=False)
    assert cr.to_markdown(a) == cr.to_markdown(b)


# (e) the engine fixture is not stale against the rig files / catalogue
def test_engine_fixture_matches_the_rig_files_and_the_catalogue():
    fix = json.loads(cr.FIXTURE.read_text(encoding="utf-8"))
    rigs = {"resources/animated_characters/humanoid.anim"}
    b = json.loads((ROOT / "resources/monsters/visuals/bindings.json").read_text(encoding="utf-8"))
    rigs |= {v["animFile"] for k, v in b.items() if isinstance(v, dict) and "animFile" in v}
    for p in (ROOT / "resources/races").glob("*.json"):
        vis = json.loads(p.read_text(encoding="utf-8")).get("visual") or {}
        if "animFile" in vis:
            rigs.add(vis["animFile"])
    assert set(fix) == rigs, "catalogue rigs changed — rerun the stress test with PHYXEL_WRITE_CLIP_FIXTURE=1"
    for rig, v in fix.items():
        names = sorted({m.group(1) for m in re.finditer(r"^ANIMATION (\S+)", (ROOT / rig).read_text(encoding="utf-8", errors="ignore"), re.M)})
        assert v["clips"] == names, f"{rig}: clip list changed — regenerate the fixture"


# the committed checklist is what the data produces today
def test_committed_checklist_is_current():
    assert cr.main(["--check"]) == 0
