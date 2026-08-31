"""Generate BattleSim: a 20-vs-20 REAL-TIME battle (no turns, no initiative).

Two armies face off across an open field and fight until one side is gone:

  CRIMSON (west, x~14)          AZURE (east, x~46)
    12 melee  (sword/axe)         12 melee
     8 casters (fire_bolt etc)     8 casters

Melee use CombatBehavior (approach / strafe / swing / evade); casters use the
new RangedCasterBehavior (hold range, cast on cooldown, back off when closed
on). Both are faction-aware through Entity::hostileTo, so the lines hold
instead of devolving into a free-for-all.

The player spawns to the side as an observer — this is a simulation to watch,
so the camera is the point.
"""
import io, json, sys

import os
# Scale is env-tunable so the same generator can bisect a load failure:
#   BATTLE_MELEE / BATTLE_CASTERS per side (default 120/80 = 200 v 200).
N_MELEE   = int(os.environ.get("BATTLE_MELEE", "120"))
N_CASTERS = int(os.environ.get("BATTLE_CASTERS", "80"))
# Line separation matters more than it looks. First pass had the armies 28u
# apart with aggro 40 — everyone (including the unaligned observer 40u off the
# flank) was in range of everyone. Second pass cut aggro to 26 while the lines
# stayed 28 apart, so NOBODY could see anyone and the battle never started at
# all. The gap and the aggro radius must be chosen together:
#     melee lines 16u apart  <  melee aggro 26  <<  ~58u to the observer
WEST_X, EAST_X = 20.0, 40.0
Z0, Z_STEP = 8.0, 2.4               # ranks spread along z

# ── LIVERY ────────────────────────────────────────────────────────────────
# Sides must be readable at a glance. Default NPC appearance is randomised per
# name, so a 400-body melee was a confetti of unrelated colours and you could
# not tell who was fighting whom. Each army now wears its colours: torso/arms/
# legs in faction hues, with casters in a lighter shade of the same family so
# the back rank is distinguishable from the line without breaking team read.
LIVERY = {
    "crimson": {
        "melee":  {"torsoColor": [0.62, 0.09, 0.09, 1], "armColor": [0.45, 0.07, 0.07, 1],
                   "legColor":   [0.30, 0.05, 0.05, 1]},
        "caster": {"torsoColor": [0.90, 0.35, 0.30, 1], "armColor": [0.72, 0.22, 0.18, 1],
                   "legColor":   [0.40, 0.10, 0.10, 1]},
    },
    "azure": {
        "melee":  {"torsoColor": [0.10, 0.22, 0.65, 1], "armColor": [0.08, 0.18, 0.48, 1],
                   "legColor":   [0.05, 0.10, 0.32, 1]},
        "caster": {"torsoColor": [0.35, 0.60, 0.95, 1], "armColor": [0.22, 0.42, 0.78, 1],
                   "legColor":   [0.10, 0.18, 0.45, 1]},
    },
}

USE_LIVERY = os.environ.get("BATTLE_LIVERY", "1") != "0"

# BATTLE_TACTICS=0 builds the CONTROL: no squads, no officers, and every
# soldier at INT 3. It must produce a battle where nobody ever takes cover,
# flanks or falls back — 100% "engage" on /api/rpg/tactics. If the control
# ALSO shows tactical intents, the measurement is meaningless and any green
# reading from the real config proves nothing.
USE_TACTICS = os.environ.get("BATTLE_TACTICS", "1") != "0"

def livery(faction, role):
    a = dict(LIVERY[faction][role])
    a["skinColor"] = [0.95, 0.78, 0.62, 1]   # uniform skin: colour = team, not person
    # An explicit "appearance" REPLACES the seeded one in the resolver, which
    # skips the morphology that would otherwise be derived from the rig. State
    # it here so the mesh rebuild matches the humanoid skeleton.
    a["morphology"] = "humanoid"
    return a

# Ranks: 120 melee per side no longer fit in one file of 2.4u spacing (that
# would be a 288u line in a 96u world), so each army forms a BLOCK — rows of
# 20 along z, successive ranks stepping back in x.
ROW = 20
def formation(x0, facing_sign, index, rank_step, row_step=2.2):
    rank = index // ROW
    file_ = index % ROW
    return {"x": x0 + facing_sign * (-rank * rank_step),
            "y": 18,
            "z": Z0 + file_ * row_step}

