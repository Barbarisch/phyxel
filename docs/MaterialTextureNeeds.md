# Material & Texture Needs

> **Standing directive:** whenever a structure/furniture/prop wants a material or texture we
> don't have, record it HERE. The user plans to overhaul the texture system for more detailed,
> better-looking results (explicitly *not* Minecraft-style flat blocks), so this list feeds that.

## Current materials (19, from `resources/materials.json`)
Default, Dirt, Grass, Stone, Cobblestone, StoneBricks, Sand, Gravel, Wood, Log, Bricks,
Sandstone, Glass, Metal, Gold, Ice, Leaf, glow, Mirror.

These skew **terrain/exterior**. Interiors, furniture, and finish work are starved — we keep
substituting (e.g. bed mattress = Sandstone, pillow = Sand) because nothing better exists.

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

## Missing — soft goods / cloth (no cloth material exists at all)
| Need | Used for |
|------|----------|
| Linen / bedsheet (white/cream) | mattress, sheets |
| Wool blanket / quilt (colored) | beds |
| Cushion / upholstery (leather, velvet) | chairs, sofas, padded furniture |
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

See also: `docs/StructurePipelineGaps.md` (asset backlog: furniture, door types, lamps+lighting).
