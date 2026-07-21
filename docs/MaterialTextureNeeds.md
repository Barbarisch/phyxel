# Material & Texture Needs

> **Standing directive:** whenever a structure/furniture/prop wants a material or texture we
> don't have, record it HERE. The user plans to overhaul the texture system for more detailed,
> better-looking results (explicitly *not* Minecraft-style flat blocks), so this list feeds that.

## Current materials (102, from `resources/materials.json` — verified 2026-07-21; up from 19 when
this doc was first written)
The atlas has grown substantially since this doc's original terrain/exterior-only snapshot: the
period roofing family (below), biome log/leaf variants, `StoneTiles`, `WoodWalnut` (dark stained
furniture wood), `Linen`/`Wool` (first cloth materials), `rug_oriental` (a real projected rug
texture), `enchanted_log`, and the `vox_00`-`vox_47` legacy palette (48 entries, being retired)
now sit alongside the original terrain/exterior set. See the sections below for what's still
missing — several rows here were satisfied by these additions and have been marked accordingly.

## Period ROOFING — DONE (2026-07-02, `tools/gen_roof_materials.py`)
Full vernacular roof family shipped: **Thatch** (redo), **ClayTile**, **WoodShingle**,
**Slate**, **StoneSlab** (all 512px, `varied:false`). Alignment is art-only, in the voxel
idiom (no projected/sloped geometry): the *stepped* subcube roof supplies the course rhythm,
so each texture has ONE course lip at the tile's bottom edge (period = subcube step, 3/tile)
instead of baked mid-face lines, and runs consistently down-slope (image-Y) so straw/grain/seams
continue over the step lip. `timber_cottage`→Thatch, `stone_manor`→ClayTile (placeholders
removed); Slate/StoneSlab wired into `vernacular_materials.json` stone-upland regions.
| Was wanted | Status |
|------|--------|
| Thatch (cottage/croft) | ✅ Thatch (v2, courses fixed) |
| Clay tile / slate (manor/townhouse) | ✅ ClayTile, Slate |
| Wood shingle / stone slab | ✅ WoodShingle, StoneSlab |
| **Wattle & daub (limewashed)** | ❌ still wanted — timber-cottage wall infill; currently "Wood"; real daub is a limewashed off-white panel between dark timbers. **The remaining vernacular gap.** |

## Resolution / quality pass (texture-array is mixed 512/1024 BC7; see CLAUDE.md)
**Tier 1 DONE (2026-06-30, `tools/gen_highdef_materials.py`, commit 2ea8b8d):** materials
that shipped as 64x64 (blurry upscales) regenerated with real detail —
Gold/Metal/Glass/Mirror bumped to **1024**; Leaf (+birch/spruce/jungle/autumn), glow,
Thatch, Default at **512**. Also fixed the **Mirror** material (pointed at a missing
`Glass_side.png` -> rendered fallback; now uses `mirror_*.png`).

**Tier 2 DONE (2026-06-30, commit 250c2db):** Stone, Cobblestone, Sandstone, Log,
LogBirch, LogSpruce, Ice bumped to **1024** and re-sourced from their ambientCG CC0
assets at 1K via `tools/fetch_cc0_textures.py` (real photo textures + `_nr` PBR sidecars,
not procedural). Leave pure terrain (Dirt/Grass/Sand/Gravel/biome grass) at 512. Skip
`vox_00`–`vox_47` (Barony palette, being retired for per-voxel tint).

**Remaining nicety (not blocking):** logs use one bark tile on all 6 faces (`{"all":
"Bark00x"}`) — top/bottom caps show bark, not tree rings. A ring-cap texture for log
top/bottom would finish them.

## Missing — projected SURFACE art (surfaced by VoxelAppearanceModel Phase 3, 2026-06-30)
The planar projected-surface path now works (rugs/paintings/banners stretch one image across the
whole prop — see `docs/VoxelAppearanceModel.md` §7 Phase 3). It needs real art; only the
placeholder `surface_test` (asymmetric labeled grid, `tools/gen_surface_textures.py`) exists.
| Need | Used for | Notes |
|------|----------|-------|
| **Rug / carpet patterns** (Persian, woven, braided) | floor rugs (`# surface: … axis=y`) | ✅ partially done — `rug_oriental` material + `resources/templates/rug_oriental.voxel` now ship a real projected rug (verified in `resources/materials.json`); more patterns/colourways still wanted, and `rug.voxel`/other floor props may still use `surface_test` |
| **Painting / portrait images** | wall paintings (`axis=z`/`x`) | framed canvas; pairs with a thin frame prop |
| **Banner / tapestry / heraldry** | hanging wall banners | tall vertical projection |
| **Mosaic / tile floor patterns** | floor medallions | same path as rugs |
Open engine follow-ups (not art): a dedicated surface-texture registry (MVP reuses the material
array, so each surface image is a texture-carrier material like `burning_wood`/`surface_test`).

