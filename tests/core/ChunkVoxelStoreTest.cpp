#include <gtest/gtest.h>

#include <string>

#include "core/Chunk.h"
#include "core/ChunkVoxelStore.h"
#include "core/Cube.h"

using namespace Phyxel;

namespace {

size_t idxOf(int x, int y, int z) { return static_cast<size_t>(z) + y * 32 + x * 1024; }

// Assert the palette store agrees with the authoritative cubes vector for EVERY voxel. This is
// the contract Phase 4.2b depends on: once authority flips, any disagreement here becomes a
// visible terrain bug, so it is checked exhaustively rather than by sampling.
void expectMirrorMatchesCubes(Chunk& chunk) {
    const ChunkVoxelStore& store = chunk.getVoxelStore();
    for (size_t i = 0; i < ChunkVoxelStore::kVoxels; ++i) {
        // flat index -> local pos (canonical z-minor order: idx = z + y*32 + x*1024)
        const int x = static_cast<int>(i / 1024);
        const int y = static_cast<int>((i % 1024) / 32);
        const int z = static_cast<int>(i % 32);
        const Cube* c = chunk.getCubeAt(glm::ivec3(x, y, z));
        ASSERT_EQ(store.solid(i), c != nullptr)
            << "presence mismatch at (" << x << "," << y << "," << z << ")";
        if (c) {
            EXPECT_EQ(store.material(i), c->getMaterialName()) << "material mismatch at " << i;
            EXPECT_EQ(store.visible(i), c->isVisible()) << "visible mismatch at " << i;
        }
    }
}

}  // namespace

// ── the store in isolation ───────────────────────────────────────────────────────

TEST(ChunkVoxelStore, EmptyByDefault) {
    ChunkVoxelStore s;
    EXPECT_EQ(s.solidCount(), 0u);
    EXPECT_EQ(s.paletteSize(), 0u);
    EXPECT_FALSE(s.solid(0));
    EXPECT_EQ(s.material(0), "");
}

TEST(ChunkVoxelStore, SetEraseRoundTrip) {
    ChunkVoxelStore s;
    s.set(idxOf(1, 2, 3), "Stone", true);
    s.set(idxOf(4, 5, 6), "Grass", false);
    EXPECT_TRUE(s.solid(idxOf(1, 2, 3)));
    EXPECT_EQ(s.material(idxOf(1, 2, 3)), "Stone");
    EXPECT_TRUE(s.visible(idxOf(1, 2, 3)));
    EXPECT_EQ(s.material(idxOf(4, 5, 6)), "Grass");
    EXPECT_FALSE(s.visible(idxOf(4, 5, 6)));
    EXPECT_EQ(s.solidCount(), 2u);

    s.erase(idxOf(1, 2, 3));
    EXPECT_FALSE(s.solid(idxOf(1, 2, 3)));
    EXPECT_EQ(s.material(idxOf(1, 2, 3)), "");
    EXPECT_EQ(s.solidCount(), 1u);
}

// The palette is the entire point: repeated materials must NOT grow it.
TEST(ChunkVoxelStore, PaletteInternsRepeatedMaterials) {
    ChunkVoxelStore s;
    for (int i = 0; i < 500; ++i) s.set(static_cast<size_t>(i), "Stone", true);
    EXPECT_EQ(s.paletteSize(), 1u);
    s.set(500, "Grass", true);
    EXPECT_EQ(s.paletteSize(), 2u);
    EXPECT_EQ(s.solidCount(), 501u);
}

TEST(ChunkVoxelStore, OutOfRangeIsSafe) {
    ChunkVoxelStore s;
    s.set(ChunkVoxelStore::kVoxels + 10, "Stone", true);   // ignored, no crash
    EXPECT_FALSE(s.solid(ChunkVoxelStore::kVoxels + 10));
    EXPECT_EQ(s.material(ChunkVoxelStore::kVoxels + 10), "");
    EXPECT_EQ(s.solidCount(), 0u);
}

