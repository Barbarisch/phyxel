# UUID Phase 2b — L4 runtime evidence (runtime-entity persistence + auto-id reseed)

Raw captured outputs from the live engine (`phyxel.exe --project StructGenTest`, port 8090),
archived per the "never cite un-archived numbers" discipline. Exercises the actual bug the Phase 2b
auditor found (auto-id collision after restore) plus the persistence round-trip.

## Sequence

1. Cleared `runtime_entities` in the project DB for a clean slate.
2. `spawn_entity {type:animated, x:25,y:40,z:25}` (NO explicit id — the bug scenario):

   ```json
   { "id": "entity_1", "type": "animated",
     "uuid": "ff9d8ed1-bb5d-450b-a36c-43c15558ed04",
     "position": { "x": 25, "y": 40, "z": 25 } }
   ```

3. `save_world {all:true}` → `{"mode":"all","success":true}`. DB row written (position settled to y=22):

   ```
   ('ff9d8ed1-bb5d-450b-a36c-43c15558ed04', 'entity_1', 'animated', 25.0, 22.0, 25.0)
   ```

4. Full engine relaunch. Boot log:

   ```
   [RuntimeEntityStore] Loaded 1 runtime entities
   ```

5. `spawn_entity {type:animated, x:27,y:40,z:27}` (NO explicit id) → got **entity_2**, NOT a collision
   with the restored `entity_1`:

   ```json
   { "id": "entity_2", "type": "animated",
     "uuid": "ffa525c7-632f-4560-a3a6-76f46e8316dd",
     "position": { "x": 27, "y": 40, "z": 27 } }
   ```

6. `list_entities` → three distinct entities, three distinct uuids:

   ```json
   [ { "id": "entity_1", "uuid": "ff9d8ed1-bb5d-450b-a36c-43c15558ed04", "position": {"x":25,"y":22,"z":25} },
     { "id": "player",   "uuid": "494044db-7186-49e8-99ba-f09c7afb2cad", "position": {"x":20,"y":17,"z":20} },
     { "id": "entity_2", "uuid": "ffa525c7-632f-4560-a3a6-76f46e8316dd", "position": {"x":27,"y":20,"z":27} } ]
   ```

## What this proves

- **Persistence:** `entity_1` survived save + full relaunch with the SAME uuid at its saved position
  (the `RuntimeEntityStore` round-trip through the world DB).
- **Auto-id reseed fix:** the post-relaunch auto-id spawn got `entity_2` (not a regenerated `entity_1`),
  so `m_nextAutoId` was correctly advanced past the restored `entity_1`. Without the fix this collides,
  silently fails to register the new entity, and misattributes `entity_1`'s uuid to it (the auditor's
  finding). Unit test `EntityRegistryUuidTest.AutoIdReseededPastRestoredEntities` encodes this and was
  shown RED with the reseed disabled, GREEN with it.
- **Player not hijacked:** `player` retained its own separate uuid across the restore (the
  save/restore of `animatedCharacter` + `currentControlTarget` around the respawn loop).

## Prior run (explicit-id, separate proof)

An earlier run with an explicit id `persist_probe` (uuid `42d992b9-c0f9-4b08-b6bf-47368ebbb197`)
likewise round-tripped: spawn → save_world → DB row present → relaunch → `list_entities` showed
`persist_probe` with the same uuid alongside a separate `player`.

## Known scope limits (tracked)

- Persistence restores on a process relaunch with `--project` (the `initialize()` path), NOT on an
  in-session launcher project switch — mirrors the existing `PlacedObjectManager` limitation.
- The 1-arg `spawn_entity` (no explicit type) registers with an empty typeTag (pre-existing behavior);
  restored entities carry their persisted `type`.
