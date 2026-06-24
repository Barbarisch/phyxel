# 16 · place_furniture

> Tier: Interior. Part-1 status: **D** (`FurniturePlacer`, 4 unit tests pass). Schema: [`README.md`](README.md).
> This spec documents the **real implementation** + its upgrade path.

## Job
Place furniture by room purpose — casegoods against free walls (facing into the room), tables centred — on the
floor, without overlap.

## Reads
- `ProgStory` rooms (rect, purpose); portals (to find + avoid door walls); the structure origin + `floorY`.

## Emits
- `std::vector<FurniturePlacement>` (`type`, `worldPos`, `rotation`, `room`) → spawned as templates parented to the structure.

## Algorithm *(as implemented)*
1. Per room (skip if `< 2×2`), detect which of the 4 walls carry a door/window (avoid backing furniture onto them).
2. `recipeFor(purpose)` → the piece list.
3. Casegoods → the first free (door-less, unused) wall; `facingIntoRoom(inwardDx,inwardDz)`: **min-x→270, max-x→90, min-z→0, max-z→180**.
4. Tables → room centre.
5. On-floor (`y = floorY`); an `occupied` set prevents overlap.

`recipeFor`: kitchen→{counter, fireplace}; bed/chamber/solar→{bed, chest}; hall/living/great→{fireplace, table(centre), bench}; service/pantry/store→{barrel, chest}; default→{chest}.

## Satisfies (checks)
K1 (purpose-appropriate furniture), K (facing into room, on-floor, no overlap, clearance). **Partially** T (room function testers).

## Engine capability needed
- `FurniturePlacer::furnish` — ✅ DONE.
- `placeTemplate` parented to the structure — ✅.

## Failure modes *(known / remaining)*
- Recipes are **minimal** — they don't yet satisfy the full Part 3 room programs / T-testers (a bedchamber gets bed+chest, not the full bed/chest/stool/light/washstand).
- Only a few fixture templates exist; **`chest` template is missing** (skipped + logged).

## Function testers
- **F1** Every furnishable room gets its purpose recipe.
- **F2** Casegoods face **into** the room (the `facingIntoRoom` convention — unit-tested).
- **F3** On the floor, no overlap, off the door walls.
- **F4** *(target)* the placement satisfies the room's **T-tester required fixtures** (the upgrade).

## Grounding
- Object dims — `object_dimensions.json` (21 archetypes). Facing math — unit-tested (`FurniturePlacerTest`).

## Open questions / upgrade path
- **Drive `recipeFor` from the Part 3 room programs** (required/typical/service) instead of the hardcoded map — the main upgrade.
- Author the missing fixture/furniture templates (chest, etc.) → backlog §3.
