# ProvingGrounds — the showcase / regression world

**Built 2026-08-01.** One streaming terrain-v2 world that exercises every distance-scaling system at
once, set up to be **measured** rather than admired. Project:
`Documents/PhyxelProjects/ProvingGrounds`. Definition: `samples/game_definitions/proving_grounds.json`
(the project's own `game.json` is the live copy). Harness: `tools/proving_grounds_probe.py`.

## What's in it

| Feature | Source | Status |
|---|---|---|
| Rolling hills, valleys, ridges | terrain-v2 `Perlin` + streaming (CoarseWorldModel) | ✅ |
| Lake + shoreline + shallows | hydrology bake (requires `streaming: true`) | ✅ |
| Large grass fields | biome `Plains` + the retuned grass layer (70 blades / 192u) | ✅ |
| Conifer forest | biome flora, `pool` stamps | ✅ visible at range; see caveat |
| Flora / fauna | biome flora + `FaunaSpawner` (streaming-only) | ✅ 6 wandering fauna |
| Village on rolling hills | **`POST /api/settlement/build`** | ✅ 8 buildings, 8 residents |
| Far terrain to the horizon | `FarTerrainManager`, 4096u, 4 rings | ✅ 55–96 tiles/pose |
| Far-LOD chunks (structures at range) | C3.3, persisted pyramids | ✅ proven, see below |

**Village provenance** (CLAUDE.md hard rule — the engine generated this, nothing was hand-placed):

```
POST /api/settlement/build
{"era":"medieval","tier":"village","seed":7,
 "position":{"x":-45,"y":51,"z":-35},"width":90,"depth":70,"terrain":true}
```

Returned `morphology: main_street`, 8 plots / 8 buildings / 1 street, `dropped_plots: 0`, typologies
longhouse · croft · tavern · hall_house ×3 · blacksmith · croft, styles stone_keep / stone_manor /
timber_cottage. The generator also populated it: `res_blacksmith`, `res_croft`, `res_tavern`,
`res_longhouse`, `res_hall_house` NPCs, all `[Scheduled]`.

## The measurement harness

```bash
python tools/proving_grounds_probe.py --measure \
  --game-json <project>/game.json --settle 22 --out docs/evidence/pg_<date>.jsonl
```

Six poses pinned in `game.json` → `testVantages`. **Treat them like WaterLab's camera: changing one
silently invalidates every archived measurement taken from it.** The harness reads the pose back
from `/api/camera` rather than trusting the pose it asked for, because a silently-rejected camera
move is the classic way to compare two measurements of the same frame.

### Baseline, 2026-08-01 (Release) — `docs/evidence/pg_baseline_20260801.jsonl`

| vantage | chunks | faces | farTiles | farTris | farChunk | farInst |
|---|--:|--:|--:|--:|--:|--:|
| village_street | 96 | 245,377 | 57 | 109,390 | 0 | 0 |
| village_overlook | 143 | 274,699 | 55 | 105,740 | 0 | 0 |
| grass_field | 72 | 305,873 | 61 | 133,102 | 0 | 0 |
| lake_and_hills | 46 | 305,188 | 58 | 89,332 | 0 | 0 |
| horizon_far | 85 | 321,318 | 78 | 173,352 | 0 | 0 |
| **village_from_afar** | 39 | 116,418 | 96 | 240,014 | **8** | **2,382** |

Observed FPS at these poses: 98–158 near-field, 120 at `village_from_afar`.

**The row that matters is the last one.** At 700 units the village sits far outside `unloadRadius`
(320u), so it is not resident — the 8 far chunks / 2,382 instances are drawn from `chunk_lod_blobs`.
And `farChunk` is exactly **0 at every near vantage**, which is the eviction invariant (a chunk is
never both resident and far-drawn) holding as a measured fact rather than an assumption. This is the
first time the C3.3 tier has been exercised on content produced by the engine's own generators.

⚠️ **`cpu_frame_ms` is not the presented frame rate.** This project documents ±20% restart variance
on the FPS counter and treats the status bar as the real rate; the column is a coarse regression
signal only. Read FPS off the screenshots for anything load-bearing.

## Setup gotchas paid for in this build

1. **`streaming: true` is not a perf setting here — it is load-bearing.** It is the only path that
   runs the hydrology bake (no lakes without it), enables fauna, and streams chunks at the *real*
   surface. A static `from`/`to` range generates deep underground stone and renders as a flat grey
   plane.
2. **The world recipe is persisted into `world.db` on FIRST boot and then WINS over `game.json`**
   (the loader warns and keeps the stored value). Changing `seed`, `params` or `water.seaLevel`
   after that does nothing until the DB is deleted. Delete **`default.db`, `-wal` AND `-shm`** — a
   `.db` removed without its WAL sibling leaves a valid-looking, stale database.
3. **Sea level must be measured, not assumed.** The first attempt used `seaLevel: 62` against a
   surface at y=51 and drowned the entire region in ocean. `kSeaLevelY = 16` is what terrain-v2
   generates against.
4. **The legacy Perlin knobs fight terrain-v2.** `heightScale` / `octaves` / `persistence` are the
   *old* amplitude controls; terrain-v2's relief comes from `CoarseWorldModel`
   (continentalness → height spline → ridged multifractal). Setting them low flattened the world
   into a coastal plain. The world block now carries only `climateFrequency`.
5. **`climateFrequency` is the biome-size knob and it decides whether the world reads as varied.**
   At 0.0022 (~455u biomes) a single biome filled the entire view and there was no forest anywhere
   near spawn; 0.004 (~250u) puts forest and open field in the same frame.
6. **`type: "Mountains"` puts spawn in the alpine band** (surface y≈191, snow, above the treeline).
   `Perlin` lands in the temperate band (y≈31–77) with ridges on the horizon — which is what
   "rolling hills with mountains beyond" actually wants.
7. **API route names**: `/api/render/stats` (not `render_stats`), `/api/world/terrain_height` (not
   `terrain/height`), `/api/screenshot` is a **GET**. The wrong path returns an empty body that
   json-decodes to nothing and fills a whole report with `None` — which reads as "the engine has no
   geometry" rather than "you used the wrong URL". Far-**chunk** counters
   (`far_chunks`/`far_instances`) are on `/api/debug/far_lod`, **not** in render stats: reporting
   only the far-*terrain* numbers would hide a completely dead structure tier.
8. **A high camera reads as an empty world.** Streaming clamps the vertical band to ±2 chunk bands
   around the camera, so probing terrain from y=420 returns "no terrain found" everywhere. Far
   terrain still draws, which makes it the right tool for scouting an unknown world.

## Known gaps

- **Forest density near spawn is Plains-grade** (0.35 density / 9 spacing) rather than the Forest
  biome's 0.85/6. Dense conifer stands are visible ~700u out but the immediate spawn basin is
  parkland. Fixable by relocating the pinned vantages onto a Forest cell or nudging
  `climateFrequency` again — not by editing global `biomes.json`, which would leak into every new
  world.
- **Mountains read as ridges, not peaks.** terrain-v2 can reach ~384 above sea level, but this
  seed's spawn basin is lowland. A dedicated `mountain_vista` vantage needs a scouted high cell.
- No dedicated **destruction / physics** vantage yet; the world is a rendering fixture so far.
