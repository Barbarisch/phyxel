#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/CoarseWorldModel.h"

namespace Phyxel {

// ── Layer-0 "imported drawn map" coarse source (docs/TerrainGenerationV2.md P4) ──
//
// Immutable, thread-safe backing for a heightmap imported by
// tools/middle_earth/import_terrain.py (me_height_<N>.png + me_terrain_meta.json).
// A 16-bit grayscale image whose pixel value IS the Phyxel world Y; each pixel spans
// `blocksPerPixel` world blocks (worldSizeBlocks / imageWidth), so the whole continent
// maps 1:1 (4 blocks/pixel at the 24000² full-res export → a 96000-block world).
//
// Held by shared_ptr and captured BY VALUE into a pure CoarseWorldModel::SourceFunc,
// so the streaming worker's generator copy shares the same immutable buffer safely
// (same contract as m_coarse — see CoarseWorldModel threading note). Never mutated
// after load.
struct MapCoarseData {
    int   widthPx = 0, heightPx = 0;    // height image dimensions (pixels)
    float blocksPerPixel = 4.0f;        // world blocks per map pixel
    float seaLevelY = 16.0f;            // Phyxel sea level (kSeaLevelY)
    float worldSizeBlocks = 0.0f;       // widthPx * blocksPerPixel (continent extent)
    float minY = 0.0f, maxY = 0.0f;     // decoded height range (world Y)
    std::vector<uint16_t> height;       // widthPx*heightPx row-major; value == world Y

    // Nearest-pixel world Y (clamped to the map edge outside the continent). Used by the
    // coarse SourceFunc — CoarseWorldModel bilinearly interpolates between pixel corners,
    // so nearest here + its lerp == a smooth bilinear field (single interpolation).
    float heightAtPixelClamped(float wx, float wz) const;

    // Bilinear world Y at an arbitrary column (clamped). For direct queries/tests.
    float sampleHeightWorld(float wx, float wz) const;

    // Load from an import_terrain.py output dir: the largest me_height_*.png plus
    // me_terrain_meta.json (for worldSizeBlocks + seaLevelY). Returns nullptr + sets `err`
    // on failure (missing dir/png/meta).
    static std::shared_ptr<MapCoarseData> load(const std::string& terrainDir, std::string& err);
};

// A pure coarse source over the map: baseHeight = nearest map Y, continentalness =
// normalized elevation (drives Layer-1 relief detail + the biome continentalness axis).
// temperature/moisture are left at defaults — sampleColumn still derives climate from
// noise for now (biome-map climate is a follow-up). Captures `data` by value.
CoarseWorldModel::SourceFunc makeMapCoarseSource(std::shared_ptr<const MapCoarseData> data);

} // namespace Phyxel