# ── COVER ─────────────────────────────────────────────────────────────────
# A flat field gives tactical AI nothing to reason about: "take cover" has no
# meaning without something to stand behind. These blocks sit between and
# around the lines so line-of-sight can actually be broken.
def battlefield_cover():
    out = []
    def block(x0, z0, w, d, h, mat, y0=16):
        out.append({"type": "fill",
                    "from": {"x": x0, "y": y0, "z": z0},
                    "to":   {"x": x0 + w, "y": y0 + h, "z": z0 + d},
                    "material": mat})
    # A low ruined wall down the centre, with gaps to fight through.
    for i, z in enumerate(range(10, 50, 9)):
        block(29, z, 2, 5, 2, "StoneBricks")
    # Rock outcrops scattered across the field (asymmetric on purpose).
    for (x, z, w, d, h) in [(20, 14, 3, 3, 3), (38, 20, 3, 4, 3),
                            (24, 34, 4, 3, 2), (42, 40, 3, 3, 3),
                            (16, 26, 3, 3, 2), (46, 12, 3, 3, 2),
                            (33, 44, 4, 3, 3), (12, 38, 3, 4, 3)]:
        block(x, z, w, d, h, "Stone")
    # Two hillocks: high ground worth holding. Each ring must sit one course
    # HIGHER than the one outside it — the first version stamped every ring at
    # y=16 and produced two flat slabs, which is a mound in the data and a
    # patio on screen.
    for (x, z, r) in [(26, 22, 5), (40, 30, 5)]:
        for i, step in enumerate(range(r, 0, -1)):
            block(x - step, z - step, step * 2, step * 2, 1, "Dirt", y0=16 + i)
    return out

