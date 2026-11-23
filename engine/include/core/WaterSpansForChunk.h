#pragma once
// docs/VoxelFoundationPlan.md F7.18 (step-7 port 1): one chunk's generation-time water spans, a pure function of the
// chunk coordinate -- the generator's memoized column flood clipped to the chunk's y range, bridge piers displacing
// their columns. WorldGenerator::generateChunk stores these on today's Chunk; the switched world fills ChunkManager's
// span store from the same function. Empty without hydrology.

#include <vector>

#include <glm/glm.hpp>

#include "core/Chunk.h"
#include "core/WorldGenerator.h"

namespace Phyxel {

std::vector<Chunk::WaterSpanLocal> waterSpansForChunk(WorldGenerator& gen, const glm::ivec3& chunkCoord);

}  // namespace Phyxel
