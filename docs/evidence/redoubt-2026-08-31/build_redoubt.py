"""THE REDOUBT — a second battle, authored with ZERO engine recompiles.

This is the falsifiable test of "a game should extend the engine, not modify
it". It runs on the SAME BattleSim.exe that ran the line battle — same bytes,
hash checked before and after — and everything that makes it a different fight
is data:

  * a fort instead of scattered cover (structures)
  * 24 defenders vs 120 attackers instead of two matched lines (asymmetric)
  * fighters driven by JSON BEHAVIOR TREES built from the game's registered
    action verbs (charge_enemy / keep_distance / cast_at_enemy / flee_below)
    instead of the built-in combat/caster behaviors

That last point is the one that matters. A new KIND of fighter — one that
routs when nearly dead, or holds a wall and shoots — is a .bt.json file, not a
C++ class. BTLoader::parseAction consults BTActionRegistry BEFORE its own
built-ins, which is the seam that makes this possible.

Writes: <deploy>/game.json and <deploy>/behaviors/*.bt.json
"""
import io, json, math, os, sys
from pathlib import Path

DEPLOY = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
BEH = DEPLOY / "behaviors"
BEH.mkdir(parents=True, exist_ok=True)

N_HORDE = int(os.environ.get("REDOUBT_HORDE", "120"))
N_GARRISON = int(os.environ.get("REDOUBT_GARRISON", "24"))

# ── The redoubt: a square stone wall with a gate gap on the south face ─────
# World is 3x2 chunks (96 x 64). Fort centred at (48, 32).
CX, CZ = 48, 32
R = 11                      # half-extent of the wall square
WALL_Y, WALL_H = 16, 4      # wall sits on the flat ground at y=16, 4 tall
GATE_HALF = 3               # gate opening half-width, on the south wall

def fort():
    out = []
    def fill(x0, z0, x1, z1, y0, y1, mat):
        out.append({"type": "fill",
                    "from": {"x": x0, "y": y0, "z": z0},
                    "to":   {"x": x1, "y": y1, "z": z1},
                    "material": mat})
    top = WALL_Y + WALL_H
    # North wall (solid), east + west walls (solid).
    fill(CX - R, CZ + R, CX + R, CZ + R + 1, WALL_Y, top, "StoneBricks")
    fill(CX - R, CZ - R, CX - R + 1, CZ + R, WALL_Y, top, "StoneBricks")
    fill(CX + R, CZ - R, CX + R + 1, CZ + R, WALL_Y, top, "StoneBricks")
    # South wall in TWO pieces, leaving a gate the horde must funnel through.
    fill(CX - R, CZ - R, CX - GATE_HALF, CZ - R + 1, WALL_Y, top, "StoneBricks")
    fill(CX + GATE_HALF, CZ - R, CX + R, CZ - R + 1, WALL_Y, top, "StoneBricks")
    # A raised keep platform in the middle — the last stand, and high ground.
    fill(CX - 4, CZ - 4, CX + 4, CZ + 4, WALL_Y, WALL_Y + 2, "Stone")
    return out

# ── Behaviour trees — the whole point of this exercise ────────────────────
# Selector = first child that succeeds wins, so ORDER is the priority list.

# Wall troops: rout at 20% hp, otherwise shoot anything in range, otherwise
# back off to keep the range they want. They never charge.
WALL_ARCHER = {
    "type": "Selector", "name": "hold_the_wall",
    "children": [
        {"type": "Action", "action": "flee_below", "hp": 0.2, "speed": 1.1},
        {"type": "Action", "action": "cast_at_enemy", "spell": "fire_bolt",
         "cooldown": 2.2, "damage": 7.0, "range": 26.0},
        {"type": "Action", "action": "keep_distance", "range": 15.0, "speed": 0.9},
    ],
}

# Gate guard: holds the gap. Stands its ground far longer (routs at 10%) and
# charges whatever reaches the gate.
GATE_GUARD = {
    "type": "Selector", "name": "hold_the_gate",
    "children": [
        {"type": "Action", "action": "flee_below", "hp": 0.1, "speed": 1.0},
        {"type": "Action", "action": "charge_enemy", "speed": 0.9, "reach": 2.2},
    ],
}

# Horde: a mob. Routs late (8%) and otherwise just comes on. The contrast with
# the garrison's trees is what should be VISIBLE — one side breaks and runs
# while the other is still shooting.
HORDE_SWARM = {
    "type": "Selector", "name": "swarm",
    "children": [
        {"type": "Action", "action": "flee_below", "hp": 0.08, "speed": 1.2},
        {"type": "Action", "action": "charge_enemy", "speed": 1.15, "reach": 2.0},
    ],
}

TREES = {"wall_archer": WALL_ARCHER, "gate_guard": GATE_GUARD, "horde_swarm": HORDE_SWARM}
for name, tree in TREES.items():
    (BEH / f"{name}.bt.json").write_text(json.dumps(tree, indent=2), encoding="utf-8")

