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
            for (int s = 0; s < 3; ++s)
                c->addSubcube(glm::ivec3(16 + dx, 12, 16 + dz), glm::ivec3(s, 1, 1), "Leaf");
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

// THE POINT: a stashed chunk serves faces identical to the live cut at every far level, so an
// evicted tree degrades into the same coarse mass a saved chunk would have shown.
TEST(EvictedLodCacheTest, StashedChunkServesFacesMatchingTheLiveCut) {
    auto c = chunkWithTree(glm::ivec3(64, 0, -32));
    EvictedLodCache cache;
    cache.stash(*c);

    const glm::ivec3 coord = c->getWorldOrigin() / 32;
    ASSERT_TRUE(cache.contains(coord)) << "eviction stash dropped a sub/microcube chunk";

    for (int lod = 1; lod <= LodPyramidService::kMaxLevel; ++lod) {
        std::vector<InstanceData> fromCache, fromLiveCut;
        ASSERT_TRUE(cache.facesForLevel(coord, lod, fromCache)) << "no faces at lod " << lod;
        LodChunkMesh::buildForLevel(*c, lod, SquashConfig{}, fromLiveCut);
        EXPECT_TRUE(sameFaces(fromCache, fromLiveCut))
            << "cache-served faces diverge from the live cut at lod " << lod << " ("
            << fromCache.size() << " vs " << fromLiveCut.size() << ")";
        EXPECT_FALSE(fromCache.empty());
    }
}

TEST(EvictedLodCacheTest, PureCubeTerrainIsNotCached) {
    auto c = pureCubeTerrain(glm::ivec3(0));
    EvictedLodCache cache;
    cache.stash(*c);
    EXPECT_FALSE(cache.contains(glm::ivec3(0)));
    EXPECT_EQ(cache.chunkCount(), 0u);
}

// Mirrors refreshPyramid's delete-then-rebuild: if the chunk's detail is gone by the next
// eviction (structure demolished, tree felled), the stale entry must go with it.
TEST(EvictedLodCacheTest, RestashWithoutDetailErasesTheStaleEntry) {
    EvictedLodCache cache;
    auto withTree = chunkWithTree(glm::ivec3(0));
    cache.stash(*withTree);
    ASSERT_TRUE(cache.contains(glm::ivec3(0)));

    auto bare = pureCubeTerrain(glm::ivec3(0));
    cache.stash(*bare);
    EXPECT_FALSE(cache.contains(glm::ivec3(0)))
        << "a demolished structure keeps rendering at distance";
}

TEST(EvictedLodCacheTest, LruCapEvictsTheColdestEntry) {
    EvictedLodCache cache;
    cache.setCapacity(2);
    auto a = chunkWithTree(glm::ivec3(0, 0, 0));
    auto b = chunkWithTree(glm::ivec3(32, 0, 0));
    auto c = chunkWithTree(glm::ivec3(64, 0, 0));
    cache.stash(*a);
    cache.stash(*b);

    // Touch A so B is the coldest when C forces an eviction.
    std::vector<InstanceData> faces;
    ASSERT_TRUE(cache.facesForLevel(glm::ivec3(0), 2, faces));
    cache.stash(*c);

    EXPECT_EQ(cache.chunkCount(), 2u);
    EXPECT_TRUE(cache.contains(glm::ivec3(0)));
    EXPECT_FALSE(cache.contains(glm::ivec3(1, 0, 0)));
    EXPECT_TRUE(cache.contains(glm::ivec3(2, 0, 0)));
}

// The far-LOD tier rescans its candidates when the revision moves. Without this, a chunk
// evicted while the camera is stationary is never picked up (the rescan is otherwise keyed on
// camera chunk crossings — the documented stationary-A/B blind spot).
TEST(EvictedLodCacheTest, RevisionBumpsOnInsertAndErase) {
    EvictedLodCache cache;
    const uint64_t r0 = cache.revision();
    auto c = chunkWithTree(glm::ivec3(0));
    cache.stash(*c);
    const uint64_t r1 = cache.revision();
    EXPECT_GT(r1, r0);
    auto bare = pureCubeTerrain(glm::ivec3(0));
    cache.stash(*bare);   // erases
    EXPECT_GT(cache.revision(), r1);
}

TEST(EvictedLodCacheTest, TracksBlobBytes) {
    EvictedLodCache cache;
    auto c = chunkWithTree(glm::ivec3(0));
    cache.stash(*c);
    ASSERT_TRUE(cache.contains(glm::ivec3(0)));
    EXPECT_GT(cache.totalBytes(), 0u);
    cache.clear();
    EXPECT_EQ(cache.totalBytes(), 0u);
    EXPECT_EQ(cache.chunkCount(), 0u);
}

// ---- Integration: the streaming pump actually hands evicted chunks to the cache -----------

TEST(EvictedLodCacheTest, EvictionPumpStashesWhileTheChunkIsStillAlive) {
    std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash> chunkMap;
    std::vector<std::unique_ptr<Chunk>> chunks;

    ChunkStreamingManager csm;
    csm.setCallbacks(
        [](const glm::ivec3&) {},
        [&]() -> std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash>& { return chunkMap; },
        [&]() -> std::vector<std::unique_ptr<Chunk>>& { return chunks; },
        []() { return std::make_pair(VkDevice(nullptr), VkPhysicalDevice(nullptr)); });

    EvictedLodCache cache;
    csm.setOnChunkEvicted([&](Chunk& chunk) { cache.stash(chunk); });

    auto c = chunkWithTree(glm::ivec3(0));
    chunkMap[glm::ivec3(0)] = c.get();
    chunks.push_back(std::move(c));

    // Camera far away → the chunk must evict, and the stash must have happened while the
    // chunk's voxels were readable (a use-after-move here crashes, which is the test).
    csm.unloadDistantChunks(glm::vec3(10000.0f, 0.0f, 0.0f), 352.0f);

    EXPECT_TRUE(chunks.empty()) << "chunk was not evicted";
    ASSERT_TRUE(cache.contains(glm::ivec3(0)));
    std::vector<InstanceData> faces;
    EXPECT_TRUE(cache.facesForLevel(glm::ivec3(0), 2, faces));
    EXPECT_FALSE(faces.empty());
}
