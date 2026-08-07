# Items-as-Props in Structure Generation — Plan

**Status: SHIPPED 2026-08-07** (commits `908a911c` → `d279683a` → `9ab4a9f9` → `29f39ed7`
— ALL four sequence steps done). Beyond the core: `as:"item"` realization shipped (rugs =
`rug_woven` props), `placeSurfaceItems` is MULTI-PLANE (a back bar stocks all its shelves;
`gen_back_bar` ships empty shelves — baked bottles removed), items rest surface-exact
(stacked-lift hover fixed), and item clicks aim along the cursor ray with tight prop AABBs.
Still open: `candle_stand` → item (scale mismatch: floor stand vs table candlestick);
barrels/signs stay baked (no fine item equivalents). What shipped first: `Deps.itemProps` +
consumer spawn via `ItemPropManager` (static-first, no bodies), **measured** surface
heights via `FurniturePlacer::templateTopUnits` (max occupied Y over the table
template's cube/sub/micro/fine tiers — not the metrics sidecar, and not `floorY+1`),
per-purpose `"surface_items"` recipe sets in `furnishing_recipes.json`,
`setParent` cascade so rebuild-at-same-origin stays flat (L4-verified: tavern_2 →
tavern_3, 89 placed objects before and after), `isClutter` knows the tableware.
Tests: `ItemPlacementTest` (recipe→holdable, measured height, removal-cascade
control); the no-overlap test was NOT written — spots come from the deterministic
seeded picker, revisit if items ever visibly collide. Goal: generated interiors
place loose objects — rugs, candlesticks, tankards, tomes, cutlery — as **item
props** (`ItemPropManager::spawnProp`: pickup-able, physics-capable, never baked)
instead of chunk-baked voxel templates. Direction set by the user: "rugs should
be items, not static microcubes"; extended to all loose furnishings.

## Where it plugs in (scouted, file:line verified 2026-08-06)

- The furnish flow is `furnishing_recipes.json` → `FurniturePlacer::furnishFromPlan`
  → neutral `FurniturePlacement` records → the CONSUMER loop in
  `StructureBuildService.cpp:707-733` resolves type → template
  (`FurnitureCatalog::templateFor`) and calls `placeTemplateMicro`.
- **Insertion point is the consumer, not the placer**: recipe entries gain
  `"as": "item", "item": "<itemId>"`; the consumer branches to
  `itemPropManager->spawnProp(...)` for those. `FurniturePlacer` stays untouched
  (pinned by `FurnishPlanEquivalenceTest` — must stay byte-identical).
- The surface-clutter pass (`StructureBuildService.cpp:779-798`) is the first
  conversion target: today it bakes hardcoded `{"mug","mug","bottle"}` templates
  at a GUESSED height (`storyFloorY + 1`, cube-snapped). It becomes item spawns
  (tankard/bottle_wine/plate/candle) at the table's MEASURED surface height.
- `StructureBuildService::Deps` (`StructureBuildService.h:39-48`) needs an
  `ItemPropManager*` member, wired from Application.

## Known conflicts to fix first (from the scout)

1. **Orphan props on rebuild (RED today):** `registerItemProp` takes no
   `parentId`, so structure-placed props would survive a rebuild-at-same-origin
   sweep (`StructureBuildService.cpp:386-403` removes only `category=="structure"`
   descendants) and duplicate. Fix: `parentId` param on `registerItemProp` +
   thread through `spawnProp`, so props cascade-remove with their structure.
2. **Physics vs determinism: RESOLVED 2026-08-07 by static-first.** `spawnProp`
   now defaults to `dynamic=false` — a settled kinematic with NO body and the
   exact authored pose. Structure-placed items simply use the default: fully
   deterministic, zero physics cost, and they still revive on explicit hit
   (`hitProp`) or when thrown.
3. **Overlap validator:** `RealizedWorldValidator::isClutter()` must treat item
   props as clutter or every tankard-on-table flags the fixture-overlap scan.

## The four red tests (write first, in this order)

1. `ItemCatalogTest` — every recipe entry `as:"item"` resolves to a holdable
   `ItemDefinition` with a loadable template (twin of `FurnitureCatalogTest`).
2. `ItemSurfacePlacementTest` — item Y == the table's MEASURED surface height
   (metrics sidecar `surface_y` / occupied-micro scan), not `floorY+1`. The
   current guess is the red.
3. `ItemPlacementNoOverlapTest` — item AABBs pairwise disjoint + inside the
   supporting fixture's footprint.
4. `ItemPropRebuildIdempotenceTest` — rebuild at same origin does NOT double
   the prop count (red today, conflict #1).

Ledger: moves `ValidationLedger.md` row **#19 place_clutter** (required L1,
current L0) and adds an `item_asset_coverage` twin of #16a.

## Sequence

1. Fix conflict #1 (parentId) + red tests 1 & 4 → green.
2. Recipe schema `as:"item"` + consumer branch + Deps wiring; convert
   candle_stand → candlestick item, add rug_woven to taproom/chamber recipes.
3. Convert the surface-clutter pass to items at measured surface heights
   (red tests 2 & 3 → green); recipe-driven clutter sets per room purpose
   (taproom: tankard/bottle_wine/plate; study: tome/candle; kitchen: bowl/jug/
   frying_pan/ladle).
4. L4: build a tavern via `POST /api/structure/build`, verify props are
   pickable/kickable, rebuild idempotence live, screenshot evidence.
