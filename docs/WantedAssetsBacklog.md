# Wanted Assets & Materials Backlog

The **"wanted assets" backlog** referenced by the content-library mechanism in
[`StructureGenerationPlacers.md`](StructureGenerationPlacers.md) Part 4 (and checklist U5). When a build needs
something the engine doesn't have, it is **logged here once** — never silently faked or improvised — and then
authored through the generate → validate → **user-approve** → persist flow so it becomes a permanent library
item (Part 4). This is how the engine, not Claude, remembers.

**Status legend:** `WANTED` (not started) · `IN-PROGRESS` · `DONE` (authored + approved + persisted) ·
`BLOCKED` (needs an engine feature first).
**Priority:** P1 (unblocks the most / biggest visual gap) → P3 (nice-to-have).

Initial contents are driven by the **Baldur's Gate 3 Act 3 gap analysis** (a dense fantasy city) measured against
the 28-material atlas (`materials.json`) and the Part 6 archetype library. All materials below are **real**
(medieval-plausible) unless noted; building *types* that are post-medieval/fantasy are genre-flagged in Part 6,
not here — materials don't need that flag.

---

## 1. Materials (atlas)

The atlas currently has 28 materials (terrain, stone family, wood family, Glass, Metal, Gold, Ice, foliage,
`glow`, Mirror, Thatch). A believable city reads through surfaces we don't have:

| Material | Driver (who needs it) | Priority | Status | Note |
|---|---|---|---|---|
| **Plaster / stucco / lime render** | every timber-framed `townhouse` / dwelling (half-timber infill) | **P1** | WANTED | the single biggest wall-surface gap; today a townhouse is bare wood or stone |
| **Roof tile (clay)** | all urban roofs (`townhouse`, civic, shops) | **P1** | WANTED | we only have rural `Thatch`; a city of thatch is wrong |
| **Slate** | better roofs, wealthier/civic buildings | P2 | WANTED | grounded medieval roofing |
| **Marble** | `temple`, `cathedral`, `civic_palace`, ornate interiors | P2 | WANTED | — |
| **Stained / coloured glass** | `church`/`temple`, `arcane_emporium` | P2 | WANTED | we have clear `Glass` only; needs the decal/image path too (§2) |
| **Lead / copper + verdigris** | domes, spires, flashing, civic roofs | P3 | WANTED | — |
| **Canvas / cloth** | awnings, market stalls, tents, sails, banners | P2 | WANTED | also a prop/decal concern (§2/§3) |
| **Carved / ornamental dressed stone** | reliefs, tracery, statuary, string courses | P3 | WANTED | may overlap the sculpture/trim asset work (§3) |
| **Bone / flesh / corruption** | horror interiors (e.g. a Bhaalist undercity temple) | P3 | WANTED | fantasy-horror palette |
| **Wattle & daub** | the cheapest vernacular infill (croft/slum) | P3 | WANTED | grounded in `structure_styles.json`; no dedicated material yet |

## 2. Decals / wall-art *(BLOCKED on an engine feature)*

There is **no decal / framed-picture mechanism** today (Part 4 flag, U6). It blocks a whole class of content that
a city's legibility is built from. The **engine feature** is the precondition (§4); the content below is authored
after it exists.

| Item | Driver | Priority | Status |
|---|---|---|---|
| **Pictorial shop / inn signs** | every shop, `tavern`, `arcane_emporium` (illiterate clientele → pictograms) | **P1** | BLOCKED |
| **Stained-glass window images** | `church`, `temple`, `cathedral` | P2 | BLOCKED |
| **Banners / heraldry** | `castle`, `civic_palace`, guildhalls | P2 | BLOCKED |
| **Tapestries / murals / frescoes** | `manor`, `ornate_house`, `temple` | P2 | BLOCKED |
| **Paintings (framed)** | ornate interiors (the Strahd-mansion driver) | P3 | BLOCKED |
| **Posters / proclamations** | `printing_house`, town notice boards | P3 | BLOCKED |

## 3. Templates / props

Furniture, fixtures, and street dressing. Some fixture templates exist (the `FurniturePlacer` maps to a few);
most of the rich set does not.

| Item | Driver | Priority | Status |
|---|---|---|---|
| **Pictorial sign-boards (geometry)** | the sign decals in §2 need a board/bracket to hang on | P2 | WANTED |
| **Market stalls / awnings** | `dress_street_life` (#48), `fish_market` | P2 | WANTED |
| **Chandelier** | great halls, taverns, `manor` (+ a point light) | P2 | WANTED |
| **Long / banquet table** | great hall, `tavern`, `civic_palace` | P2 | WANTED |
| **Statues / busts / sculptures** | `temple`, `civic_palace`, gardens, mausolea | P3 | WANTED |
| **Ornate banister / baluster** | stairs in `manor`/civic (place_stairs #12) | P3 | WANTED |
| **Function fixtures** (forge, anvil, loom, altar, bar, counter, vault door) | the Part 3 room programs + shop family | P2 | WANTED (audit which exist) |
| **Street props** (woodpile, cart, trough, well-head, midden, lamp post) | `dress_street_life`, `place_yard_props` (#29) | P3 | WANTED |

## 4. Engine features *(blockers — not assets; real engineering)*

These gate entire categories above and must be built in the engine, not authored as content.

| Feature | Unblocks | Priority | Status |
|---|---|---|---|
| **Wall-art / decal system** (place an image/decal on a face, with a frame) | all of §2 — signs, stained glass, banners, murals | **P1** | not started |
| **Water / shoreline + harbour** | the entire `Maritime` group + waterfront districts + sea-temples | **P1** | not started |
| **Subterranean: terrain excavation + multi-level connectivity** | Part 5 basements, Part 8 sewers/crypts/dungeons, castle dungeons | **P1** | not started (the same gap behind the basement stub) |
| **Style-overlay system** (gothic-horror, condition/decay) | the BG3 overlay buildings (vampire palace, haunted mansion) | P2 | thin/partial |
| **Settlement waterfront sub-tier** (Part 7) | siting wharves/quays relative to water | P2 | not started |

---

## Process

Each item follows the Part 4 loop: **detect → generate (variants → rank → repair → gates → user approval) →
persist → reuse**. Materials are authored via the texture pipeline (source PNG → `texture_atlas_builder.py`,
no recompile); templates via BlockSmith (text → `.voxel`); decals/water/excavation are engine work. Nothing here
is built yet — this file is the honest ledger of what's missing, so it gets authored **once** and the engine
keeps it, rather than re-improvised (and faked) every session.
