// L2 validation for Phase 3 — planar projected surfaces (rugs/paintings/banners).
//
// Contract: when a kinematic object carries an active KinematicSurface, the faces
// whose normal lies along the projection axis must be textured so a SINGLE image
// spans the whole object's footprint — i.e. each such face's UV rectangle is its
// slice of the object extent (NOT per-voxel material tiling), the slices tile the
// unit square with no overlap, and they sample the surface texture index. Faces off
// the projection axis are untouched (material tiling preserved).
//
// These assertions run on the real buildFaces output via the public add()/getObjects()
// API. See docs/VoxelAppearanceModel.md §7 Phase 3.

#include <gtest/gtest.h>

#include "core/KinematicVoxelManager.h"

#include <glm/glm.hpp>
#include <vector>
#include <cmath>

using namespace Phyxel::Core;

namespace {

constexpr float kEps = 1e-4f;
constexpr uint16_t kSurfaceTex = 7777;  // sentinel distinct from any material index

// A flat slab of full cubes: W along X, 1 along Y, D along Z, resting at y=0.
std::vector<KinematicVoxel> flatRug(int W, int D) {
    std::vector<KinematicVoxel> voxels;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z) {
            KinematicVoxel v;
            v.localPos     = glm::vec3(x + 0.5f, 0.5f, z + 0.5f);
            v.scale        = glm::vec3(1.0f);
            v.materialName = "Cloth";
            voxels.push_back(v);
        }
    return voxels;
}

uint32_t faceOf(const KinematicFaceData& f) { return f.faceId & 0x7u; }

}  // namespace

// A 3x1x2 rug: the six top (+Y) faces must each carry a 1/3 x 1/2 slice of the
// surface image, and together tile [0,1]^2 exactly.
TEST(KinematicSurfaceProjection, TopFacesTileObjectExtent) {
    KinematicVoxelManager kvm;
    KinematicSurface surf;
    surf.active = true;
    surf.textureIndex = kSurfaceTex;
    surf.axis = 1;  // Y

    auto id = kvm.add("rug", flatRug(3, 2), glm::mat4(1.0f), "", true, surf);
    const auto& obj = kvm.getObjects().at(id);

    int topCount = 0;
    double areaSum = 0.0;
    for (const auto& f : obj.faces) {
        if (faceOf(f) != 4u) continue;  // +Y only
        ++topCount;
        // Projected slice: 1/3 of the X extent, 1/2 of the Z extent.
        EXPECT_NEAR(f.uvScale.x, 1.0f / 3.0f, kEps);
        EXPECT_NEAR(f.uvScale.y, 1.0f / 2.0f, kEps);
        EXPECT_EQ(f.textureIndex, kSurfaceTex);
        // Slice stays inside the unit square.
        EXPECT_GE(f.uvOffset.x, -kEps);
        EXPECT_GE(f.uvOffset.y, -kEps);
        EXPECT_LE(f.uvOffset.x + f.uvScale.x, 1.0f + kEps);
        EXPECT_LE(f.uvOffset.y + f.uvScale.y, 1.0f + kEps);
        areaSum += double(f.uvScale.x) * double(f.uvScale.y);
    }
    EXPECT_EQ(topCount, 6);                 // 3*2 cubes, all top faces exposed
    EXPECT_NEAR(areaSum, 1.0, 1e-3);        // slices tile the whole image once
}

// Side faces (normal not along the projection axis) keep material tiling:
// untouched texture index and unit uvScale.
TEST(KinematicSurfaceProjection, SideFacesUnaffected) {
    KinematicVoxelManager kvm;
    KinematicSurface surf;
    surf.active = true;
    surf.textureIndex = kSurfaceTex;
    surf.axis = 1;  // Y

    auto id = kvm.add("rug", flatRug(3, 2), glm::mat4(1.0f), "", true, surf);
    const auto& obj = kvm.getObjects().at(id);

    for (const auto& f : obj.faces) {
        uint32_t fc = faceOf(f);
        if (fc == 4u || fc == 5u) continue;  // skip projected (top/bottom)
        EXPECT_NE(f.textureIndex, kSurfaceTex);
        EXPECT_NEAR(f.uvScale.x, 1.0f, kEps);
        EXPECT_NEAR(f.uvScale.y, 1.0f, kEps);
    }
}

// Degenerate 1x1 rug: the single top face maps the entire image.
TEST(KinematicSurfaceProjection, SingleCubeFullImage) {
    KinematicVoxelManager kvm;
    KinematicSurface surf;
    surf.active = true;
    surf.textureIndex = kSurfaceTex;
    surf.axis = 1;

    auto id = kvm.add("rug", flatRug(1, 1), glm::mat4(1.0f), "", true, surf);
    const auto& obj = kvm.getObjects().at(id);

    int top = 0;
    for (const auto& f : obj.faces) {
        if (faceOf(f) != 4u) continue;
        ++top;
        EXPECT_NEAR(f.uvScale.x, 1.0f, kEps);
        EXPECT_NEAR(f.uvScale.y, 1.0f, kEps);
        EXPECT_NEAR(f.uvOffset.x, 0.0f, kEps);
        EXPECT_NEAR(f.uvOffset.y, 0.0f, kEps);
        EXPECT_EQ(f.textureIndex, kSurfaceTex);
    }
    EXPECT_EQ(top, 1);
}

// No surface => byte-identical to the legacy path: uvScale == voxel scale,
// no texture override.
TEST(KinematicSurfaceProjection, NoSurfaceIsLegacyTiling) {
    KinematicVoxelManager kvm;
    auto id = kvm.add("rug", flatRug(2, 2), glm::mat4(1.0f), "", true);  // no surface
    const auto& obj = kvm.getObjects().at(id);
    for (const auto& f : obj.faces) {
        EXPECT_NEAR(f.uvScale.x, 1.0f, kEps);  // full cubes
        EXPECT_NEAR(f.uvScale.y, 1.0f, kEps);
        EXPECT_NE(f.textureIndex, kSurfaceTex);
    }
}
