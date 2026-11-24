// FaceDirBucketingTest.cpp — Phase 3 face-direction bucketing (docs/LargeWorldScalePlan.md).
//
// After rebuildAllFaces the instance buffer must be DIRECTION-MAJOR: getFaceDirRanges()
// returns 7 prefix offsets ([d] = first instance of faceID d, [6] = total) so draw
// passes can submit only the ranges the GPU wouldn't backface/frontface-cull anyway.
// The mesher path is pure CPU (no Vulkan) — a true headless unit test.

#include <gtest/gtest.h>

#include "graphics/ChunkRenderManager.h"
#include "core/Cube.h"

#include <array>
#include <memory>
#include <vector>

using namespace Phyxel;
using namespace Phyxel::Graphics;

namespace {

uint32_t faceIdOf(const InstanceData& inst) {
    return (inst.packedData >> 15) & 0x7u;
}

// A cube scene guaranteeing faces in all 6 directions: a 2x2 slab of cubes plus one cube alongside.
// (Was a sub/microcube scene; the Chunk 1/9 m store was deleted in D3c-5.)
struct MixedScene {
    std::vector<std::unique_ptr<Cube>> cubes;

    MixedScene() {
        for (int cx = 0; cx < 2; ++cx)
            for (int cz = 0; cz < 2; ++cz)
                cubes.push_back(std::make_unique<Cube>(glm::ivec3(cx, 0, cz), "Stone"));
        cubes.push_back(std::make_unique<Cube>(glm::ivec3(5, 0, 5), "Wood"));
    }
};

} // namespace

TEST(FaceDirBucketing, RangesPartitionFacesDirectionMajor) {
    MixedScene scene;
    ChunkRenderManager crm;
    crm.rebuildAllFaces(scene.cubes, glm::ivec3(0, 0, 0));

    const auto& faces = crm.getFaces();
    const auto& ranges = crm.getFaceDirRanges();
    ASSERT_GT(faces.size(), 0u);

    // Offsets are monotone and span exactly the face count.
    for (int d = 0; d < 6; ++d) EXPECT_LE(ranges[d], ranges[d + 1]);
    EXPECT_EQ(ranges[6], faces.size());
    EXPECT_EQ(ranges[6], crm.getNumInstances());

    // Every instance inside range d has faceID d (the property the draw split relies on).
    for (int d = 0; d < 6; ++d) {
        for (uint32_t i = ranges[d]; i < ranges[d + 1]; ++i) {
            EXPECT_EQ(faceIdOf(faces[i]), static_cast<uint32_t>(d))
                << "instance " << i << " in direction-range " << d;
        }
    }

    // Every direction is represented (the scene exposes faces on all 6 sides).
    for (int d = 0; d < 6; ++d) {
        EXPECT_GT(ranges[d + 1] - ranges[d], 0u) << "no faces in direction " << d;
    }
}

TEST(FaceDirBucketing, ReorderPreservesPerDirectionCounts) {
    MixedScene scene;
    ChunkRenderManager crm;
    crm.rebuildAllFaces(scene.cubes, glm::ivec3(0, 0, 0));

    const auto& faces = crm.getFaces();
    const auto& ranges = crm.getFaceDirRanges();

    // Range widths must equal a full scan's per-direction counts — nothing dropped,
    // nothing duplicated by the counting-sort scatter.
    std::array<size_t, 6> scanCounts{};
    for (const auto& f : faces) scanCounts[faceIdOf(f)]++;
    for (int d = 0; d < 6; ++d) {
        EXPECT_EQ(ranges[d + 1] - ranges[d], scanCounts[d]) << "direction " << d;
    }
}

TEST(FaceDirBucketing, EmptyChunkYieldsZeroRanges) {
    std::vector<std::unique_ptr<Cube>> cubes;
    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, glm::ivec3(0, 0, 0));

    const auto& ranges = crm.getFaceDirRanges();
    for (int d = 0; d <= 6; ++d) EXPECT_EQ(ranges[d], 0u);
    EXPECT_EQ(crm.getNumInstances(), 0u);
}