def army(faction, x, facing_sign, melee_weapon, spells):
    npcs = []
    # Melee front ranks
    for i in range(N_MELEE):
        npcs.append({
            "name": f"{faction}_M{i:03d}",
            "position": formation(x + facing_sign * 2.0, facing_sign, i, 1.8),
            **({"appearance": livery(faction, "melee")} if USE_LIVERY else {}),
            "behavior": "combat",
            "faction": faction,
            "maxHealth": 60,
            "weapon": melee_weapon,
            # SQUADS of 20 (one per rank), first man in each is the officer.
            **({"squad": f"{faction}_S{i // ROW}"} if USE_TACTICS else {}),
            **({"rank": "officer"} if USE_TACTICS and (i % ROW) == 0 else {}),
            # INTELLIGENCE: officers are sharp (14), the ranks are ordinary
            # (9) — and crimson's line is drilled two points better than
            # azure's, so the difference should be visible in how they fight
            # rather than in any stat sheet.
            "intelligence": ((14 if (i % ROW) == 0
                              else (11 if faction == "crimson" else 8))
                             if USE_TACTICS else 3),
            # Deep blocks: the REAR ranks start ~11u further back than the
            # front, so aggro must cover the line gap PLUS the formation depth
            # or the back half stands idle while the front dies.
            "aggro_range": 35.0,
            "attack_damage": 7.0,
            "attack_cooldown": 1.6,
        })
    # Caster ranks, formed up BEHIND the melee block
    melee_depth = ((N_MELEE + ROW - 1) // ROW) * 1.8
    for i in range(N_CASTERS):
        npcs.append({
            "name": f"{faction}_C{i:03d}",
            "position": formation(x + facing_sign * (2.0 - melee_depth - 2.0),
                                  facing_sign, i, 1.8),
            **({"appearance": livery(faction, "caster")} if USE_LIVERY else {}),
            "behavior": "caster",
            "faction": faction,
            "maxHealth": 40,
            "spells": spells,
            # Casters form up BEHIND the melee blocks, so the two caster
            # bodies are ~42u apart — aggro has to clear that or the entire
            # back half of both armies never fires a shot. (Reaching the
            # observer is harmless now that it is faction "neutral".)
            "aggro_range": 60.0,
            "preferred_range": 14.0,
            "cast_cooldown": 3.5,
            "spell_damage": 5.0,
        })
    return npcs

scene = {
    "id": "battle",
    "name": "Battle Simulation",
    "sceneType": "world",
    "transitionStyle": "fade",
    "worldDatabase": "worlds/battle.db",
    "definition": {
        "world": {"type": "Flat", "seed": 7,
                  "from": {"x": 0, "y": 0, "z": 0}, "to": {"x": 2, "y": 0, "z": 1}},
        # Ruins, outcrops and two hillocks — without something to stand behind,
        # "take cover" is a behaviour with nowhere to go.
        "structures": battlefield_cover(),
        # The observer is "neutral" — attacked by nobody, attacks nobody — so it
        # can stand right by the fighting. It used to be UNALIGNED (hostile to
        # everyone), which forced it 45u off the flank... at z=78, OUTSIDE the
        # generated world (z 0-63). The camera therefore filmed the void: a
        # black frame with one floating character while the battle raged
        # off-screen. Keep the spectator INSIDE the world.
        # 200-strong blocks are 44u wide and ~11u deep, filling x 2-59, z 8-52.
        # The spectator stands EAST of it all, clear of the formations.
        "player": {"type": "animated", "faction": "neutral",
                   "position": {"x": 78, "y": 18, "z": 30},
                   "_comment": "neutral spectator, east of both armies, inside the world"},
        "camera": {"position": {"x": 86, "y": 30, "z": 30},
                   "yaw": 180, "pitch": -25, "mode": "third_person"},
        "npcs": army("crimson", WEST_X, +1.0, "longsword",
                     ["fire_bolt", "magic_missile"]) +
                army("azure",   EAST_X, -1.0, "battleaxe",
                     ["ray_of_frost", "magic_missile"]),
        "triggers": [],
    },
}

game = {
    "name": "BattleSim",
    "description": "20 vs 20 real-time battle simulation: mixed melee and casters, two "
                   "factions, no turns. A scale + behavior stress test for the combat stack.",
    "version": "0.1",
    "scenes": [scene],
    "combat": {"mode": "real_time", "decision_log": False,
               "_comment": "real-time: CombatBehavior/RangedCasterBehavior drive every frame; "
                           "no CombatDirector turns. Decision log off — 40 NPCs would flood it."},
    "playerDefaults": {"type": "animated", "maxHealth": 100},
}

# ── Geometry sanity: the two failure modes this file has already hit ──
# 1) aggro < line gap  -> nobody ever sees anyone, the battle never starts
# 2) aggro > observer distance -> both armies stop to kill the spectator
import math
npcs = scene["definition"]["npcs"]
obs = scene["definition"]["player"]["position"]
def gap(kind):
    a = [n for n in npcs if n["faction"] == "crimson" and kind in n["name"]]
    b = [n for n in npcs if n["faction"] == "azure"   and kind in n["name"]]
    return abs(a[0]["position"]["x"] - b[0]["position"]["x"])
for kind, label in (("_M", "melee"), ("_C", "caster")):
    same = [n for n in npcs if kind in n["name"]]
    aggro = same[0]["aggro_range"]
    g = gap(kind)
    nearest_obs = min(math.dist((n["position"]["x"], n["position"]["z"]),
                                (obs["x"], obs["z"])) for n in same)
    assert aggro > g + 4, (
        f"{label} aggro {aggro} does not clear the {g:.0f}u line gap — "
        "the armies would never engage")
    # The observer is faction "neutral" now, so aggro reaching it is harmless —
    # but it MUST be inside the generated world or the camera films the void.
    print(f"  {label}: gap {g:.0f}u < aggro {aggro:.0f} (observer {nearest_obs:.0f}u, neutral)  OK")

w = scene["definition"]["world"]
wx = (w["from"]["x"] * 32, (w["to"]["x"] + 1) * 32)
wz = (w["from"]["z"] * 32, (w["to"]["z"] + 1) * 32)
assert wx[0] <= obs["x"] <= wx[1] and wz[0] <= obs["z"] <= wz[1], (
    f"observer ({obs['x']},{obs['z']}) is OUTSIDE the generated world "
    f"x{wx} z{wz} — the camera would film empty space")
for n in npcs:
    p = n["position"]
    assert wx[0] <= p["x"] <= wx[1] and wz[0] <= p["z"] <= wz[1], (
        f"{n['name']} at ({p['x']},{p['z']}) is outside the world x{wx} z{wz}")
print(f"  world x{wx} z{wz}: observer and all {len(npcs)} combatants inside  OK")

path = sys.argv[1]
io.open(path, "w", encoding="utf-8").write(json.dumps(game, indent=2))
n = len(npcs)
print(f"BattleSim written: {n} combatants "
      f"({N_MELEE} melee + {N_CASTERS} casters per side)")
