# Recipe: save-integrity (deep persistence round-trip)

**Satisfies milestone:** `save_load` (elevated to L4 save-integrity for survival/rpg). **Genre:** any
systemic game. This is the check these genres break on most — a save that silently drops a subsystem.

## The bar
`save -> reload -> deep-diff every persisted subsystem == identical`. Not "a save file exists" — a
full round-trip where nothing is lost.

## 1. Enumerate the persistence surface (GAMEPLAN.md)
List **everything** that must survive a save/load for this game. For survival/RPG that's typically:
world/chunk edits, player character sheet (abilities/level/XP), inventory + equipment, quest progress
+ story variables, NPC states/relationships/reputation, base builds/placed objects, world
time/calendar, spawned entities, and active conditions/effects. A subsystem you forget to list is a
subsystem that silently won't persist.

## 2. Build the round-trip test
- Set up a rich state: play a bit, accept a quest, craft/equip items, change reputation, build
  something, advance time.
- Snapshot the live state (via the relevant MCP reads: `get_player_state`, `get_inventory`,
  `get_objectives`, `story_get_state`, `list_placed_objects`, `get_world_date`, …).
- `save_world` (+ save player / scene manifest as applicable).
- Restart / reload the project.
- Read the same state back and **deep-diff** against the snapshot.

## 3. Watch the known trap
A DB-load path that doesn't rebuild derived state is the classic failure (the engine's
"every DB-load must call `buildAllChunkPhysics()`" rule is exactly this class of bug). If reload
drops collision/physics/occupancy or an entity, this test catches it.

## 4. Validate + record
- L3: one round-trip subsystem-by-subsystem identical.
- L4: round-trip after a *full* play session (the real state surface, not a toy state).
- Record `save_load -> validated:L4` with the diff evidence. Re-run it in the regression sweep after
  any change that touches persistence (it's a durability-sensitive milestone).
