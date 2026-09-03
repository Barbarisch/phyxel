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

N_MELEE, N_CASTERS = 12, 8          # per side -> 20 v 20, 40 characters
# Line separation matters more than it looks. First pass had the armies 28u
# apart with aggro 40 — everyone (including the unaligned observer 40u off the
# flank) was in range of everyone. Second pass cut aggro to 26 while the lines
# stayed 28 apart, so NOBODY could see anyone and the battle never started at
# all. The gap and the aggro radius must be chosen together:
#     melee lines 16u apart  <  melee aggro 26  <<  ~58u to the observer
WEST_X, EAST_X = 20.0, 40.0
Z0, Z_STEP = 8.0, 2.4               # ranks spread along z

def army(faction, x, facing_sign, melee_weapon, spells):
    npcs = []
    # Melee front rank
    for i in range(N_MELEE):
        npcs.append({
            "name": f"{faction}_M{i:02d}",
            "position": {"x": x + facing_sign * 2.0, "y": 18, "z": Z0 + i * Z_STEP},
            "behavior": "combat",
            "faction": faction,
            "maxHealth": 60,
            "weapon": melee_weapon,
            # Comfortably over the 16u line gap, far under the ~58u to the
            # observer (unaligned = hostile to all, so it must stay out of range).
            "aggro_range": 26.0,
            "attack_damage": 7.0,
            "attack_cooldown": 1.6,
        })
    # Caster back rank, set behind the line
    for i in range(N_CASTERS):
        npcs.append({
            "name": f"{faction}_C{i:02d}",
            # 3u behind the line (not 5): with 5 the caster gap was 30u and
            # aggro 34 barely cleared it, so casters dithered at the edge of
            # acquisition. The generator now asserts this margin.
            "position": {"x": x - facing_sign * 3.0, "y": 18,
                         "z": Z0 + 1.2 + i * (Z_STEP * 1.5)},
            "behavior": "caster",
            "faction": faction,
            "maxHealth": 40,
            "spells": spells,
            # First run: 8 casters/side at 9 damage every 2.2s wiped BOTH
            # armies (and the observer) before the melee lines ever met —
            # artillery, not a battle. Softer and slower so the lines close
            # and melee decides it.
            # Casters sit 5u behind their own line, so they are ~30u apart:
            # aggro must clear that, and still fall well short of the observer.
            "aggro_range": 34.0,
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
        # The observer is UNALIGNED, and unaligned means hostile to everyone —
        # so it must sit outside every aggro radius or both armies stop the war
        # to kill the spectator (measured: they did, and won).
        "player": {"type": "animated",
                   "position": {"x": 30, "y": 18, "z": 78},
                   "_comment": "observer: 45+ units off the flank, beyond every aggro radius"},
        "camera": {"position": {"x": 30, "y": 46, "z": 74},
                   "yaw": -90, "pitch": -42, "mode": "third_person"},
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
    assert aggro < nearest_obs - 8, (
        f"{label} aggro {aggro} reaches the observer at {nearest_obs:.0f}u — "
        "both armies would attack the spectator")
    print(f"  {label}: gap {g:.0f}u < aggro {aggro:.0f} < observer {nearest_obs:.0f}u  OK")

path = sys.argv[1]
io.open(path, "w", encoding="utf-8").write(json.dumps(game, indent=2))
n = len(npcs)
print(f"BattleSim written: {n} combatants "
      f"({N_MELEE} melee + {N_CASTERS} casters per side)")
