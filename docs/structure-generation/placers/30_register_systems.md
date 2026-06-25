# 30 · register_systems

> Tier: Systems. Part-1 status: **P**. Schema: [`README.md`](README.md).

## Job
Wire the finished structure into the engine's runtime systems — **doors → DoorManager**, **occupancy grids**
(CPU + GPU), **navgrid**, **location markers**, NPC/story hooks. The build isn't "done" until it's registered.

## Reads
- The placed structure (walls / floors / doors / rooms / portal graph); the engine systems.

## Emits
- DoorManager registrations; per-chunk **occupancy grids** rebuilt on every touched chunk; navgrid nodes; per-room **location markers**; NPC spawn/route hooks; collision.

## Algorithm
1. Walk the finished structure.
2. Register every door (DoorManager).
3. **Rebuild occupancy grids** on all touched chunks (the `buildAllChunkPhysics()` rule).
4. Build the navgrid from the portal graph + floors (every room reachable).
5. Tag location markers per room (NPC/story).

## Satisfies (checks)
N (engine/systems integration), G (navgrid reachability == the portal graph), and the **"every DB-load path must call `buildAllChunkPhysics()`"** rule.

## Engine capability needed
- DoorManager register — ✅; occupancy rebuild (`buildAllChunkPhysics`) — ✅; NavGrid/NavGraph — ✅; location markers (`add_location`) — ✅.

## Failure modes
- Occupancy **not rebuilt** → characters fall through / float (the known rule).
- Doors not registered → won't open.
- Navgrid not built → NPCs can't path.

## Function testers
- **F1** Every door registered with DoorManager.
- **F2** Occupancy rebuilt on **all** touched chunks.
- **F3** Navgrid matches the portal graph (every room reachable).
- **F4** A location marker per room.
- **F5** A character can stand / walk / open doors at runtime.

## Grounding
- N/A (systems wiring, no dimensions).

## Open questions
- GPU + CPU occupancy sync timing; incremental vs full physics rebuild after a structure place.
