# Functional Wiring Backlog — the "W axis" (opened 2026-08-28)

> **Why this doc exists.** The corner towers shipped as solid drums with a decorative cone: they
> read as towers and no agent could ever use one. The audit that followed
> ([`StructurePipelineGaps.md`](StructurePipelineGaps.md) § 2026-08-28) found the same shape of
> defect across the pipeline, and one cause behind it:
>
> **The validation ladder has no rung for "is it wired?"** L1 exists · L2 structural invariant ·
> L3 an agent can traverse it · L4 live engine — none of these asks whether the emitted object is
> REGISTERED with the engine system that gives it its function. That is why `place_doors` scores
> "L3 ✅" in the [`ValidationLedger`](structure-generation/ValidationLedger.md) while nothing in
> the world can open a door: L3 measures whether the character box fits through the hole.
>
> **W (wired)** is the missing axis: *after a build, query the owning manager and assert the
> object is in it.* Every item below is stated as a W-test you can write red first.

## Ordering

Sorted by (unlocks-the-most × cheapest), not by severity. W3–W7 are contained correctness fixes.
W8+ are not wiring at all: no system exists to wire to, so they are feature proposals and must be
scoped as such, never as a small fix.

> **⚑ USER DIRECTION 2026-08-28 — this whole backlog is NOT the current priority.** I proposed
> "W1 + W2, so an NPC walks into the tavern and sits down" as the next milestone. That was wrong:
> **the seated POSE is not solved** — characters warp when sitting, and the user has attempted this
> with Claude several times and been failed every time. W1 is therefore gated on an unsolved
> character-animation problem, and I had not checked that before proposing it. Do not re-pitch
> W1/W2 as a quick win.
>
> For the record, since it will be needed eventually: the warping is already MEASURED and filed in
> `tools/interaction_pipeline/engine_fix_queue.json` — `INITIAL_TELEPORT` (`sitAt()` places the
> root at the END-of-clip hips reference at t=0, so the character is teleported onto the seat
> before `stand_to_sit` plays: `AnimatedVoxelCharacter.cpp:1806-1840`), `POSE_FEET_DESYNC`
> (`:1875-1900`, foot-anchor lerp on a different alpha than the bone blend) and
> `SEATED_THIGHS_NOT_HORIZONTAL` (the `sitting_idle` clip is not a seated pose: knees 0.439 m below
> hips vs a ±0.35 tolerance). Also structural: `worldPosition` re-snaps at EVERY sit transition to a
> hips reference sampled from a different clip, with `getCameraTrackPosition()` compensating so the
> camera hides it — three references that disagree, papered over at the camera. **A previous
> agent's "clear and easy fix" read on this has failed repeatedly. Treat it as hard.**
>
> Current priority is structure CONTENT (castle/keep, mansion, further functional typologies) plus
> the interior-lighting bleed — see [`CityForgePlan.md`](CityForgePlan.md).

---

## W1 — Furniture must carry interaction points (SMALL, unblocks seating)

**Defect.** `PlacedObjectManager::placeTemplateMicro` (`PlacedObjectManager.cpp:700-741`) never
populates `obj.interactionPoints` and never records `metadata["kinematic_part_ids"]`. The sibling
`placeTemplate` does both (`:681`, `:687`). Every generator-placed interior fixture and yard prop
goes through the micro path, so `find_fitting_seat` / `sit_character` find **zero seats in a
freshly built settlement**, and a chest lid cannot even be animated.

**Three separate faults, all needed:**
1. `placeTemplateMicro` must compute interaction points and kinematic parts like its sibling.
2. `recomputeAllInteractionPoints` reads `obj.position` (the FLOORED cube), not `obj.microAnchor`
   — so even the reload-time self-heal lands points up to ~0.89 m off the real seat.
3. `chair.voxel` — the chair the placer actually emits — has **no `# interaction_point:` line at
   all**, so it can never be a seat regardless. Only `stool`, `stool_low`, `chair_wood`,
   `bench_wood`, `bench_great`, `bar_stool` carry one.

**Latent regression to close in the same pass:** those six templates declare
`method: tools/regen_furniture.py`, but that script writes the seat anchor only into the
`.metrics.json` sidecar (`regen_furniture.py:128`) and never emits the `# interaction_point:`
header — re-running it would silently delete every remaining seat point in the library.
(`chair.metrics.json` already has a `seat_0` anchor its `.voxel` does not.)

**W-test owed:** build a tavern, then assert the taproom's chairs/benches are returned by
`find_fitting_seat` and that a character `sit_character`s on one at the measured seat height.
Teeth: strip the interaction points and the same query must return nothing.

