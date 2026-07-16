# Middle-earth map → Phyxel terrain: integration notes

**Provenance / license:** all data here is decoded from the *Middle-earth* mod
(1.0.0-1.21.8-beta), which is **All Rights Reserved** and built on Tolkien IP. Use
as a **personal terrain test-bed / realism reference only** — do not ship it.

## What the importer produced

| file | what | how to sample |
|---|---|---|
| `me_height_24000.png` | 16-bit grayscale; **pixel value == Phyxel world Y** (sea @ 16, peaks ~388) | bilinear |
| `me_biome_24000.png` | 8-bit index into the legend (178 regions) | nearest |
| `me_biome_legend.json` | index → `{rgb, temperature, moisture, isOcean, isSnow}` | table lookup |
| `me_terrain_meta.json` | scale: `blocksPerImagePixel=4`, world = 96000² blocks, seaLevelY=16 | — |

**Coordinate mapping:** `imageX = worldX / blocksPerImagePixel`, `imageY = worldZ / blocksPerImagePixel`
(4 blocks per pixel at full res). Continent spans world X,Z ∈ [0, 96000).

## ✅ SHIPPED — 1:1 streaming import (implemented)

This is now built into the engine, not just a sketch:

- **`engine/{include,src}/core/MapCoarseSource.h/.cpp`** — `MapCoarseData::load(dir)` reads
  the largest `me_height_<N>.u16` (raw uint16 LE — the repo's stb_image is too old for 16-bit
  PNG, so `import_terrain.py` writes a `.u16` sidecar) + `me_terrain_meta.json`. Provides a pure,
  copy-safe `CoarseWorldModel::SourceFunc` (nearest pixel → baseHeight; normalized elevation →
  continentalness).
- **`WorldGenerator::setHeightmapSource(...)`** — swaps the Layer-0 coarse source from noise to
  the map and drops the procedural hydrology bake (the map's rivers are in the height). Shared
  immutable buffer → safe under the streaming worker's generator copy (one 1.15 GB buffer, not
  per-worker).
- **`GameDefinitionLoader`** parses a `world.heightmap.dir` block and installs it on both the
  bounded and streaming generators.
- Unit test: `tests/core/MapCoarseSourceTest.cpp` (sampling, clamp, source func, drives surface).

**game.json to run it 1:1** (real-scale 96000-block continent, streaming):
```json
"world": {
  "type": "Perlin", "seed": 3019, "streaming": true,
  "heightmap": { "dir": "<project>/terrain" },
  "loadRadius": 4, "unloadRadius": 8,
  "from": { "x": <spawnChunkX-2>, "y": 0, "z": <spawnChunkZ-2> },
  "to":   { "x": <spawnChunkX+2>, "y": 1, "z": <spawnChunkZ+2> }
}
```
Put `me_height_24000.u16` + `me_terrain_meta.json` in `<project>/terrain`. World col X = pixel X ×
4; spawn deep in the map (e.g. world 60400,50800 = plains under an eastern range). Verified: player
grounds on map-height terrain, forests decorate it, FPS ~55.

### ⚠️ Known limitation (stress-test finding)
At 1:1 the world is effectively infinite. **Do NOT crank render distance** — the mini-bake tolerated
1400 only because it was a bounded 400-chunk world. Here, a high render distance (or teleporting the
camera into a tall/forested region) asks the streamer to generate tens of thousands of chunks at once
and the engine **hard-crashes** (OOM / no exception logged; the water sim recentering over the fresh
tall terrain is also heavy). Keep render distance modest (~192–320) and move gradually. Making
continental-scale streaming robust (throttle per-frame gen volume, OOM-guard, budget the water
recenters) is the real follow-up — and the substrate for the continuous-LOD "minimap ↔ world by
scrolling" clipmap vision.

## Why this drops straight into Phyxel's two-tier model

`CoarseWorldModel` (Layer-0, `docs/TerrainGenerationV2.md`) is fed a pure
`SourceFunc(worldX, worldZ) → CoarseSample{baseHeight, temperature, moisture, continentalness}`.
The imported map supplies exactly those fields; Phyxel's existing Layer-1 `reliefAt()`
then adds high-frequency rock/ridge detail on top — the same split the mod itself uses
(its green channel = mountain amplitude, its own Perlin = detail). The header already
anticipates this as "an imported drawn map (P4)".

