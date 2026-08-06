# Fine-Voxel Item Asset Class

**Status: SHIPPED 2026-08-06** (red-before-green; 16-test L2 suite `FineVoxelItemTest.cpp`;
L4-verified live — world props + in-hand). The item tier that ends "the axe looks like a
blocky plank": items (weapons, tools, books, small props) are authored on a **fine grid
finer than microcubes**, matching how characters get arbitrary-size boxes while chunks
stay on the C/S/M ladder.

## The format

```
# grid: 81                  <- cells per cube edge; 27 or 81 only (9*3^k)
# category: item
V x y z Material [tint=#rrggbb] [state=<name>]
```

- `V` lines are **template-local min-corner cell coords** on the declared lattice.
  Grid 81 → 1 cell = 1/81 unit ≈ **1.23 cm** (3× finer than a microcube per axis).
- **One lattice per file**: mixing `V` with `C/S/M`, a `V` before `# grid`, or an
  invalid grid value **rejects the whole template** (`loadTemplate` returns false,
  loud `REJECTED` log). No half-loaded silent failures.
- Tints do the fine color work (leather, steel edge, gilt, glow); materials carry
  physics + texture. ⚠️ **Steel/iron tints must run bright** (near-white): tint
  MULTIPLIES the already-dark Metal albedo — mid-gray tints render near-black
  (verified live).

## Kinematic-only — by design

Fine templates **never bake into chunks**. `ObjectTemplateManager::canBakeStatic()`
is the gate; `spawnTemplate` / `spawnTemplateMicro` / `spawnTemplateSequentially`
refuse fine templates loudly (the chunk store bottoms out at the 9-per-cube micro
grid). Spawn them as **item props** (`spawn_item` MCP / `ItemPropManager::spawnProp`)
or hold them (`items.json` `held` block). This is the same split as characters:
props/held items are kinematic objects with free `vec3` scales.

## Engine path (what was built)

1. **Parse** — `ObjectTemplateManager::parseLine` (`# grid:` header + `V` lines →
   `VoxelTemplate::fineVoxels` / `fineGridResolution`; contract violations set
   `parseError` and the template is rejected).
2. **Greedy box merge** — `ItemPropManager::voxelsFromTemplate` →
   `mergeFineVoxels()`: same-appearance cells merge X→Y→Z (deterministic) into
   arbitrary-scale `KinematicVoxel` boxes, **never crossing a cube boundary**
   (keeps sub-tile UVs in [0,1]). Measured: maul 4102 cells → **21 boxes/80
   faces**; longsword 931 → 37/144; all 18 shipped items ≈ **2.3k faces total**
   (<2% of the 131k kinematic budget).
3. **Scale-agnostic culling** — `KinematicVoxelManager::buildFaces` lattice is now
   the **per-object GCD of all voxel extents in 1/81 units** (legacy objects
   resolve to exactly the historical 1/9 — pinned by control tests; fine objects
   to their grid cell), with **per-axis spans** and per-axis sub-tile UV rects for
   non-cubic merged boxes. The renderer needed zero changes
   (`kinematic_voxel.vert` always took free `vec3` scale).

## Tooling

- **`tools/gen_items.py`** — deterministic generator (no LLM) for the item library:
  18 assets (swords long/short, dagger, hand/battle axe, spear, mace, maul,
  warhammer, 4 school staffs, wand, tome, scroll, candlestick, potion). Writes
  templates + `items_manifest.json` (dims + **grip points** for `items.json`
  `held.gripOffset` = −grip_point) + registers `template_catalog.json`.
- **`tools/vox_import.py`** — now auto-detects **MagicaVoxel RIFF** `.vox` (the CC0
  pack standard; Z-up-UP, RGBA index off-by-one handled) alongside Barony's flat
  format; `--scale fine --grid 81` emits this format directly with per-voxel
  `tint=`. CC0 .vox weapon packs can be ingested at full fidelity.
- **`tools/lint_voxel_detail.py`** — `V` lines satisfy the handtool detail rule.
- MCP `inspect_template` reports `V` counts + grid.

## Conventions (match `gen_items.py`)

- +Y up, origin at grip/base, min corner ≥ 0 after normalization.
- Wieldables mark a grip point; `items.json` `held.gripOffset` = **negated** grip
  point; `held.scale` stays 1.0 (items are authored at true world scale).
- Blades lie in the XY plane (thickness along Z) matching character +Z facing.

## Tests

`tests/core/FineVoxelItemTest.cpp` — parse contract (5), merge exact-tiling/
determinism (3), fine culling incl. 1/81 + non-cubic slabs (4), **pinned legacy
controls** (2: stacked cubes = 10 faces, micro-on-cube = 11 — these must never
move), static-bake refusal (1). All red before implementation, all green after;
full suite regression-checked.