---

## W2 — A scheduled NPC must be able to enter a generated interior (LARGE, the big one)

**Defect.** Every location anchor is deliberately pinned two cells **outside** the wall
(`StructureRealizer.cpp:120-131`) because the NavGraph cannot route exterior→interior
(`StructureBuildService.cpp:137-143`, recorded as "measured: 12x no_route"). So no scheduled NPC
ever crosses a threshold: the seats, hearths, beds and tableware sit in rooms nobody visits.
**This is the single largest geometry-only surface in the pipeline.**

**First task is to re-measure, not to trust the comment** — establish today whether the failure is
doorway aperture in the nav grid, the anchor's own cell, or route cost, before designing a fix.

**W-test owed:** a generated tavern with a resident whose schedule targets an INTERIOR anchor;
assert the NPC reaches it. Teeth: seal the doorway and the same route must fail.

**Depends on W1** to be worth anything — an NPC that walks in and finds no seat has nowhere to go.

---

## W3 — Generated doors must be doors (MEDIUM)

**Defect.** `registerDoor` has no call site in generation (only `DoorManager.cpp` itself and the
MCP handler in `editor/src/Application.cpp`). Openings are carved to air and framed; no leaf is
placed, though `door_wood.voxel` and siblings are fully authored **with handle interaction
points**. Wall gates and fence gates are the same — `gate_timber.voxel` is an orphaned asset with
zero references anywhere in `engine/`.

**W-test owed:** after a build, every `assembly_plan.openings[]` door has a leaf placed and a
`DoorManager` registration; `open_door` on it changes its state. Teeth: an unregistered opening
must fail the same assertion.

---

## W4 — Cheap correctness batch (each an hour or two)

| # | Defect | Fix |
|---|---|---|
| a | `tower_house` gets **ZERO residents** — the only `room_program.json` typology falling through `locationTypeForTypology` to `LocationType::Custom` (`StructureRealizer.cpp:70-79`); `ResidentPlanner.cpp:35-37` skips anything not Home/Work/Tavern | add it to the Home list; assert residents > 0 per typology in a data test so the next typology cannot repeat it |
| b | **Arrow loops are letterbox windows**: `TowerForge.cpp:230-234` cuts 6 micro across a whole rim cube column → **9 wide × 6 tall × 9 deep micro** (~1.0 × 0.67 × 1.0 m), ~1.5× wider than tall — the inverse of a real loop (~30 mm wide, 1–2 m tall) | cut a 1–2 micro × 18+ micro vertical slot with an internal embrasure splay; assert the aspect ratio, not just "a gap exists" |
| c | `MarketDressingTest.DressedSquareStaysWalkable` is **documented as L3 but is a 2-D 4-neighbour cube flood** — no agent box, no height, no step-up, so it cannot detect a headroom/width/step failure | replace with a `TraversalProbe` walk + teeth; correct the label in `CityForgePlan.md` |
| d | **Fence gate-at-door has no agent coverage on the shipped path**: `FencePolicyTest.GateWindowFollowsTheDoor` asserts integer arithmetic only; the one real fence-gate walk (`ParcelFenceTest:74`) exercises `planParcelFence`, which is **dead in production** (the stamper uses `planParcelFenceRuns` + `fenceGateWindowAt`); `SettlementWalkabilityTest:218` walks the older centred `fenceGateWindow` | point a probe walk at `fenceGateWindowAt` with a sealed-gate control; delete or reinstate `planParcelFence` rather than leaving a dead twin |
| e | **One tower clipping one building refuses the WHOLE circuit** (`TownWall.cpp:157-163`); with `tower_size: 9` protruding into the site, a dense city can lose its entire wall to a single overlap | shrink or drop that one tower and report it, the way `towers_usable` already reports the solid fallback |
| f | **Nav obstacles missing outside the editor**: `setNavObstacleProvider` is wired only at `editor/src/Application.cpp:1655`; `GameShell` never calls it, so in a packaged game every well, stall, statue and woodpile is nav-invisible | move the provider into the shared runtime |
| g | `NPCManager.cpp:378-387` clamps the nav grid to **±256 columns** and logs that NPCs outside it have no nav — a large or streamed city falls partly outside | at minimum surface it as a build-time warning tied to site extent |

---

## W5 — Windows that can actually open (MEDIUM)

