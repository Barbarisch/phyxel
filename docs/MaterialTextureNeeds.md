# Material & Texture Needs

> **Standing directive:** whenever a structure/furniture/prop wants a material or texture we
> don't have, record it HERE. The user plans to overhaul the texture system for more detailed,
> better-looking results (explicitly *not* Minecraft-style flat blocks), so this list feeds that.

## Current materials (19, from `resources/materials.json`)
Default, Dirt, Grass, Stone, Cobblestone, StoneBricks, Sand, Gravel, Wood, Log, Bricks,
Sandstone, Glass, Metal, Gold, Ice, Leaf, glow, Mirror.

These skew **terrain/exterior**. Interiors, furniture, and finish work are starved — we keep
substituting (e.g. bed mattress = Sandstone, pillow = Sand) because nothing better exists.

## Missing — period ROOFING (surfaced by E2 grounding, 2026-06-22)
| Need | Used for | Notes |
|------|----------|-------|
| **Thatch** | medieval cottage/croft roofs | the canonical peasant roof; `timber_cottage` roof is "Wood" as a PLACEHOLDER. Real thatch needs ≥45–50° pitch (now grounded in structure_styles.json). |
| **Clay tile / slate** | manor / townhouse roofs | `stone_manor` roof is "Wood" PLACEHOLDER; real clay tile at 35–45°. |
| **Wattle & daub (limewashed)** | timber-cottage exterior wall infill | currently "Wood"; real daub is a limewashed off-white panel between dark timbers. |

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
| **Rug / carpet patterns** (Persian, woven, braided) | floor rugs (`# surface: … axis=y`) | the demo `rug_test.voxel` uses `surface_test` as a stand-in |
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
| Dark / stained / polished wood | fine furniture, panelling, floors | Wood is light oak only |
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
  armchairs.
- **Book-binding material** (leather/cloth spines, reds/greens/browns/gilt) — replaces the
  Bricks/Log/Gold spine stand-ins.
- **Dark/stained wood** distinct from light Wood — for ornate/manor furniture vs plain.

## Missing — soft goods / cloth (no cloth material exists at all)
| Need | Used for |
|------|----------|
| Linen / bedsheet (white/cream) | mattress, sheets |
| Wool blanket / quilt (colored) | beds |
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
| Thatch | cottage roofs |
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