- `baseHeight`  ← bilinear height sample (already in Phyxel Y).
- `continentalness` ← normalized height `(Y-seaLevel)/(peak-seaLevel)`; drives how much
  Layer-1 relief Phyxel layers on (bright green ridges → tall, detailed mountains).
- `temperature`/`moisture` ← from the biome legend at that pixel → keeps Phyxel biome
  selection (`biomes.json`) keyed to the real region (Shire temperate, Harad hot-dry,
  Forodwaith frozen, Mordor hot-arid).

## C++ SourceFunc sketch (drop-in for WorldGenerator::configure…)

```cpp
// MapCoarseSource.h — loads the imported PNGs once, returns a pure SourceFunc.
// Keep the loaded buffers in a shared_ptr captured BY VALUE so the streaming
// worker's generator copy stays safe (see CoarseWorldModel threading contract).
struct MapData {
    int w = 0, h = 0;                 // image dims (24000)
    float blocksPerPixel = 4.0f;      // meta.blocksPerImagePixel
    float seaLevel = 16.0f, peak = 388.0f;
    std::vector<uint16_t> height;     // w*h, value = world Y
    std::vector<uint8_t>  biome;      // w*h, legend index
    std::vector<float>    temp, moist;// per legend index (size 178)

    float sampleHeight(float px, float pz) const {          // bilinear
        px = std::clamp(px, 0.f, w - 1.001f); pz = std::clamp(pz, 0.f, h - 1.001f);
        int x0 = (int)px, z0 = (int)pz; float fx = px - x0, fz = pz - z0;
        auto H = [&](int x, int z){ return (float)height[(size_t)z * w + x]; };
        float a = H(x0, z0), b = H(x0+1, z0), c = H(x0, z0+1), d = H(x0+1, z0+1);
        return (a*(1-fx)+b*fx)*(1-fz) + (c*(1-fx)+d*fx)*fz;
    }
    int biomeAt(int px, int pz) const {
        px = std::clamp(px, 0, w-1); pz = std::clamp(pz, 0, h-1);
        return biome[(size_t)pz * w + px];
    }
};

CoarseWorldModel::SourceFunc makeMapSource(std::shared_ptr<const MapData> m) {
    return [m](float worldX, float worldZ) {
        CoarseSample cs;
        const float px = worldX / m->blocksPerPixel, pz = worldZ / m->blocksPerPixel;
        cs.baseHeight = m->sampleHeight(px, pz);
        cs.continentalness = std::clamp(
            (cs.baseHeight - m->seaLevel) / (m->peak - m->seaLevel), 0.f, 1.f);
        const int bi = m->biomeAt((int)std::lround(px), (int)std::lround(pz));
        cs.temperature = m->temp[bi];
        cs.moisture    = m->moist[bi];
        return cs;
    };
}
// then: m_coarse = std::make_shared<CoarseWorldModel>(makeMapSource(mapData), 4.0f /*cellSize≈blocksPerPixel*/);
```

## Tuning knobs

- **Peak height** — rerun the importer with `--height-scale H` (green 255 → seaLevel+H).
  Default 420 → peaks ~388 above sea, matching Phyxel's grandest-peak budget.
- **Playable crop** — 96 km is huge. To walk a region, crop the PNGs (e.g. Eriador =
  image rows/cols ~[300:1400]) and offset world origin; the engine only needs the
  crop. A crop keeps load light and avoids sampling a 24k image at runtime.
- **Layer-1 detail** — the map is smooth (4 blocks/pixel); let Phyxel's `reliefAt()`
  supply crags. If ridges read too soft, raise the relief amplitude where
  `continentalness` is high rather than sharpening the map.
- **Rivers** — the mod etches river channels into the height (thin dark lines). They
  survive into `baseHeight`, so Phyxel hydrology can pick them up, or you can rely on
  the coarse channels directly.

## Open items / caveats

- One region near NW is painted max-bright (green 227 → Y 388); it reads as a single
  very high massif. Faithful to source; clamp per-region if undesirable.
- Ocean floor is a flat `seaLevel - oceanDepth`; the mod encodes real bathymetry in
  blue/red we currently ignore. Add it later if underwater terrain matters.
- Biome climate (temp/moisture) is a hue heuristic, not the mod's authored biome
  climate. Good enough for selection; refine by mapping legend colors to the mod's
  269 `worldgen/biome` defs if you want exact biome names.
```