## Missing — interior / finish
| Need | Used for | Notes |
|------|----------|-------|
| Plaster / stucco (white & tinted) | interior walls | most interior walls aren't bare stone |
| Wallpaper (patterned) | manor/parlor walls | gothic damask for Strahd interiors |
| Dark / stained / polished wood | fine furniture, panelling, floors | ✅ done — `WoodWalnut` (dark chocolate-brown, oiled/waxed) shipped, verified in `resources/materials.json` |
| Wood floorboards (distinct from planks) | floors | directional plank floor |
| Marble / tile (floor + wall) | grand floors, baths | checkerboard, veined |
| Carpet / rug (patterned) | floors | area rugs, runners |
| Plain plaster ceiling | ceilings | currently ceilings are the floor-above's underside |

## TOP PRIORITY (from the furnished-mansion walkthrough, 2026-06-21)
The furniture is forced to reuse a tiny palette, and **Bricks (red) is badly overused** — it's
standing in for: bed coverlets, four-poster valances, book spines, and rug borders, so red brick
shows up all over the interior. Highest-impact additions:
- **Rug / carpet textures** (patterned: oriental, runner, oval; burgundy/blue/green) — a rug should
  NOT be brick-red stone. This alone fixes much of the "too much brick" look.
- **Cloth/linen/wool/velvet** in several colours (white linen sheets, wool blankets, velvet
  drapes/upholstery) — replaces Bricks/Sandstone/Sand stand-ins on beds, coverlets, valances,
  armchairs. ✅ `Linen` (undyed sheets/sacks) and `Wool` (dyed madder-red coverlets/blankets) now
  ship — velvet/upholstery colourways still wanted.
- **Book-binding material** (leather/cloth spines, reds/greens/browns/gilt) — replaces the
  Bricks/Log/Gold spine stand-ins.
- **Dark/stained wood** distinct from light Wood — for ornate/manor furniture vs plain. ✅ done
  (`WoodWalnut`).

## Missing — soft goods / cloth
`Linen` and `Wool` now exist (verified in `resources/materials.json`, 2026-07-21) — this category
is no longer "no cloth material exists at all." Remaining gaps:
| Need | Used for |
|------|----------|
| ~~Linen / bedsheet (white/cream)~~ | ✅ done — `Linen` |
| ~~Wool blanket / quilt (colored)~~ | ✅ done — `Wool` (madder red; more colourways still wanted) |
| Cushion / upholstery (leather, velvet) | chairs, sofas, padded furniture |
| Book bindings (leather, cloth — reds/greens/browns/gilt) | books on shelves currently reuse Bricks/Sandstone/Log/Gold/Metal/Leaf as spine stand-ins |
| Drapery / curtain (heavy, colored) | windows, four-poster beds |
| Canvas / tarp | tents, sacks |

## Missing — metal / hardware / accent
| Need | Used for |
|------|----------|
| Iron (dark, banded) distinct from Metal | door bands, hinges, fixtures, gates |
| Brass / bronze | lamps, fittings, candlesticks |
| Wax / candle + flame | lamps, candelabra (pairs with the lighting backlog) |
| Slate / wood-shingle roof | roofs (Stone roof reads flat) |
| Stained glass | church/manor windows |
| Tapestry | manor walls |

## Missing — nature / misc
| Need | Used for |
|------|----------|
| ~~Thatch~~ | ✅ done, see "Period ROOFING — DONE" above — this row was stale, left over from before roofing shipped |
| Cracked / mossy stone variants | ruins, age (Strahd is decayed) |
| Cobweb | abandoned interiors |
| Bone / skull | crypts |
| Water (still + flowing) | wells, fountains, moats |
| Soil/planter, foliage variants | gardens, courtyards |

## Texture-system overhaul (user intent)
The user intends to **redo the texture system** for higher fidelity. Implications to keep in mind:
- We want geometry (sub/microcube relief) AND richer surface texture — not flat Minecraft tiles.
- Per-material PROPERTIES beyond color (roughness/spec, emissive, normal/relief) would let
  fewer materials look far better.
- When the new system lands, the structure/furniture pipelines should be re-audited to use the
  new materials (this doc is the worklist).

See also: `docs/structure-generation/StructurePipelineGaps.md` (asset backlog: furniture, door types, lamps+lighting).
