# tools/middle_earth — Middle-earth mod → Phyxel (read-only reference)

Three read-only tools that decode content from the **Middle-earth** Minecraft mod
(Modrinth: `middle-earth`, by Jukoz / Seven Stars) for use as a Phyxel design
reference and terrain test-bed.

> ⚠️ **License.** The mod is **All Rights Reserved** and built on Tolkien IP. These
> tools *read* the jar and emit derived data for **personal study / test-beds only**.
> Do **not** commit the derived output (reports, PNGs, world.db) or ship any of it.
> The `.gitignore` here keeps generated artifacts out of the repo — keep it that way.
> Everything each tool produces states its provenance; terrain baked here is an
> **imported heightmap, NOT Phyxel's procedural generator** output.

## 1. `analyze_structures.py` — building floorplan/material reference
Decodes all ~356 `.nbt` structure templates into design reference.
```bash
python analyze_structures.py --jar path/to/Middle-earth-*.jar --out me_structures
```
Output: `_SUMMARY.md` (master table: size/blocks/storeys/doors/windows/materials),
`_index.json` (machine-readable), and one `<faction>__<name>.txt` per structure with
per-floor ASCII floorplans (`#` wall `D` door `o` window `/` stair `f` furniture
`*` light `.` floor). Use to study room kits + proportions for the structure generator.

## 2. `import_terrain.py` — Middle-earth heightmap → Phyxel Layer-0 fields
Assembles the mod's 24000² map pyramid (4 blocks/px → 96 km continent) and decodes it
into a sampleable heightfield + biome map for `CoarseWorldModel`.
```bash
python import_terrain.py --jar path/to/Middle-earth-*.jar --out me_terrain --downsample 1
#   --downsample 8  => fast 3000² check     --height-scale H => peak height tuning
```
Output: `me_height_<N>.png` (16-bit, value = Phyxel world Y), `me_biome_<N>.png` +
`me_biome_legend.json` (178 regions + climate), `me_terrain_meta.json`, `preview_*`.
Ground truth (javap): PIXEL_WEIGHT=4, REGION_SIZE=3000, MAP_ITERATION=3; green channel
= relief, blue/red = base+water. See **`INTEGRATION.md`** (checked in here) for the C++
`CoarseWorldModel::SourceFunc` sketch that wires the output into the real generator.

## 3. `bake_test_world.py` — viewable relief world.db
Downsamples the imported terrain and bakes a surface voxel shell into a Phyxel
`world.db` (legacy sparse `cubes` table) so you can load and fly over Middle-earth.
```bash
python bake_test_world.py --terrain-dir me_terrain --out MyProj/worlds/default.db \
    --size 512 --vscale 0.14 --smooth 2
```
Materials by height/biome (Ice sea, Sand shore/Harad, Grass/GrassForest/GrassSavanna,
Stone, Snow caps). Then point a project at it and launch.

## Viewing gotcha (important)
The runtime **default render distance is only 192** — a whole-continent overview is
frustum-culled to sky until you raise it:
```bash
curl -X POST http://localhost:8090/api/debug/render_distance -d '{"distance":1400}'
```
Then a free-cam at ~Y=700 pitch −84 over the world centre shows the full map. Avoid
pitch −90 (gimbal flip → sky). A `--project` world's `game.json` camera is overridden
by player-follow on load; set the overview with `set_camera mode=free` after boot.
