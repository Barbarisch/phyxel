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

---

# UUID Phase 3 — L4 runtime evidence (item-instance identity)

`spawn_item` of a non-stackable item mints + returns an instance uuid and persists it on the prop:

```
POST /api/items/spawn {"item":"iron_sword","x":22,"y":40,"z":22}
-> { "success":true, "item":"iron_sword", "prop_id":"item_iron_sword_1",
     "instance_uuid":"fb7c2480-c588-469f-a556-845c90d5290b" }

get_placed_object("item_iron_sword_1"):
  category:"item",
  metadata: { "itemId":"iron_sword", "displayName":"Iron Sword",
              "instanceUuid":"fb7c2480-c588-469f-a556-845c90d5290b" },
  uuid: "660bbbd0-abde-4f7e-bbee-3fc2e041ad90"   (Phase-1 PlacedObject uuid, distinct)
```

Proves: mint on spawn, uuid returned, and persisted in the prop's placed-object metadata (which rides
the Phase-1 placed_objects blob, so it survives reload; ItemPropManager::rebuildFromPlacedObjects
restores it into the Prop). The drop→pickup and equip→unequip carry-through are unit-tested
(InventoryUuidTest, EquipmentUuidTest, 53 green with the existing suites). (give_item returned
overflow here only because StructGenTest's player inventory was pre-filled — the unique branch
correctly reported it, not a code fault.)

## Phase 3 — full drop→pickup round-trip (survival) + creative copy uniqueness

Survival (creative OFF) — a true MOVE preserves the instance uuid, no duplication:
```
give_item iron_sword           -> instance_uuids: ["890c8a74-ff95-4de1-8080-1db955a103b5"]
get_inventory                  -> slot 0 iron_sword uuid 890c8a74...   (give mint == stored)
select_hotbar_slot 0; drop_item
get_inventory (after drop)     -> iron_sword slots: []                 (consumed — true move)
POST /api/interact  (pickup)   -> {"success":true,"triggered":true}
get_inventory (after pickup)   -> slot 0 iron_sword uuid 890c8a74...   (SAME uuid, ONE item)
```

Creative (ON) — a drop is a COPY and gets its OWN fresh uuid (no two items share identity):
```
inventory sword uuid           890c8a74-...
drop_item (creative, no consume)
inventory still has            890c8a74-...   (original kept)
dropped prop item_iron_sword_2 instanceUuid = 946006cd-...  (DISTINCT fresh uuid)
```
The creative case was a bug found by running this round-trip: reusing the same uuid on a
non-consuming (creative) drop put two items under one identity. Fixed in dropHeldItem — a creative
copy mints a fresh uuid; a real consuming drop moves the same uuid.

## Phase 3 — equip→unequip handler carries the instance uuid

```
POST /api/entity/npc_equip_probe/equip {"itemId":"iron_sword","instance_uuid":"11111111-2222-4333-8444-555566667777"}
-> { success:true, itemId:"iron_sword", slot:"MainHand",
     instance_uuid:"11111111-2222-4333-8444-555566667777" }        (carried IN + echoed)

POST /api/entity/npc_equip_probe/unequip {"slot":"MainHand"}
-> { success:true, removedItemId:"iron_sword", slot:"MainHand",
     removed_instance_uuid:"11111111-2222-4333-8444-555566667777" } (SAME uuid comes back OUT)
```
Closes the equip/unequip handler-level gap: the item-instance identity survives the equip→unequip
cycle through the real API handlers (not just the EquipmentSlots primitive). Required a route fix —
the /api/entity/:id/equip route was not forwarding instance_uuid to the equip_item command.