About half of all windows are permanently solid: open/closed is a fixed per-opening hash
(`StructureRealizer.cpp:353-357`) and the "closed" leaf is painted into the **static** micro canvas
(`:371-373`, `:396-398`) — masonry, not a shutter. No kinematic part, no manager, can never open.
No typology declares `glass` yet either, so "you can see through it" is false in practice.
Shares its mechanism with W3 (a leaf + a manager registration).

---

## W6 — Wall-walk access (MEDIUM; folds into the castle/keep work)

Proven unreachable by `TownWallPassageTest.AuditTheWallWalkIsCurrentlyUnreachable`: there IS
standing room on the inner course and no agent can get to it, so the crenellations decorate a
surface nothing can reach. Wants a mural stair or a tower doorway at walk level. **When it ships,
flip that test's `EXPECT_FALSE` to `EXPECT_TRUE`** — it is written to be flipped.

Related and already logged: the road does not reach the gate (~4 cubes of raw ungraded terrain
between the paved street end and the outside of the gateway, because the band sits outside
site+margin while paving covers only the street rects inside the site).

---

## W7 — Signs should point at something (SMALL)

Which board hangs is genuine typology data, but `setMetadata(sid, "signage", …)` is never read by
anything, and it stores a **PlacedObject id, not the Location id** — there is no sign↔location
lookup in either direction. All sign items are `"fixed": true`, so they get no pickup point and
cannot be interacted with or read. Minimum useful step: store the Location id and expose the
lookup, so "the smithy's sign" is addressable.

---

## W8+ — NOT wiring: no system exists to wire to

State these as feature proposals with their own design-keys gate. Calling any of them a small fix
would be dishonest.

- **Trade.** `LocationType::Market` / `GuardPost` / `Temple` / `Farm` have **no producer** anywhere
  — only the string↔enum converters (`LocationRegistry.cpp:16-19,29-32`). `Schedule::merchantSchedule`
  targets a hardcoded `"market"` id that nothing ever registers. So the square, its stalls, the well
  and the statue are pure voxels: no vendor, no water, no buying or selling.
- **Crafting.** **No crafting system exists at all** — `engine/{src,include}/core` contains no
  crafting file. The anvil, bellows, oven, workbench and forge are props. (CLAUDE.md's
  `CraftingSystem` entry was stale and was removed 2026-08-28.)
- **Containers.** No container system; `KinematicAnimator.h:32` says "future ContainerManager".
  `LootTable.cpp` exists with **no call sites**. Chests, barrels, crates and wardrobes hold nothing.
- **Ranged / line-of-sight.** Nothing reads `loopCells`; arrow loops cannot be fought from even
  once W4b makes them the right shape.
- **NavGrid road bias.** No road or paving term in `NavGrid.cpp` / `AStarPathfinder.cpp` — paving is
  uniform-cost terrain, so nothing makes an NPC prefer a street to a garden.

---

## Not in this backlog

These came out of the audit **clean**, and are worth knowing as the standard the rest should meet:

- **Hearth / chimney** — genuinely wired: real fuel item props, a real engine point light per
  emitter, survives reload.
- **Stairs** — genuinely L3 with teeth (`TavernUpstairsTest` + its `WithoutStairUpstairsIsUnreachable`
  control), incl. the silent-failure paths.
- **Town-wall gates and corner-tower entry** — proven functional 2026-08-28 by
  `TownWallPassageTest` (6 tests: four-gate passage, sealed-gate control per side, squat-lintel
  control, and the composed town → tower doorway → top chamber walk).

## Resolved W-axis findings

- **2026-09-29 — NPC `equip_item` showed a placeholder, not the item.** `equip_item` on an NPC
  registered the item in `EquipmentSlots` correctly but attached a hard-coded grey box
  (0.15×0.4×0.15) to `right_hand` — a pre-fine-voxel stub — so an equipped `iron_sword` never
  showed `weapons/sword_long.voxel`. Found by the user during the UniMate clip-review session
  ("that doesn't look like the iron sword"). Fix (`Application.cpp`): `updateNpcHeldItems()` now
  derives the held item from the equipped MainHand (a `CombatBehavior` weapon keeps precedence)
  and builds the real template through the same grip path as the player; the stub attach and the
  `detachAll()` on unequip are gone. W-test still to write: after `equip_item`, assert
  `m_npcHeld[name].itemId == itemId` and the kinematic group exists.

## Also still open, tracked elsewhere

[`CityForgePlan.md`](CityForgePlan.md): M6 `mansion` typology, castle/keep precinct, gatehouse
rooms, wall following the terrain contour instead of a rectangle, settlements tolerating gentle
elevation, and interior point lights bleeding through walls.
