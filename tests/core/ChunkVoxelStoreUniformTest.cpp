#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/Chunk.h"
#include "core/ChunkBlobCodec.h"
#include "core/ChunkVoxelStore.h"

using namespace Phyxel;

// ── Phase 4.4 stage 1: UNIFORM store representation (docs/LargeWorldScalePlan.md) ──
//
// ~4 of 5 resident chunks on the 1:1 benchmark are uniform (fully-buried solid or pure sky),
// yet the store always allocates its dense 96 KB index/state arrays. These tests define the
// uniform representation: a store is born uniform-air, can be filled uniform-solid in O(1),
// answers every query from the uniform fast path, and allocates the dense arrays only on the
// first NON-CONFORMING write (the "split"). Written RED against the always-dense 4.2b store —
// the approxBytes assertions are the red line.

namespace {
size_t idxOf(int x, int y, int z) { return static_cast<size_t>(z) + y * 32 + x * 1024; }
}

// A fresh (all-air) store must not pay the dense arrays.
TEST(ChunkVoxelStoreUniform, EmptyStoreIsTiny) {
    ChunkVoxelStore s;
    EXPECT_EQ(s.solidCount(), 0u);
    EXPECT_FALSE(s.solid(0));
    EXPECT_LT(s.approxBytes(), 1024u) << "uniform-air store must not allocate dense arrays";
}

// fillUniform is the buried-chunk representation: full presence, one palette entry, ~no bytes.
TEST(ChunkVoxelStoreUniform, FillUniformIsCheapAndAnswersQueries) {
    ChunkVoxelStore s;
    s.fillUniform("Stone", /*visible=*/true);
    EXPECT_EQ(s.solidCount(), ChunkVoxelStore::kVoxels);
    EXPECT_EQ(s.paletteSize(), 1u);
    EXPECT_TRUE(s.solid(idxOf(0, 0, 0)));
    EXPECT_TRUE(s.solid(idxOf(31, 31, 31)));
    EXPECT_TRUE(s.visible(idxOf(15, 20, 7)));
    EXPECT_EQ(s.material(idxOf(3, 4, 5)), "Stone");
    EXPECT_FALSE(s.solid(ChunkVoxelStore::kVoxels + 5));   // out of range still safe
    EXPECT_LT(s.approxBytes(), 1024u) << "uniform-solid store must not allocate dense arrays";
}

// Writing a voxel that MATCHES the uniform state must not split.
TEST(ChunkVoxelStoreUniform, ConformingWriteKeepsUniform) {
    ChunkVoxelStore s;
    s.fillUniform("Stone", true);
    s.set(idxOf(5, 5, 5), "Stone", true);   // identical to the uniform value
    EXPECT_LT(s.approxBytes(), 1024u) << "a conforming write must not allocate dense arrays";
    EXPECT_EQ(s.solidCount(), ChunkVoxelStore::kVoxels);
}

// The split: the first non-conforming write materializes the dense arrays and preserves every
// other voxel exactly.
TEST(ChunkVoxelStoreUniform, EraseSplitsAndPreservesEverythingElse) {
    ChunkVoxelStore s;
    s.fillUniform("Stone", true);
    s.erase(idxOf(10, 10, 10));

    EXPECT_FALSE(s.solid(idxOf(10, 10, 10)));
    EXPECT_EQ(s.solidCount(), ChunkVoxelStore::kVoxels - 1);
    // Exhaustive: every other voxel must still be Stone/visible.
    for (size_t i = 0; i < ChunkVoxelStore::kVoxels; ++i) {
        if (i == idxOf(10, 10, 10)) continue;
        ASSERT_TRUE(s.solid(i)) << "voxel lost at " << i;
        ASSERT_TRUE(s.visible(i));
        ASSERT_EQ(s.material(i), "Stone");
    }
}

TEST(ChunkVoxelStoreUniform, MaterialChangeSplits) {
    ChunkVoxelStore s;
    s.fillUniform("Stone", true);
    s.set(idxOf(0, 0, 0), "Gold", true);
    EXPECT_EQ(s.material(idxOf(0, 0, 0)), "Gold");
    EXPECT_EQ(s.material(idxOf(0, 0, 1)), "Stone");
    EXPECT_EQ(s.solidCount(), ChunkVoxelStore::kVoxels);
    EXPECT_EQ(s.paletteSize(), 2u);
}

