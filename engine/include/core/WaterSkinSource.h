#pragma once

#include "core/water/WaterCore.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// WaterCore §51.4 S3 step 2: the far skin's source for a chunk column whose chunks were never loaded - the water the GENERATOR
// would give them (as far terrain samples the pristine generator), so a far lake does not depend on which chunks happened to load.
// It must equal what the generated chunks store (WorldGenerator::generateChunk's water spans) - pinned by
// WaterSkinGeneratorTest.TheGeneratorSummaryEqualsTheGeneratedChunks - or a chunk's load would change the skin there.
namespace Phyxel {

class WorldGenerator;

/// The runs (world Y) of chunk column `colChunk`'s 32 x 32 columns, as its generated chunks would hold them before any edit: the
/// generator's per-chunk-column spans (waterSpansForChunkColumn), skipping a bridge pier's columns (generateChunk skips them too).
/// Empty without hydrology. Not thread-safe on a shared generator (it memoizes): give a worker its own copy.
void generatedWaterRuns(WorldGenerator& gen, const glm::ivec2& colChunk, std::vector<Core::Water::ColumnRuns>& out);

/// 51.16: BUMP THIS with any change to what generatedWaterRuns returns for a given recipe (the hydrology bake, the carve, the river
/// stage, the spans...). The far skin's per-world summary cache is keyed by it: a change without the bump leaves cached summaries of
/// the OLD water, caught only where the load's verification samples (WaterSkin::kGenCacheVerify columns).
constexpr uint32_t kGeneratedWaterVersion = 1;   // 1 = 51.14 (level rivers) as of 2026-11-13
/// The cache key: FNV-1a 64 of the generator's own recipe JSON (makeRecipe: seed, type, flags, biomes...), kGeneratedWaterVersion
/// and the summary file format.
uint64_t generatedWaterCacheKey(const WorldGenerator& gen);

}  // namespace Phyxel
