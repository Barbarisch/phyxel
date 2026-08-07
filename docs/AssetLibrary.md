# The Asset Library — layout, index, aesthetics, and how to add assets

**Status: REORGANIZED 2026-08-07.** This is the canonical guide for anyone —
an LLM session, a world-building pipeline, or a human — choosing or adding
voxel assets. Read this before touching `resources/templates/`.

## Layout (the taxonomy)

```
resources/templates/
  weapons/        wieldables: swords, axes, staffs, bows... (fine-grid V format)
  items/          holdable props: tableware, lighting, books, rugs... (fine-grid)
  furniture/      placed fixtures: tables, chairs, bar, doors' furniture, clutter
                  templates (mug/bottle) + .metrics.json sidecars
  nature/         trees (tree_*/forge_*), bushes, shrubs, mushrooms
  architecture/   doors, gates, ladders, pillars, whole-building templates
  imported/       external imports (barony_* — LOCAL USE ONLY, (c) Turning Wheel)
  decor/          (reserved) surface-projected wall/floor art not registered as items
  test/           test rigs (*_test) — never shipped, never placed by generators
  generated/      BlockSmith LLM output lands here (quarantine until curated)
  template_catalog.json   THE INDEX (generated — see below)
  items_manifest.json     gen_items.py sidecar: dims + grip points
```

**Rules (validated by `tools/asset_index.py --validate`):**
- **Stems are UNIQUE across the whole library.** Everything references templates
  by stem (world DBs, flora, FurnitureCatalog); the engine's recursive scan
  hard-errors on collisions (`STEM COLLISION` in the log) and refuses the
  second file. Never create `items/foo.voxel` when any `foo.voxel` exists.
- **No `.voxel` strays at the library root** — every template lives in exactly
  one category dir. Sidecars (`.metrics.json`) sit beside their template.
- The engine resolves by stem OR by relative path (`items/torch`); item
  definitions (`items.json`) always use the relative path.

## The index — template_catalog.json

**Generated, not hand-maintained**: run `python tools/asset_index.py` after any
template change (generators that register entries still work; regeneration
preserves intent fields — display_name, description, tags, prompt/model —
and recomputes everything measurable). Each entry:

- `file`, `category`, `resolution_class` (`cube`/`subcube`/`microcube`/
  `fine-27`/`fine-81`), primitive counts, `dims_units`, `materials`
- `item_ids` — gameplay items (items.json) using this template
- `grip_point_units` (wieldables), `surface` (projected-surface assets),
  `metrics_sidecar` (furniture conformance data exists)

`search_templates` (MCP) queries this index. **Pipelines choosing assets should
filter on `category` + `tags` + `resolution_class`** — e.g. a tavern furnisher
wants `category=items, tags∩{tavern,tableware,light}`.

`--validate` fails on: root strays, duplicate stems, unknown category dirs,
dead `items.json` templateFile references. Run it before committing assets.

## Aesthetic contract (what "fits the engine" means)

1. **Resolution follows role.**
   - *Items* (anything a hand holds or a table carries): **fine grid**
     (`# grid: 81`, 1 cell ≈ 1.23 cm; broad flat props may use 27). Authored
     at TRUE world scale, `held.scale` stays 1.0. Full-cube or bare-microcube
     handtools are DEFECTS (`lint_voxel_detail.py` enforces).
   - *Furniture/fixtures*: microcubes (regen_furniture.py DSL), dims conforming
     to `object_dimensions.json`.
   - *Nature*: branch-driven generators (tree_forge/gen_tree) — cubes for
     interior mass, sub/micro shell detail, unconditional (never behind flags).
   - *Buildings*: come from the STRUCTURE GENERATOR, not templates (the
     architecture/ house templates are legacy references).
2. **Materials carry physics + texture; tints carry color detail.** Steel/iron
   parts use the bright `Steel` material (tint MULTIPLIES albedo — mid-gray
   tints on dark `Metal` render near-black). `varied` only on soft naturals.
3. **Small round things need shading, not geometry**: radial/edge tint
   gradients fake curvature at 1.2 cm cells (see gen_items.py candlestick);
   thin walls are RINGS, not solid discs.
4. **Flat art uses projected surfaces** (`# surface:` header): rugs, signs,
   tome covers, banners — one image spans the object; keep the board ONE fine
   cell thick.
5. **Loose objects are ITEMS, not baked voxels** — anything a player could
   pick up ships as an items.json entry (physics prop + pickup). Chunk-baking
   is for terrain and structural fixtures only.
6. **Furniture stays chunk-baked microcube (decided 2026-08-07).** Fine-grid
   kinematic furniture would forfeit chunk culling/greedy meshing/per-voxel
   lighting; instead, fine detail arrives as ITEM accents placed on furniture
   (tankards, candlesticks, tomes) + richer tints. Items spawn STATIC-FIRST
   (no physics until dropped/thrown/hit — docs/FineVoxelItems.md §Physics).

## Adding assets — the paths

| What | How |
|---|---|
| Weapon/tool/tableware/prop | Add a builder to `tools/gen_items.py` (deterministic DSL; writes template + grips manifest + catalog), add the items.json entry (gripOffset = −manifest grip point), regenerate, verify in-engine (spawn + in-hand screenshot) |
| Furniture/fixture | `tools/regen_furniture.py` builder + `object_dimensions.json` canon + conformance test |
| Tree/plant species | `tools/tree_forge.py` / `tools/gen_tree.py --batch tools/tree_library.json` |
| CC0 MagicaVoxel pack | `tools/vox_import.py pack.vox -o resources/templates/<category>/name.voxel --scale fine` (auto-detects RIFF; per-voxel tints) |
| LLM one-off | `tools/blocksmith_generate.py` → lands in `generated/`; curate (rename/move/polish) before pipelines may use it |

After ANY of these: `python tools/asset_index.py` (regenerate + validate),
then verify live (`spawn_item` / `spawn_template` + screenshot). New items are
instantly available in the editor's **Item Equipper** panel.

## For future LLM sessions, in one breath

Query `template_catalog.json` (or `search_templates`) — filter category/tags/
resolution_class; items link to `items.json` ids for spawn/equip. Never
hand-place voxel lookalikes of things the library or generators provide; never
create duplicate stems; run `asset_index.py --validate` before committing.