// A solid chunk's static state must cost ~96 KB, not the ~7.6 MB the Cubes do. This is the
// number Phase 4.2b converts into the actual RAM win.
TEST(ChunkVoxelStore, SolidChunkIsAboutNinetySixKilobytes) {
    ChunkVoxelStore s;
    for (size_t i = 0; i < ChunkVoxelStore::kVoxels; ++i) s.set(i, "Stone", true);
    EXPECT_EQ(s.solidCount(), ChunkVoxelStore::kVoxels);
    EXPECT_EQ(s.paletteSize(), 1u);
    EXPECT_LT(s.approxBytes(), 200u * 1024u);   // 64 KB idx + 32 KB state + palette
}

// ── the mirror against a real Chunk (the 4.2b contract) ──────────────────────────

TEST(ChunkVoxelStoreMirror, TracksAddAndRemove) {
    Chunk chunk(glm::ivec3(0, 0, 0));
    chunk.initializeForLoading();   // wires voxelManager callbacks (no Vulkan needed)
    chunk.addCube(glm::ivec3(1, 1, 1), "Stone");
    chunk.addCube(glm::ivec3(2, 3, 4), "Grass");
    chunk.addCube(glm::ivec3(31, 31, 31), "Dirt");
    expectMirrorMatchesCubes(chunk);
    EXPECT_EQ(chunk.getVoxelStore().material(idxOf(2, 3, 4)), "Grass");

    chunk.removeCube(glm::ivec3(2, 3, 4));
    expectMirrorMatchesCubes(chunk);
    EXPECT_FALSE(chunk.getVoxelStore().solid(idxOf(2, 3, 4)));
}

// Re-adding over an existing cube updates its material; the mirror must follow, not keep the old.
TEST(ChunkVoxelStoreMirror, TracksMaterialOverwrite) {
    Chunk chunk(glm::ivec3(0, 0, 0));
    chunk.initializeForLoading();   // wires voxelManager callbacks (no Vulkan needed)
    chunk.addCube(glm::ivec3(5, 5, 5), "Stone");
    ASSERT_EQ(chunk.getVoxelStore().material(idxOf(5, 5, 5)), "Stone");
    chunk.addCube(glm::ivec3(5, 5, 5), "Grass", /*overwrite=*/true);
    expectMirrorMatchesCubes(chunk);
    EXPECT_EQ(chunk.getVoxelStore().material(idxOf(5, 5, 5)), "Grass");
}

// The out-of-band path: mutate a Cube directly, then sync. This is what the legacy WorldStorage
// load does (addCube then setVisible(false)).
TEST(ChunkVoxelStoreMirror, SyncStoreAtPicksUpDirectCubeMutation) {
    Chunk chunk(glm::ivec3(0, 0, 0));
    chunk.initializeForLoading();   // wires voxelManager callbacks (no Vulkan needed)
    chunk.addCube(glm::ivec3(7, 8, 9), "Stone");
    ASSERT_TRUE(chunk.getVoxelStore().visible(idxOf(7, 8, 9)));

    Cube* c = chunk.getCubeAt(glm::ivec3(7, 8, 9));
    ASSERT_NE(c, nullptr);
    c->setVisible(false);
    chunk.syncVoxelStoreAt(glm::ivec3(7, 8, 9));

    EXPECT_FALSE(chunk.getVoxelStore().visible(idxOf(7, 8, 9)));
    expectMirrorMatchesCubes(chunk);
}

// initializeVoxelMaps() is the gen/load rebuild path — the mirror must come out identical.
TEST(ChunkVoxelStoreMirror, RebuildFromCubesMatches) {
    Chunk chunk(glm::ivec3(0, 0, 0));
    chunk.initializeForLoading();   // wires voxelManager callbacks (no Vulkan needed)
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 8; ++z)
            chunk.addCube(glm::ivec3(x, 0, z), (x + z) % 2 ? "Stone" : "Grass");
    expectMirrorMatchesCubes(chunk);

    chunk.initializeVoxelMaps();   // rebuild from the authoritative cubes
    expectMirrorMatchesCubes(chunk);
    EXPECT_EQ(chunk.getVoxelStore().solidCount(), 64u);
    EXPECT_EQ(chunk.getVoxelStore().paletteSize(), 2u);
}
