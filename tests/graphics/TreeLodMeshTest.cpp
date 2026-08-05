// WRv2 M2 — pure meshing of TemplateLodChain levels (TreeLodMeshRegistry::buildLevelMesh).
// Verifies neighbor-culled quad emission without any Vulkan device.

#include <gtest/gtest.h>

#include "core/PlacedObjectManager.h"   // full InteractionPointDef for VoxelTemplate's vector
#include "core/TemplateLodChain.h"
#include "core/VoxelTemplate.h"
#include "graphics/TreeLodMeshRegistry.h"

using namespace Phyxel;
using namespace Phyxel::Core;
using namespace Phyxel::Graphics;

namespace {
FarMaterialResolver fakeResolver() {
    return [](const std::string& m, int) -> uint16_t { return m == "Leaf" ? 7 : 3; };
}

TemplateLodChain::Level levelOf(std::vector<TemplateLodChain::Cell> cells, int size = 9) {
    TemplateLodChain::Level l;
    l.cellSizeMicros = size;
    l.cells = std::move(cells);
    return l;
}
} // namespace

TEST(TreeLodMeshTest, SingleCellEmitsSixFaces) {
    auto mesh = TreeLodMeshRegistry::buildLevelMesh(
        levelOf({{glm::ivec3(0, 0, 0), "Leaf"}}), fakeResolver());
    EXPECT_EQ(mesh.vertices.size(), 24u);   // 6 faces x 4 verts
    EXPECT_EQ(mesh.indices.size(), 36u);    // 6 faces x 6 indices
    for (const auto& v : mesh.vertices) EXPECT_EQ(farVertexTexIndex(v.packed), 7);
}

TEST(TreeLodMeshTest, AdjacentCellsCullSharedFaces) {
    auto mesh = TreeLodMeshRegistry::buildLevelMesh(
        levelOf({{glm::ivec3(0, 0, 0), "Log"}, {glm::ivec3(0, 1, 0), "Leaf"}}),
        fakeResolver());
    // Two cubes sharing one face: 12 - 2 = 10 faces.
    EXPECT_EQ(mesh.vertices.size(), 40u);
    EXPECT_EQ(mesh.indices.size(), 60u);
}

TEST(TreeLodMeshTest, MeshIsTrunkAnchoredAtOrigin) {
    // The base cell (0,0,0) at cell size 9 (one voxel) spans local -0.5..0.5 on X/Z — the
    // trunk axis sits at the mesh origin so instances place trees by their column center.
    auto mesh = TreeLodMeshRegistry::buildLevelMesh(
        levelOf({{glm::ivec3(0, 0, 0), "Log"}}), fakeResolver());
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f;
    for (const auto& v : mesh.vertices) {
        minX = std::min(minX, v.pos.x);
        maxX = std::max(maxX, v.pos.x);
        minY = std::min(minY, v.pos.y);
    }
    EXPECT_FLOAT_EQ(minX, -0.5f);
    EXPECT_FLOAT_EQ(maxX, 0.5f);
    EXPECT_FLOAT_EQ(minY, 0.0f);   // trunk base sits ON the ground plane
}

TEST(TreeLodMeshTest, AnchorMatchesTheNearStamp) {
    // THE correspondence pin (user: "the lower detail trees dont seem to correspond with high
    // detail trees"). decorateChunk stamps at base = worldPos - maxExtent/2; the far instance
    // sits at the column center (worldX + 0.5). For a template spanning voxels 0..8 on X/Z,
    // stamped voxel v occupies world [worldPos + v - 4, +1] — so the mesh, drawn at the
    // instance position, must put voxel v at local [v - 4.5, v - 3.5].
    VoxelTemplate t;
    t.name = "stamp_parity";
    for (int x = 0; x <= 8; ++x)
        t.cubes.push_back({glm::ivec3(x, 0, 4), "Log"});   // 9-wide row at z=4
    const glm::vec3 anchor = TreeLodMeshRegistry::stampAnchorFor(t);
    EXPECT_FLOAT_EQ(anchor.x, -4.5f);   // mx.x=8 -> -(8/2) - 0.5
    EXPECT_FLOAT_EQ(anchor.z, -2.5f);   // mx.z=4 -> -(4/2) - 0.5 (stamp halves per AXIS extent)
    EXPECT_FLOAT_EQ(anchor.y, 0.0f);

    // Meshed at that anchor, voxel x=0 must start at -4.5 and voxel x=8 end at +4.5 — i.e.
    // the mesh's world span equals the stamp's span exactly (offset ghost = regression).
    std::vector<TemplateLodChain::Cell> cells;
    for (int x = 0; x <= 8; ++x) cells.push_back({glm::ivec3(x, 0, 4), "Log"});
    auto mesh = TreeLodMeshRegistry::buildLevelMesh(levelOf(std::move(cells)),
                                                    fakeResolver(), anchor);
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& v : mesh.vertices) {
        minX = std::min(minX, v.pos.x);
        maxX = std::max(maxX, v.pos.x);
    }
    EXPECT_FLOAT_EQ(minX, -4.5f);
    EXPECT_FLOAT_EQ(maxX, 4.5f);
}
