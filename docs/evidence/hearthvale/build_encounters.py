"""Rewrite Hearthvale's cellar into three escalating encounters.

Layout: the player enters the cellar around (10, 26) and works toward the
remedy at low z. Each encounter is a region trigger across the approach:

  E1  z 23-25  THE PACK       2x Rat        brutes, focus-fire (gang up as one)
  E2  z 16-18  THE WARDEN     Warden        caster, targets CASTERS first
                              Archer        kites to 25 ft, targets WEAKEST
                              Acolyte       healer, heals any ally under 60%
  E3  z  9-11  THE HORROR     Horror        heavy brute, targets WEAKEST
                              2x Cultist    FLEE below 35% hp (morale)

The remedy sits behind all three (z 4-7), so the fights are on the path.
"""
import io, json, sys

PATH = sys.argv[1]
j = json.load(io.open(PATH, encoding="utf-8-sig"))
cellar = [s for s in j["scenes"] if s["id"] == "cellar"][0]
d = cellar["definition"]

def npc(name, x, z, hp, comment):
    return {"name": name, "position": {"x": x, "y": 18, "z": z},
            "behavior": "idle", "maxHealth": hp, "_comment": comment}

d["npcs"] = [
    # E1 — the pack: weak but coordinated
    npc("Rat",      13, 22,  8, "E1 pack: focus-fires with Rat2 so two bites land on one target"),
    npc("Rat2",     15, 22,  8, "E1 pack: the other half of the focus-fire pair"),
    # E2 — the warden's post: artillery + skirmisher + support. These carry
    # enough HP to SURVIVE being wounded — with 12-14 HP the party one-shot
    # them and the Acolyte never had anyone to heal (measured).
    npc("Warden",   16, 16, 30, "E2 artillery: wizard, opens with magic_missile, targets CASTERS first"),
    npc("Archer",   12, 16, 26, "E2 skirmisher: kites to 25 ft and shoots the WEAKEST target"),
    npc("Acolyte",  14, 14, 24, "E2 support: heals any ally that drops under 70% hp"),
    # E3 — the horror: a heavy plus breakable minions
    npc("Horror",   13,  9, 34, "E3 boss: heavy brute, hunts the WEAKEST of the party"),
    npc("Cultist",  11,  9, 14, "E3 minion: FLEES below 35% hp - morale, not a fight to the death"),
    npc("Cultist2", 15,  9, 14, "E3 minion: same broken morale"),
]

def region(x0, z0, x1, z1):
    return {"from": {"x": x0, "y": 16, "z": z0}, "to": {"x": x1, "y": 22, "z": z1}}

def encounter(tid, x0, z0, x1, z1, foes, bonus):
    parts = [{"entity_id": "player", "player_side": True, "initiative_bonus": 3}]
    for f, b in zip(foes, bonus):
        parts.append({"entity_id": f, "initiative_bonus": b})
    return {"id": tid,
            "when": {"event": "entity_reached_region", "entity": "player",
                     "region": region(x0, z0, x1, z1)},
            "then": [{"type": "start_combat", "participants": parts}],
            "once": True}

# Region bands are 5 deep, not 2: a 2-deep band is narrower than the probe's
# arrival tolerance, so "arrived at z=17" could land at 18.6 — OUTSIDE the band
# — and the encounter then fired mid-walk toward the NEXT one (measured: the
# warden fight was mislabeled as the horror fight). Bands stay disjoint.
d["triggers"] = [
    encounter("encounter_pack",   8, 20, 20, 25,
              ["npc_Rat", "npc_Rat2"], [1, 1]),
    encounter("encounter_warden", 8, 13, 20, 18,
              ["npc_Warden", "npc_Archer", "npc_Acolyte"], [2, 4, 1]),
    encounter("encounter_horror", 8,  6, 20, 11,
              ["npc_Horror", "npc_Cultist", "npc_Cultist2"], [0, 1, 1]),
    {"id": "horror_slain",
     "when": {"event": "entity_died", "id": "npc_Horror"},
     "then": [{"type": "set_story_variable", "name": "cellar_cleared", "value": True},
              {"type": "save_game"}],
     "once": True},
    {"id": "find_remedy",
     "when": {"event": "entity_reached_region", "entity": "player",
              "region": region(12, 1, 18, 4)},
     "then": [{"type": "complete_objective", "id": "find_remedy"},
              {"type": "set_story_variable", "name": "remedy_found", "value": True},
              {"type": "give_item", "id": "moonpetal_remedy", "count": 1},
              {"type": "long_rest", "_comment": "the shrine alcove: slots back for the walk home"},
              {"type": "save_game"}],
     "once": True},
    {"id": "back_to_town",
     "when": {"event": "entity_reached_region", "entity": "player",
              "region": region(4, 24, 8, 28)},
     "then": [{"type": "transition_scene", "target": "town"}],
     "once": True},
]

# NPC spellcasters (Warden = artillery, Acolyte = healer)
j["casters"] = {
    "npc_Warden":  {"class": "wizard", "level": 1,
                    "spells": ["fire_bolt", "magic_missile"],
                    "_comment": "artillery: 2 slots of magic_missile, then fire_bolt forever"},
    "npc_Acolyte": {"class": "cleric", "level": 1,
                    "spells": ["sacred_flame", "cure_wounds"],
                    "_comment": "support: cure_wounds while slots last, sacred_flame otherwise"},
}

# Tactical profiles — one archetype per behavior so each is observable alone
j["combat_ai"] = {
    "npc_Rat":      {"target": "focus",   "_comment": "pack: piles onto the side's focus"},
    "npc_Rat2":     {"target": "focus",   "_comment": "pack: same"},
    "npc_Warden":   {"target": "casters", "preferred_range": 20,
                     "_comment": "artillery: kills enemy casters first, keeps its distance"},
    "npc_Archer":   {"target": "weakest", "preferred_range": 25,
                     "_comment": "skirmisher: kites to 25 ft, finishes the wounded"},
    "npc_Acolyte":  {"heal_ally_below": 0.7, "preferred_range": 15,
                     "_comment": "support: heals the hurt, stays out of reach"},
    "npc_Horror":   {"target": "weakest",
                     "_comment": "boss: hunts whoever is closest to dropping"},
    "npc_Cultist":  {"flee_below_hp": 0.35, "_comment": "minion: breaks and runs"},
    "npc_Cultist2": {"flee_below_hp": 0.35, "_comment": "minion: same"},
}

io.open(PATH, "w", encoding="utf-8").write(json.dumps(j, indent=2))
print("cellar rebuilt:", len(d["npcs"]), "npcs,", len(d["triggers"]), "triggers,",
      len(j["casters"]), "casters,", len(j["combat_ai"]), "tactical profiles")