LIVERY = {
    "gold":  {"torsoColor": [0.85, 0.68, 0.18, 1], "armColor": [0.62, 0.48, 0.12, 1],
              "legColor": [0.35, 0.27, 0.08, 1]},
    "green": {"torsoColor": [0.16, 0.42, 0.18, 1], "armColor": [0.12, 0.31, 0.13, 1],
              "legColor": [0.08, 0.20, 0.09, 1]},
}

def look(faction):
    a = dict(LIVERY[faction])
    a["skinColor"] = [0.95, 0.78, 0.62, 1]
    a["morphology"] = "humanoid"
    return a

npcs = []

# GARRISON — spaced along the wall walk (inside the wall line), plus gate guards.
for i in range(N_GARRISON):
    onGate = (i % 6 == 0)
    if onGate:
        # Just inside the gate gap.
        x = CX - GATE_HALF + 1 + (i // 6) * 2
        z = CZ - R + 3
        tree, role = "gate_guard", "gate"
    else:
        # Distribute the rest around the inside of the other three walls.
        k = i - (i // 6) - 1
        side = k % 3
        t = (k // 3) / max(1, (N_GARRISON // 3))
        inset = R - 2
        if side == 0:      x, z = CX - inset + int(t * 2 * inset), CZ + inset   # north
        elif side == 1:    x, z = CX - inset, CZ - inset + int(t * 2 * inset)   # west
        else:              x, z = CX + inset, CZ - inset + int(t * 2 * inset)   # east
        tree, role = "wall_archer", "wall"
    npcs.append({
        "name": f"gold_{role}_{i:03d}",
        "position": {"x": float(x), "y": 22.0, "z": float(z)},
        "appearance": look("gold"),
        "behavior": "behavior_tree",
        "behaviorTree": f"behaviors/{tree}.bt.json",
        "faction": "gold",
        "maxHealth": 70,
        "intelligence": 13,
    })

# HORDE — a ring outside the fort, closing from every side. A ring, not a
# line: the built-in battle was two facing blocks, this one arrives from 360
# degrees and has to find the gate or die on the wall.
for i in range(N_HORDE):
    ang = (2.0 * math.pi * i) / N_HORDE
    rad = R + 8 + (i % 4) * 3          # a few ranks deep
    x = CX + math.cos(ang) * rad
    z = CZ + math.sin(ang) * rad
    npcs.append({
        "name": f"green_swarm_{i:03d}",
        "position": {"x": round(x, 1), "y": 22.0, "z": round(z, 1)},
        "appearance": look("green"),
        "behavior": "behavior_tree",
        "behaviorTree": "behaviors/horde_swarm.bt.json",
        "faction": "green",
        "maxHealth": 45,
        "intelligence": 7,
    })

scene = {
    "id": "redoubt",
    "name": "The Redoubt",
    "sceneType": "world",
    "transitionStyle": "fade",
    "worldDatabase": "worlds/redoubt.db",
    "definition": {
        "world": {"type": "Flat", "seed": 3,
                  "from": {"x": 0, "y": 0, "z": 0}, "to": {"x": 2, "y": 0, "z": 1}},
        "structures": fort(),
        "player": {"type": "animated", "faction": "neutral",
                   "position": {"x": 84.0, "y": 18.0, "z": 32.0},
                   "_comment": "neutral spectator, east of the fort, inside the world"},
        "camera": {"position": {"x": 90, "y": 40, "z": 32},
                   "yaw": 180, "pitch": -30, "mode": "third_person"},
        "npcs": npcs,
        "triggers": [],
    },
}

game = {
    "name": "BattleSim",
    "description": "The Redoubt: an asymmetric siege authored entirely in data — JSON behavior "
                   "trees over game-registered action verbs, on an unmodified engine binary.",
    "version": "0.1",
    "scenes": [scene],
    "combat": {"mode": "real_time", "decision_log": False},
    "playerDefaults": {"type": "animated", "maxHealth": 100},
}

# ── Sanity: same class of geometry assert the line battle needed ──────────
w = scene["definition"]["world"]
wx = (w["from"]["x"] * 32, (w["to"]["x"] + 1) * 32)
wz = (w["from"]["z"] * 32, (w["to"]["z"] + 1) * 32)
obs = scene["definition"]["player"]["position"]
assert wx[0] <= obs["x"] <= wx[1] and wz[0] <= obs["z"] <= wz[1], "observer outside the world"
for n in npcs:
    p = n["position"]
    assert wx[0] <= p["x"] <= wx[1] and wz[0] <= p["z"] <= wz[1], \
        f"{n['name']} at ({p['x']},{p['z']}) outside world x{wx} z{wz}"
# Every referenced tree must exist on disk, or the NPC silently gets no brain.
for n in npcs:
    bt = DEPLOY / n["behaviorTree"]
    assert bt.exists(), f"{n['name']} references missing tree {bt}"

(DEPLOY / "game.json").write_text(json.dumps(game, indent=2), encoding="utf-8")
g = sum(1 for n in npcs if n["faction"] == "gold")
h = sum(1 for n in npcs if n["faction"] == "green")
print(f"Redoubt written: {g} garrison vs {h} horde ({len(npcs)} total), "
      f"{len(TREES)} behavior trees, all JSON")
print(f"  trees: {', '.join(sorted(TREES))}")