TEST(ChunkVoxelStoreUniform, SetVisibleSplitsUniformSolid) {
    ChunkVoxelStore s;
    s.fillUniform("Stone", true);
    s.setVisible(idxOf(1, 2, 3), false);
    EXPECT_FALSE(s.visible(idxOf(1, 2, 3)));
    EXPECT_TRUE(s.solid(idxOf(1, 2, 3)));      // still solid, just hidden
    EXPECT_TRUE(s.visible(idxOf(1, 2, 4)));
}

// Writing into a uniform-AIR store splits too (this is every ordinary chunk's first addCube).
TEST(ChunkVoxelStoreUniform, SetOnAirSplits) {
    ChunkVoxelStore s;
    s.set(idxOf(7, 7, 7), "Grass", true);
    EXPECT_TRUE(s.solid(idxOf(7, 7, 7)));
    EXPECT_FALSE(s.solid(idxOf(7, 7, 8)));
    EXPECT_EQ(s.solidCount(), 1u);
}

// clear() returns to uniform-air AND releases the dense arrays (chunk pooling/reuse path).
TEST(ChunkVoxelStoreUniform, ClearReturnsToUniformAir) {
    ChunkVoxelStore s;
    s.set(idxOf(1, 1, 1), "Stone", true);   // force a split
    s.clear();
    EXPECT_EQ(s.solidCount(), 0u);
    EXPECT_FALSE(s.solid(idxOf(1, 1, 1)));
    EXPECT_LT(s.approxBytes(), 1024u) << "clear() must release the dense arrays";
}

// isUniform observability for the sealed classifier (stage 2 consumes this).
TEST(ChunkVoxelStoreUniform, ReportsUniformity) {
    ChunkVoxelStore s;
    EXPECT_TRUE(s.isUniform());
    s.fillUniform("Stone", true);
    EXPECT_TRUE(s.isUniform());
    EXPECT_EQ(s.uniformMaterial(), "Stone");
    s.erase(idxOf(0, 0, 0));
    EXPECT_FALSE(s.isUniform());
}

// The chunk-level view: a chunk filled via fillUniform behaves exactly like one filled voxel
// by voxel — the mesher, presence queries, and the blob codec must not see a difference.
TEST(ChunkVoxelStoreUniform, UniformChunkEncodesIdenticallyToDense) {
    Chunk uniform(glm::ivec3(0, 0, 0));
    uniform.initializeForLoading();
    uniform.populateWithCubes();            // fillUniform("Default") post-4.4

    Chunk dense(glm::ivec3(0, 0, 0));
    dense.initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 32; ++y)
            for (int z = 0; z < 32; ++z)
                dense.addCube(glm::ivec3(x, y, z), "Default");

    EXPECT_EQ(uniform.getCubeCount(), dense.getCubeCount());
    std::vector<uint8_t> a = ChunkBlobCodec::encode(uniform);
    std::vector<uint8_t> b = ChunkBlobCodec::encode(dense);
    EXPECT_EQ(a, b) << "uniform and dense representations must serialize byte-identically";

    // And a uniform chunk must round-trip through the codec.
    Chunk loaded(glm::ivec3(0, 0, 0));
    loaded.initializeForLoading();
    ASSERT_TRUE(ChunkBlobCodec::decode(a.data(), a.size(), loaded));
    EXPECT_EQ(loaded.getCubeCount(), ChunkVoxelStore::kVoxels);
    EXPECT_EQ(loaded.getVoxelStore().material(idxOf(9, 9, 9)), "Default");
    EXPECT_TRUE(loaded.getVoxelStore().isUniform())
        << "decoding a whole-chunk run must land in the uniform representation";
}

// getCubeAt (materialize-on-demand, 4.2b) must work on a uniform chunk — materializing one Cube
// splits nothing (the overlay is separate from the store).
TEST(ChunkVoxelStoreUniform, MaterializeOnUniformChunkWorks) {
    Chunk chunk(glm::ivec3(0, 0, 0));
    chunk.initializeForLoading();
    chunk.populateWithCubes();

    Cube* c = chunk.getCubeAt(glm::ivec3(4, 5, 6));
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->getMaterialName(), "Default");
    EXPECT_EQ(chunk.materializedCubeCount(), 1u);
    EXPECT_TRUE(chunk.getVoxelStore().isUniform())
        << "materializing an overlay Cube must not split the store";

    // Removing that voxel goes through removeCube → store erase → split.
    ASSERT_TRUE(chunk.removeCube(glm::ivec3(4, 5, 6)));
    EXPECT_FALSE(chunk.getVoxelStore().solid(idxOf(4, 5, 6)));
    EXPECT_EQ(chunk.getVoxelStore().solidCount(), ChunkVoxelStore::kVoxels - 1);
}
