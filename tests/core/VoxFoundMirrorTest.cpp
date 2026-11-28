// docs/VoxelFoundationPlan.md step 7 D4 follow-up S3 -- the mirror plane on the switched world
// (VoxFound::mirrorFacesOfChunk / mirrorPlane). Today's Chunk flagged its first mirror voxel and the renderer fixed the
// normal at +Z (RenderCoordinator::scanForMirrorVoxels, deleted in D4: since then no mirror ever drew). Restated on
// 1/8 m voxels: exposed Mirror faces counted per direction; normal = the direction with the most faces (ties -x, +x,
// -y, +y, -z, +z); point = the mean centre of those faces. Summed per chunk -- a chunk border must not add faces.
// RED before: the stubs report no faces and no plane.
#include <gtest/gtest.h>

#include "core/MaterialRegistry.h"
#include "core/voxfound/EmissiveLights.h"

using namespace Phyxel::Core;
using namespace Phyxel::Core::VoxFound;

namespace {
struct MirrorRig : ::testing::Test {
    static void SetUpTestSuite() { MaterialRegistry::instance().loadFromJson("resources/materials.json"); }
    std::vector<std::string> palette{"", "Stone", "Mirror"};
};
}  // namespace

TEST_F(MirrorRig, NoMirrorInThePaletteMeansNoScan) {
    EXPECT_FALSE(anyMirror(mirrorMaterials({"", "Stone", "Glass"})));
    EXPECT_TRUE(anyMirror(mirrorMaterials(palette)));
    MirrorFaces none;
    glm::vec3 p, n;
    EXPECT_FALSE(mirrorPlane(none, p, n));
}

TEST_F(MirrorRig, PlaneOfASlabInFrontOfStone) {
    // Stone fills z 32..39 behind; a 2 x 2 m Mirror slab one voxel thick at z = 40 (x, y 16..31).
    // Predicted: +z 256 faces (16 x 16), -z 0 (stone), each side 16; normal +z; point (3.0, 3.0, 41 / 8).
    BlockStore store{glm::ivec3(256)};
    for (int bx = 0; bx < 6; ++bx)
        for (int by = 0; by < 6; ++by) store.fillBlock(glm::ivec3(bx, by, 4), 1);
    for (int x = 16; x < 32; ++x)
        for (int y = 16; y < 32; ++y) store.set(glm::ivec3(x, y, 40), 2);
    MirrorFaces acc;
    mirrorFacesOfChunk(store, glm::ivec3(0), mirrorMaterials(palette), acc);
    EXPECT_EQ(acc.count[5], 256u);   // +z
    EXPECT_EQ(acc.count[4], 0u);     // -z: against stone
    EXPECT_EQ(acc.count[0], 16u);
    EXPECT_EQ(acc.count[1], 16u);
    EXPECT_EQ(acc.count[2], 16u);
    EXPECT_EQ(acc.count[3], 16u);
    glm::vec3 p, n;
    ASSERT_TRUE(mirrorPlane(acc, p, n));
    EXPECT_EQ(n, glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(p.x, 3.0f, 1e-5f);
    EXPECT_NEAR(p.y, 3.0f, 1e-5f);
    EXPECT_NEAR(p.z, 41.0f / 8.0f, 1e-5f);
}

TEST_F(MirrorRig, AChunkBorderAddsNoFaces) {
    // The same slab, but straddling the x border of chunks (0,0,0) and (1,0,0): world voxels x 248..263. Summed per
    // chunk, with the neighbour chunk's voxels read across the border: +z 256, +x / -x 16 each (not 32), point x 32 m.
    BlockStore a{glm::ivec3(256)}, b{glm::ivec3(256)};
    for (int x = 248; x < 264; ++x)
        for (int y = 16; y < 32; ++y) (x < 256 ? a : b).set(glm::ivec3(x & 255, y, 40), 2);
    auto at = [&](glm::ivec3 w) -> uint8_t {   // world voxel -> material (the two chunks; air elsewhere)
        if (w.y < 0 || w.y > 255 || w.z < 0 || w.z > 255) return 0;
        if (w.x >= 0 && w.x < 256) return a.at(w);
        if (w.x >= 256 && w.x < 512) return b.at(glm::ivec3(w.x - 256, w.y, w.z));
        return 0;
    };
    const std::function<uint8_t(glm::ivec3)> outside = at;
    MirrorFaces acc;
    const auto table = mirrorMaterials(palette);
    mirrorFacesOfChunk(a, glm::ivec3(0, 0, 0), table, acc, &outside);
    mirrorFacesOfChunk(b, glm::ivec3(1, 0, 0), table, acc, &outside);
    EXPECT_EQ(acc.count[5], 256u);
    EXPECT_EQ(acc.count[4], 256u);   // nothing behind this one: both sides exposed
    EXPECT_EQ(acc.count[0], 16u);
    EXPECT_EQ(acc.count[1], 16u);
    glm::vec3 p, n;
    ASSERT_TRUE(mirrorPlane(acc, p, n));
    EXPECT_EQ(n, glm::vec3(0.0f, 0.0f, -1.0f));   // the tie -z / +z goes to -z (the stated order)
    EXPECT_NEAR(p.x, 32.0f, 1e-4f);
    EXPECT_NEAR(p.z, 40.0f / 8.0f, 1e-5f);
}
