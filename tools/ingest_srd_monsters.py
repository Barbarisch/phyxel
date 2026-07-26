#!/usr/bin/env python3
"""Ingest the D&D 5e SRD monster bestiary into the engine's MonsterDefinition format.

Source: dnd5eapi.co (SRD 5.1, CC-BY-4.0) -- ~334 monsters. Maps each API stat block to the
JSON shape MonsterDefinition::fromJson reads (see resources/monsters/beasts.json for the
canonical format), pairs it to an imported visual rig where one exists, skips any id already
defined in the hand-curated files, and writes grouped resources/monsters/srd_<type>.json.

Usage: python tools/ingest_srd_monsters.py [--limit N] [--dry-run]
"""
from __future__ import annotations
import argparse
import json
import re
import sys
import time
import urllib.request
from pathlib import Path

API = "https://www.dnd5eapi.co"
OUT_DIR = Path("resources/monsters")

# Best-effort pairing to imported rigs (resources/animated_characters/*.anim). Keyword in the
# monster name/type -> rig. First match wins; "" (no match) leaves rig unset.
RIG_MAP = [
    ("dragon", "monster_dragon"), ("wolf", "quad_wolf"), ("dire wolf", "quad_wolf"),
    ("goblin", "monster_orc"), ("orc", "monster_orc"), ("hobgoblin", "monster_orc"),
    ("bugbear", "monster_orc"), ("skeleton", "monster_orc_skull"), ("zombie", "monster_orc"),
    ("ogre", "monster_yeti"), ("troll", "monster_yeti"), ("yeti", "monster_yeti"),
    ("demon", "monster_demon"), ("devil", "monster_bluedemon"), ("frog", "monster_frog"),
    ("toad", "monster_frog"), ("spider", "character_spider2"), ("bear", "bear_meshy"),
    ("boar", "boar_meshy"), ("deer", "deer"), ("elk", "elk_meshy"), ("stag", "stag"),
    ("horse", "quad_horse"), ("mastiff", "quad_husky"), ("jackal", "quad_wolf"),
    ("cow", "cow"), ("ox", "bull"), ("bull", "bull"), ("mule", "donkey"),
    ("lizard", "monster_dino"), ("fish", "monster_fish"), ("shark", "monster_fish"),
    ("rabbit", "monster_bunny"), ("hare", "monster_bunny"), ("cactus", "monster_cactoro"),
    ("myconid", "monster_mushroomking"), ("fungus", "monster_mushroomking"),
    ("alien", "monster_alien"), ("ninja", "monster_ninja"), ("tribal", "monster_tribal"),
]

ABILS = [("Strength", "strength"), ("Dexterity", "dexterity"), ("Constitution", "constitution"),
         ("Intelligence", "intelligence"), ("Wisdom", "wisdom"), ("Charisma", "charisma")]
ABBR = {"str": "STR", "dex": "DEX", "con": "CON", "int": "INT", "wis": "WIS", "cha": "CHA"}


def get(path):
    req = urllib.request.Request(API + path, headers={"User-Agent": "phyxel-srd-ingest"})
    return json.load(urllib.request.urlopen(req, timeout=20))


def feet(s):
    """'60 ft.' -> 60; dict/'' -> 0."""
    if not s:
        return 0
    m = re.search(r"(\d+)", str(s))
    return int(m.group(1)) if m else 0


def pick_rig(name, mtype):
    hay = (name + " " + mtype).lower()
    for kw, rig in RIG_MAP:
        if kw in hay:
            return rig
    return ""


def map_attack(a):
    """One API action -> MonsterAttack json, or None if it's neither an attack nor a save."""
    desc = a.get("desc", "")
    dmg = a.get("damage") or []
    dice = ""
    dtype = ""
    if dmg and isinstance(dmg[0], dict) and dmg[0].get("damage_dice"):
        dice = dmg[0]["damage_dice"]
        dtype = (dmg[0].get("damage_type") or {}).get("name", "").lower()
    reach = feet(re.search(r"reach (\d+)", desc).group(1)) if re.search(r"reach (\d+)", desc) else 5
    ranged = "ranged" in desc.lower()
    rng = re.search(r"range (\d+)/(\d+)", desc)
    out = {
        "name": a.get("name", "Attack"),
        "isWeaponAttack": a.get("attack_bonus") is not None,
        "toHitBonus": a.get("attack_bonus", 0) or 0,
        "damageDice": dice,
        "damageType": dtype,
        "reach": reach,
        "isRanged": ranged,
        "rangeNormal": int(rng.group(1)) if rng else 0,
        "rangeLong": int(rng.group(2)) if rng else 0,
        "requiresSave": False,
        "saveAbility": "",
        "saveDC": 0,
        "effectOnFail": "",
        "effectDuration": -1.0,
        "description": desc,
    }
    dc = a.get("dc")
    if dc:
        out["requiresSave"] = True
        out["saveAbility"] = ABBR.get((dc.get("dc_type") or {}).get("index", "")[:3], "")
        out["saveDC"] = dc.get("dc_value", 0) or 0
    # Keep only actions that DO something (deal damage or force a save).
    if not out["damageDice"] and not out["requiresSave"]:
        return None
    return out


