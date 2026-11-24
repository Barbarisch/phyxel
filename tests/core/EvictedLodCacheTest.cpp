#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "core/Chunk.h"
#include "core/ChunkStreamingManager.h"
#include "core/EvictedLodCache.h"
#include "core/LodChunkMesh.h"
#include "core/LodPyramidService.h"

using namespace Phyxel;
using namespace Phyxel::Core;

// World-look A1/A2 (docs/WorldLookBacklog.md): trees and structures VANISH past the unload
// radius because streaming worlds never save pristine generated chunks, and the far-LOD tier
// only serves chunks with persisted pyramids. These tests pin the in-memory handoff that closes
// the gap: eviction stashes a coarse LOD, and the cache serves the SAME faces the persisted
// path would have.

namespace {

/// Generated terrain plus a tree-like sub/microcube carrier — the exact content class that
/// used to vanish (flora templates stamp as subcubes; structures carry micro detail).
std::unique_ptr<Chunk> chunkWithTree(const glm::ivec3& origin = glm::ivec3(0)) {
    auto c = std::make_unique<Chunk>(origin);
    c->initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 6; ++y)
                c->addCube(glm::ivec3(x, y, z), (y == 5) ? "Grass" : "Dirt");
    // Trunk (whole cubes) + canopy detail (subcubes), like a stamped flora template.
    for (int y = 6; y < 12; ++y) c->addCube(glm::ivec3(16, y, 16), "Log");
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            c->addCube(glm::ivec3(16 + dx, 12, 16 + dz), "Leaf");   // (was subcube canopy; D3c-5)
    return c;
}

/// Pure whole-cube terrain: far terrain's job, must NOT be cached.
std::unique_ptr<Chunk> pureCubeTerrain(const glm::ivec3& origin = glm::ivec3(0)) {
    auto c = std::make_unique<Chunk>(origin);
    c->initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 5; ++y)
                c->addCube(glm::ivec3(x, y, z), "Stone");
    return c;
}

bool sameFaces(const std::vector<InstanceData>& a, const std::vector<InstanceData>& b) {
    if (a.size() != b.size()) return false;
    return a.empty() ||
           std::memcmp(a.data(), b.data(), a.size() * sizeof(InstanceData)) == 0;
}

} // namespace

TEST(EvictedLodCacheTest, PureCubeTerrainIsNotCached) {
    auto c = pureCubeTerrain(glm::ivec3(0));
    EvictedLodCache cache;
    cache.stash(*c);
    EXPECT_FALSE(cache.contains(glm::ivec3(0)));
    EXPECT_EQ(cache.chunkCount(), 0u);
}

// ---- Integration: the streaming pump actually hands evicted chunks to the cache -----------
