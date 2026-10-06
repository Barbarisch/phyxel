// DebrisSliceTest — a broken piece keeps the slice of its parent cube's texture
// (DebrisInteractionPlan 1d, texture parity).
//
// GPU debris used to render every subcube/microcube piece with the CENTRE slice of the parent
// texture (particle_expand hard-coded localPos (1,1,1)), so a piece visibly changed texture the
// moment it broke. Each piece now carries its micro position inside the parent cube in
// materialIndex bits 16-25; particle_expand decodes it into the grid position dynamic_voxel.vert
// reads. These tests pin the CPU encode (DamageSystem::debrisSliceFor) and that the decode the
// shader performs recovers the right grid position. The visual proof is the live pixel check.

#include <gtest/gtest.h>

#include "core/DamageSystem.h"
#include "solver_shared.h"   // shaders/ is on the include path (1b)

using namespace Phyxel;

namespace {

/// The decode particle_expand.comp performs (kept beside the test as the pinned contract).
glm::ivec3 decodeLocalPos(uint32_t slice, float scale) {
    const glm::ivec3 m(int(slice / 81u), int((slice / 9u) % 9u), int(slice % 9u));
    const glm::ivec3 s = m / 3, u = m % 3;
    if (scale < 0.2f) return {s.x | (s.y << 2) | (s.z << 4) | (u.x << 6) | (u.y << 8) | (u.z << 10), 0, 0};
    return s;
}

}  // namespace

TEST(DebrisSlice, EverySubcubeKeepsItsOwnSliceAtAnyCube) {
    for (const glm::ivec3 cube : {glm::ivec3(0, 0, 0), glm::ivec3(173, 16, 13), glm::ivec3(-5, -1, -40)})
        for (int x = 0; x < 3; ++x) for (int y = 0; y < 3; ++y) for (int z = 0; z < 3; ++z) {
            const glm::vec3 centre = glm::vec3(cube) + (glm::vec3(x, y, z) + 0.5f) / 3.0f;
            const uint32_t slice = DamageSystem::debrisSliceFor(centre, 1.0f / 3.0f);
            EXPECT_EQ(decodeLocalPos(slice, 1.0f / 3.0f), glm::ivec3(x, y, z))
                << "subcube (" << x << "," << y << "," << z << ") in cube (" << cube.x << "," << cube.y << "," << cube.z << ")";
        }
}

TEST(DebrisSlice, EveryMicrocubeKeepsItsOwnSlice) {
    for (const glm::ivec3 cube : {glm::ivec3(2, 3, 4), glm::ivec3(-7, -2, -9)})
        for (int x = 0; x < 9; ++x) for (int y = 0; y < 9; ++y) for (int z = 0; z < 9; ++z) {
            const glm::vec3 centre = glm::vec3(cube) + (glm::vec3(x, y, z) + 0.5f) / 9.0f;
            const uint32_t slice = DamageSystem::debrisSliceFor(centre, 1.0f / 9.0f);
            const glm::ivec3 s(x / 3, y / 3, z / 3), u(x % 3, y % 3, z % 3);
            const int packed = s.x | (s.y << 2) | (s.z << 4) | (u.x << 6) | (u.y << 8) | (u.z << 10);
            ASSERT_EQ(decodeLocalPos(slice, 1.0f / 9.0f).x, packed)
                << "micro (" << x << "," << y << "," << z << ")";
        }
}

TEST(DebrisSlice, TheSliceFitsItsBitsAndLeavesTheMaterialAlone) {
    const uint32_t worst = DamageSystem::debrisSliceFor(glm::vec3(0.99f), 1.0f / 9.0f);   // micro (8,8,8)
    EXPECT_EQ(worst, 728u);
    EXPECT_LE(worst, DebrisShared::SLICE_MASK);
    const uint32_t packed = (101u & DebrisShared::MATERIAL_MASK) | (worst << DebrisShared::SLICE_SHIFT);
    EXPECT_EQ(packed & DebrisShared::MATERIAL_MASK, 101u) << "the material survives the slice bits";
    EXPECT_EQ((packed >> DebrisShared::SLICE_SHIFT) & DebrisShared::SLICE_MASK, 728u);
}

TEST(DebrisSlice, FullCubesShowTheWholeFace) {
    EXPECT_EQ(DamageSystem::debrisSliceFor(glm::vec3(3.5f, 4.5f, 5.5f), 1.0f), 0u);
}
