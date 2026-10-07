#!/usr/bin/env python3
"""coverage_report.py — the R1 scoreboard (docs/CharacterAnimationRoadmap.md §4, R1).

For every D&D catalogue entry (each monster, each race, each class) it lists what that character
must be able to animate, and whether the shipped content delivers it:

  ✅  a shipped/approved clip the engine actually plays (states) or that is tagged for it
  ⚠️  only a draft clip, an untyped substitute (a generic `attack` clip standing in for a bite),
      or a clip that exists but the engine has no state to play it (e.g. a "fly" clip)
  ❌  nothing

Sources (nothing is hand-written per character):
  tests/fixtures/clip_resolution.json   what the ENGINE plays per rig + state (ClipResolutionFixtureTest)
  resources/animated_characters/*.anim  clip names + `# clip_meta:` header lines
  resources/anim/motion_vocabulary.json base state list per gait class
  resources/anim/attack_kinds.json      stat-block attack name -> attackKind
  resources/anim/class_actions.json     class -> weapon / cast / signature actions
  resources/monsters/*.json, visuals/bindings.json, resources/races/*.json, resources/classes/*.json

Outputs (deterministic: sorted rows, no timestamps):
  build/coverage/coverage_report.json
  docs/CharacterChecklist.md            generated — never edit by hand

    python tools/anim_pipeline/coverage_report.py [--json-out P] [--md-out P] [--check]
--check exits 1 if the committed checklist differs from what the data now produces.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests" / "fixtures" / "clip_resolution.json"
JSON_OUT = ROOT / "build" / "coverage" / "coverage_report.json"
MD_OUT = ROOT / "docs" / "CharacterChecklist.md"
HUMANOID = "resources/animated_characters/humanoid.anim"
OK, DRAFT, MISSING = "✅", "⚠️", "❌"
COUNTED = {"approved", "shipped"}
MOVEMENT_WORDS = {"fly": ("fly", "flight", "glide", "soar"), "swim": ("swim",), "climb": ("climb",),
                  "burrow": ("burrow", "dig"), "hover": ("hover",)}


def load(rel: str):
    return json.loads((ROOT / rel).read_text(encoding="utf-8"))


# ---------------------------------------------------------------------------------------------
# rigs: clip names + clip_meta
# ---------------------------------------------------------------------------------------------
_META = re.compile(r"^# clip_meta:\s+(\S+)\s*(.*)$")


def rig_meta(rig: str) -> dict:
    """{clip: {key: value}} from the `# clip_meta:` header lines (read only up to SKELETON)."""
    out = {}
    with open(ROOT / rig, encoding="utf-8", errors="ignore") as f:
        for line in f:
            if not line.startswith("#"):
                if line.startswith("SKELETON"):
                    break
                continue
            m = _META.match(line.rstrip("\n"))
            if m:
                kv = dict(tok.split("=", 1) for tok in m.group(2).split() if "=" in tok)
                out[m.group(1)] = kv
    return out


def review_of(meta: dict) -> str:
    return meta.get("review", "shipped")


# ---------------------------------------------------------------------------------------------
# requirement derivation
# ---------------------------------------------------------------------------------------------
class AttackClassifier:
    def __init__(self):
        self.rules = [(re.compile(r["match"], re.I), r["kind"]) for r in load("resources/anim/attack_kinds.json")["rules"]]

    def kind(self, attack: dict) -> str | None:
        name = attack.get("name", "")
        base = re.sub(r"\s*\(.*\)\s*$", "", name)          # "Claw (Fiend Form Only)" -> "Claw"
        for rx, kind in self.rules:
            if rx.search(base):
                if kind == "weapon":
                    return "weapon_ranged" if attack.get("isRanged") else "weapon_melee"
                return kind
        return None


def attack_requirements(monster: dict, clf: AttackClassifier):
    kinds, unclassified = [], []
    for a in monster.get("attacks", []):
        k = clf.kind(a)
        if k is None:
            unclassified.append(a.get("name", "?"))
        elif k not in kinds:
            kinds.append(k)
    return kinds, unclassified


def class_requirements(cls: dict, table: dict):
    needs, unmapped = [], []
    profs = set(cls.get("weaponProficiencies", []))
    if profs & set(table["weaponProficiency"]["melee"]):
        needs.append(("attack", "weapon_melee"))
    if profs & set(table["weaponProficiency"]["ranged"]):
        needs.append(("attack", "weapon_ranged"))
    if cls.get("spellcastingType"):
        needs.append(("action", "cast"))
    fmap = table["features"]
    for lvl in sorted(cls.get("features", {}), key=int):
        for feat in cls["features"][lvl]:
            name = feat.get("name", "")
            if name not in fmap:
                unmapped.append(name)
            elif fmap[name] and ("action", fmap[name]) not in needs:
                needs.append(("action", fmap[name]))
    return needs, unmapped


# ---------------------------------------------------------------------------------------------
# satisfaction
# ---------------------------------------------------------------------------------------------
def clips_for_state(state: str, rig_fix: dict, meta: dict):
    """Clips counting toward a state: the clip the engine resolves (if it exists and is not the idle
    stand-in for a non-idle state) plus any clip tagged `state=<state>` (variants)."""
    names = set(rig_fix["clips"])
    resolved = rig_fix["states"].get(state, "")
    found = []
    if resolved in names and (state == "Idle" or resolved != rig_fix["states"].get("Idle")):
        found.append(resolved)
    want = state.lower()
    for clip, kv in meta.items():
        if kv.get("state", "").lower() == want and clip in names and clip not in found:
            found.append(clip)
    return found


def clips_for_attack(kind: str, rig_fix: dict, meta: dict):
    names = set(rig_fix["clips"])
    found = []
    for clip, kv in meta.items():
        if clip not in names:
            continue
        if kv.get("attackKind") == kind:
            found.append(clip)
        elif kind == "weapon_melee" and kv.get("meleeFamily") and kv["meleeFamily"] != "unarmed":
            found.append(clip)
        elif kind == "weapon_ranged" and kv.get("weaponFamily"):
            found.append(clip)
        elif kind == "slam" and kv.get("meleeFamily") == "unarmed":
            found.append(clip)
    return found


def clips_for_action(action: str, rig_fix: dict, meta: dict):
    names = set(rig_fix["clips"])
    found = []
    for clip, kv in meta.items():
        if clip not in names:
            continue
        if kv.get("action") == action:
            found.append(clip)
        elif action == "cast" and kv.get("castFamily"):
            found.append(clip)
        elif action == "unarmed_combo" and kv.get("meleeFamily") == "unarmed":
            found.append(clip)
    return found


def mark(found: list, meta: dict, target: int, substitute: bool = False, unwired: bool = False):
    counted = [c for c in found if review_of(meta.get(c, {})) in COUNTED]
    drafts = [c for c in found if review_of(meta.get(c, {})) == "draft"]
    if counted:
        sym = OK
    elif drafts or substitute or unwired:
        sym = DRAFT
    else:
        sym = MISSING
    return {"mark": sym, "variants": len(counted), "target": target, "clips": sorted(counted + drafts)}


def movement_need(mode: str, rig_fix: dict, meta: dict):
    names = rig_fix["clips"]
    tagged = [c for c, kv in meta.items() if kv.get("state", "").lower() == mode and c in names]
    if tagged:
        return mark(tagged, meta, 1)
    lookalike = [c for c in names if any(w in c.lower() for w in MOVEMENT_WORDS[mode])]
    r = mark([], meta, 1, unwired=bool(lookalike))
    r["clips"] = sorted(lookalike)
    if lookalike:
        r["note"] = "clip exists but the engine has no state that plays it"
    return r


# ---------------------------------------------------------------------------------------------
# rows
# ---------------------------------------------------------------------------------------------
def model_source(rig: str) -> str:
    base = Path(rig).stem
    if rig == HUMANOID:
        return "humanoid-shared"
    if "_meshy" in base:
        return "meshy"
    if base.startswith("forge_"):
        return "forge"
    return "other"


def base_needs(rig_fix: dict, vocab: dict, meta: dict) -> dict:
    gait = rig_fix.get("gaitClass") or "biped_fsm"
    out = {}
    for state, target in vocab["gaitClasses"].get(gait, {}).items():
        out[f"state:{state}"] = mark(clips_for_state(state, rig_fix, meta), meta, target)
    return out


def monster_row(m: dict, binding: dict, fix: dict, metas: dict, vocab: dict, clf: AttackClassifier) -> dict:
    rig = binding.get("animFile", HUMANOID)
    rf, meta = fix[rig], metas[rig]
    needs = base_needs(rf, vocab, meta)
    kinds, unclassified = attack_requirements(m, clf)
    resolved_attack = rf["states"].get("Attack", "")
    generic_used = False
    for k in kinds:
        found = clips_for_attack(k, rf, meta)
        substitute = False
        if not found and resolved_attack in rf["clips"] and not generic_used:
            substitute = True; generic_used = True       # the generic `attack` clip stands in once
        r = mark(found, meta, vocab["attackVariantTarget"], substitute=substitute)
        if substitute:
            r["note"] = f"untyped generic clip '{resolved_attack}' stands in"
        needs[f"attack:{k}"] = r
    modes = m.get("movementModes")
    if modes is None:
        needs["movement"] = {"mark": DRAFT, "variants": 0, "target": 1, "clips": [], "note": "movement unverified"}
    else:
        for mode in ("fly", "swim", "climb", "burrow", "hover"):
            if modes.get(mode):
                needs[f"move:{mode}"] = movement_need(mode, rf, meta)
    return {"kind": "monster", "id": m["id"], "name": m["name"], "rig": rig, "plan": rf["plan"],
            "gaitClass": rf["gaitClass"], "model": model_source(rig), "needs": needs,
            "unclassified": unclassified}


def race_row(r: dict, fix: dict, metas: dict, vocab: dict) -> dict:
    rig = (r.get("visual") or {}).get("animFile", HUMANOID)
    rf, meta = fix[rig], metas[rig]
    needs = base_needs(rf, vocab, meta)
    needs["style"] = {"mark": MISSING, "variants": 0, "target": 1, "clips": [], "note": "movement style arrives in R4"}
    return {"kind": "race", "id": r["id"], "name": r["name"], "rig": rig, "plan": rf["plan"],
            "gaitClass": rf["gaitClass"], "model": model_source(rig), "needs": needs, "unclassified": []}


def class_row(c: dict, fix: dict, metas: dict, table: dict, vocab: dict) -> dict:
    rf, meta = fix[HUMANOID], metas[HUMANOID]
    reqs, unmapped = class_requirements(c, table)
    needs = {}
    for kind, what in reqs:
        found = clips_for_attack(what, rf, meta) if kind == "attack" else clips_for_action(what, rf, meta)
        needs[f"{kind}:{what}"] = mark(found, meta, vocab["attackVariantTarget"] if kind == "attack" else 1)
    return {"kind": "class", "id": c["id"], "name": c["name"], "rig": HUMANOID, "plan": rf["plan"],
            "gaitClass": rf["gaitClass"], "model": "humanoid-shared", "needs": needs, "unclassified": unmapped}


def score(row: dict) -> float:
    n = len(row["needs"])
    return sum(1 for v in row["needs"].values() if v["mark"] == OK) / n if n else 0.0


def build() -> dict:
    fix = json.loads(FIXTURE.read_text(encoding="utf-8"))
    metas = {rig: rig_meta(rig) for rig in fix}
    vocab = load("resources/anim/motion_vocabulary.json")
    clf = AttackClassifier()
    table = load("resources/anim/class_actions.json")
    bindings = load("resources/monsters/visuals/bindings.json")
    rows = []
    for p in sorted((ROOT / "resources" / "monsters").glob("*.json")):
        for m in json.loads(p.read_text(encoding="utf-8")):
            rows.append(monster_row(m, bindings.get(m["id"], {}), fix, metas, vocab, clf))
    for p in sorted((ROOT / "resources" / "races").glob("*.json")):
        rows.append(race_row(json.loads(p.read_text(encoding="utf-8")), fix, metas, vocab))
    for p in sorted((ROOT / "resources" / "classes").glob("*.json")):
        rows.append(class_row(json.loads(p.read_text(encoding="utf-8")), fix, metas, table, vocab))
    rows.sort(key=lambda r: ({"monster": 0, "race": 1, "class": 2}[r["kind"]], r["id"]))
    for r in rows:
        r["score"] = round(score(r), 4)
    totals = {"rows": len(rows), "needs": 0, OK: 0, DRAFT: 0, MISSING: 0}
    for r in rows:
        for v in r["needs"].values():
            totals["needs"] += 1; totals[v["mark"]] += 1
    unclassified = sorted({u for r in rows if r["kind"] == "monster" for u in r["unclassified"]})
    unmapped = sorted({u for r in rows if r["kind"] == "class" for u in r["unclassified"]})
    return {"version": 1, "totals": totals, "unclassifiedAttacks": unclassified,
            "unmappedClassFeatures": unmapped, "rows": rows}


# ---------------------------------------------------------------------------------------------
# markdown
# ---------------------------------------------------------------------------------------------
def to_markdown(rep: dict) -> str:
    t = rep["totals"]
    L = ["# Character Checklist", "",
         "> **GENERATED by `tools/anim_pipeline/coverage_report.py` — do not edit by hand.** Regenerate after",
         "> any rig, clip_meta, body-plan, monster, race or class change. Plan: `docs/CharacterAnimationRoadmap.md` (R1).",
         ">",
         "> ✅ shipped/approved clip the engine plays · ⚠️ draft, untyped stand-in, or present but not wired ·",
         "> ❌ missing. `n/t` = reviewed variants / target. Score = share of needs at ✅.", "",
         "## Totals", "",
         f"| Rows | Needs | ✅ | ⚠️ | ❌ |", "|---|---|---|---|---|",
         f"| {t['rows']} | {t['needs']} | {t[OK]} | {t[DRAFT]} | {t[MISSING]} |", ""]
    for kind, title in (("monster", "Monsters"), ("race", "Races"), ("class", "Classes")):
        rows = [r for r in rep["rows"] if r["kind"] == kind]
        L += [f"## {title} ({len(rows)})", "",
              "| Id | Model | Gait | Score | Needs |", "|---|---|---|---|---|"]
        for r in rows:
            cells = []
            for need, v in r["needs"].items():
                label = need.split(":", 1)[-1]
                vt = f" {v['variants']}/{v['target']}" if v["target"] > 1 else ""
                cells.append(f"{v['mark']}{label}{vt}")
            extra = f" · unmapped: {', '.join(r['unclassified'])}" if r["unclassified"] else ""
            L.append(f"| {r['id']} | {r['model']} | {r['gaitClass']} | {round(100 * r['score'])}% | {' '.join(cells)}{extra} |")
        L.append("")
    L += ["## Unclassified attack names", "", ", ".join(rep["unclassifiedAttacks"]) or "none", "",
          "## Unmapped class features", "", ", ".join(rep["unmappedClassFeatures"]) or "none", ""]
    return "\n".join(L)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json-out", type=Path, default=JSON_OUT)
    ap.add_argument("--md-out", type=Path, default=MD_OUT)
    ap.add_argument("--check", action="store_true", help="fail if the committed checklist is stale")
    a = ap.parse_args(argv)
    rep = build()
    md = to_markdown(rep)
    if a.check:
        current = a.md_out.read_text(encoding="utf-8") if a.md_out.exists() else ""
        if current != md:
            print(f"{a.md_out} is stale — run tools/anim_pipeline/coverage_report.py", file=sys.stderr)
            return 1
        return 0
    a.json_out.parent.mkdir(parents=True, exist_ok=True)
    a.json_out.write_text(json.dumps(rep, indent=1, ensure_ascii=False, sort_keys=True), encoding="utf-8")
    a.md_out.write_text(md, encoding="utf-8")
    t = rep["totals"]
    print(f"{t['rows']} rows, {t['needs']} needs: {t[OK]} ✅  {t[DRAFT]} ⚠️  {t[MISSING]} ❌")
    print(f"unclassified attacks: {len(rep['unclassifiedAttacks'])}, unmapped class features: {len(rep['unmappedClassFeatures'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