def map_monster(m):
    scores = [{"type": t, "base": m.get(k, 10), "racial": 0, "equipment": 0, "temporary": 0}
              for t, k in ABILS]
    ac = m.get("armor_class") or []
    ac0 = ac[0] if ac else {}
    armor_src = ", ".join(x.get("name", "") for x in ac0.get("armor", [])) or ac0.get("type", "")

    saves, skills = [], {}
    for p in m.get("proficiencies", []):
        idx = (p.get("proficiency") or {}).get("index", "")
        if idx.startswith("saving-throw-"):
            saves.append(ABBR.get(idx.split("-")[-1][:3], ""))
        elif idx.startswith("skill-"):
            skname = (p.get("proficiency") or {}).get("name", "").replace("Skill: ", "")
            skills[skname] = 1
    sp = m.get("speed") or {}
    senses = m.get("senses") or {}
    attacks = [x for x in (map_attack(a) for a in m.get("actions", [])) if x]
    traits = [{"name": s.get("name", ""), "description": s.get("desc", "")}
              for s in m.get("special_abilities", [])]
    langs = [s.strip() for s in (m.get("languages") or "").split(",") if s.strip()
             and "understand" not in s.lower()]
    cond_imm = [(c.get("name") if isinstance(c, dict) else c) for c in m.get("condition_immunities", [])]
    tags = [m.get("type", "")] + ([m.get("subtype")] if m.get("subtype") else [])

    out = {
        "id": m["index"], "name": m["name"], "type": m.get("type", ""),
        "subtype": m.get("subtype") or "", "size": m.get("size", "Medium"),
        "alignment": m.get("alignment", "unaligned"),
        "armorClass": ac0.get("value", 10), "armorSource": armor_src,
        "hitPointDice": m.get("hit_points_roll") or m.get("hit_dice", ""),
        "averageHP": m.get("hit_points", 0), "speed": feet(sp.get("walk")),
        "attributes": {"scores": scores},
        "savingThrowProficiencies": [s for s in saves if s],
        "skillProficiencies": skills,
        "damageResistances": m.get("damage_resistances", []),
        "damageImmunities": m.get("damage_immunities", []),
        "conditionImmunities": [c for c in cond_imm if c],
        "darkvisionRange": feet(senses.get("darkvision")),
        "blindsightRange": feet(senses.get("blindsight")),
        "truesightRange": feet(senses.get("truesight")),
        "passivePerception": senses.get("passive_perception", 10),
        "languages": langs,
        "challengeRating": float(m.get("challenge_rating", 0) or 0),
        "xpValue": m.get("xp", 0) or 0,
        "attacks": attacks, "traits": traits, "tags": [t for t in tags if t],
    }
    rig = pick_rig(m["name"], m.get("type", ""))
    if rig:
        out["rig"] = rig            # visual pairing (engine ignores unknown keys)
    return out


def existing_ids():
    ids = set()
    for f in OUT_DIR.glob("*.json"):
        if f.name.startswith("srd_"):
            continue
        try:
            data = json.loads(f.read_text(encoding="utf-8"))
            for e in (data if isinstance(data, list) else [data]):
                if isinstance(e, dict) and "id" in e:
                    ids.add(e["id"])
        except Exception:
            pass
    return ids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    skip = existing_ids()
    print(f"{len(skip)} ids already in curated files (will skip): {sorted(skip)[:8]}...")
    index = get("/api/monsters")["results"]
    if args.limit:
        index = index[: args.limit]
    print(f"fetching {len(index)} SRD monsters...")

    groups, paired, failed = {}, 0, 0
    for i, ref in enumerate(index):
        idx = ref["index"]
        if idx in skip:
            continue
        try:
            mon = map_monster(get("/api/monsters/" + idx))
        except Exception as e:
            print(f"  FAIL {idx}: {e}")
            failed += 1
            continue
        if "rig" in mon:
            paired += 1
        groups.setdefault(mon["type"] or "other", []).append(mon)
        if (i + 1) % 50 == 0:
            print(f"  {i + 1}/{len(index)}")

    total = sum(len(v) for v in groups.values())
    print(f"\nmapped {total} monsters ({paired} paired to a rig), {failed} failed")
    for t, v in sorted(groups.items()):
        print(f"  {t:14s} {len(v)}")
        if not args.dry_run:
            v.sort(key=lambda m: m["challengeRating"])
            (OUT_DIR / f"srd_{t}.json").write_text(
                json.dumps(v, indent=2, ensure_ascii=False), encoding="utf-8")
    if args.dry_run:
        print("(dry-run: no files written)")


if __name__ == "__main__":
    main()
