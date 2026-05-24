// Unit tests for the Phase C0 "# part:" directive in VoxelTemplate parsing.
//
// Drives ObjectTemplateManager::parseLine via reading a temporary .voxel
// file, then verifies that voxels are tagged with the correct partId and
// that part metadata (hinge, axis, movable flag) round-trips. Backward
// compatibility is checked by parsing a directive-free file and confirming
// that exactly one implicit "default" part materialises.

#include <gtest/gtest.h>

#include "core/ObjectTemplateManager.h"
#include "core/VoxelTemplate.h"
#include "core/KinematicVoxelManager.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace Phyxel;

namespace {

fs::path writeTempTemplate(const std::string& name, const std::string& body) {
    auto path = fs::temp_directory_path() / (name + ".voxel");
    std::ofstream f(path);
    f << body;
    return path;
}

VoxelTemplate loadOne(const fs::path& path) {
    ObjectTemplateManager mgr(nullptr, nullptr);
    EXPECT_TRUE(mgr.loadTemplate(path.string()));
    auto stem = path.stem().string();
    const auto* tmpl = mgr.getTemplate(stem);
    EXPECT_NE(tmpl, nullptr);
    return *tmpl;
}

}  // namespace

TEST(VoxelTemplatePart, BackwardCompatibleNoDirective) {
    // No `# part:` directive => one implicit default part, all voxels in it.
    auto path = writeTempTemplate("phasec0_legacy",
        "C 0 0 0 Stone\n"
        "C 1 0 0 Wood\n");
    auto tmpl = loadOne(path);

    ASSERT_EQ(tmpl.parts.size(), 1u);
    EXPECT_EQ(tmpl.parts[0].name, "default");
    EXPECT_FALSE(tmpl.parts[0].movable);
    ASSERT_EQ(tmpl.cubes.size(), 2u);
    EXPECT_EQ(tmpl.cubes[0].partId, 0);
    EXPECT_EQ(tmpl.cubes[1].partId, 0);
}

TEST(VoxelTemplatePart, StaticPartTagsSubsequentVoxels) {
    // A static (no hinge) named part still bumps partId for tagging.
    auto path = writeTempTemplate("phasec0_static",
        "C 0 0 0 Stone\n"       // implicit default (partId 0)
        "# part: trim\n"
        "C 1 0 0 Wood\n");      // tagged with the new part
    auto tmpl = loadOne(path);

    ASSERT_EQ(tmpl.parts.size(), 2u);
    EXPECT_EQ(tmpl.parts[1].name, "trim");
    EXPECT_FALSE(tmpl.parts[1].movable);
    ASSERT_EQ(tmpl.cubes.size(), 2u);
    EXPECT_EQ(tmpl.cubes[0].partId, 0);
    EXPECT_EQ(tmpl.cubes[1].partId, 1);
}

TEST(VoxelTemplatePart, HingeKeywordMakesMovable) {
    auto path = writeTempTemplate("phasec0_hinge_kw",
        "# part: base\n"
        "C 0 0 0 Stone\n"
        "# part: lid hinge=back_top axis=x\n"
        "C 0 2 0 Wood\n");
    auto tmpl = loadOne(path);

    ASSERT_EQ(tmpl.parts.size(), 3u);  // implicit default + base + lid
    const auto& lid = tmpl.parts.back();
    EXPECT_EQ(lid.name, "lid");
    EXPECT_TRUE(lid.movable);
    EXPECT_FALSE(lid.hingeExplicit);
    EXPECT_EQ(lid.hingeKeyword, "back_top");
    EXPECT_EQ(lid.axis, "x");

    // Voxels emitted between the two `# part:` directives belong to "base".
    ASSERT_EQ(tmpl.cubes.size(), 2u);
    EXPECT_EQ(tmpl.cubes[0].partId, 1);  // base
    EXPECT_EQ(tmpl.cubes[1].partId, 2);  // lid
}

TEST(VoxelTemplatePart, HingeExplicitTriple) {
    // Explicit `hinge=x,y,z` form bypasses keyword resolution.
    auto path = writeTempTemplate("phasec0_hinge_xyz",
        "# part: door hinge=0,0,5 axis=y\n"
        "C 0 0 0 Wood\n");
    auto tmpl = loadOne(path);

    const auto& door = tmpl.parts.back();
    EXPECT_TRUE(door.movable);
    EXPECT_TRUE(door.hingeExplicit);
    EXPECT_TRUE(door.hingeKeyword.empty());
    EXPECT_FLOAT_EQ(door.hingeLocal.x, 0.0f);
    EXPECT_FLOAT_EQ(door.hingeLocal.y, 0.0f);
    EXPECT_FLOAT_EQ(door.hingeLocal.z, 5.0f);
    EXPECT_EQ(door.axis, "y");
}

TEST(VoxelTemplatePart, SubcubeAndMicrocubeInherit) {
    // Sub/microcubes share the same partId tagging path as cubes.
    auto path = writeTempTemplate("phasec0_subs",
        "# part: lid hinge=back axis=x\n"
        "C 0 0 0 Wood\n"
        "S 0 0 0 1 1 1 Wood\n"
        "M 0 0 0 1 1 1 2 2 2 Wood\n");
    auto tmpl = loadOne(path);

    ASSERT_EQ(tmpl.cubes.size(), 1u);
    ASSERT_EQ(tmpl.subcubes.size(), 1u);
    ASSERT_EQ(tmpl.microcubes.size(), 1u);
    const int lidId = static_cast<int>(tmpl.parts.size()) - 1;
    EXPECT_EQ(tmpl.cubes[0].partId, lidId);
    EXPECT_EQ(tmpl.subcubes[0].partId, lidId);
    EXPECT_EQ(tmpl.microcubes[0].partId, lidId);
}

// ============================================================================
// Phase C0b: spawnTemplate routes movable-part voxels to KinematicVoxelManager
// instead of baking them into chunks.
//
// These tests construct ObjectTemplateManager with NULL ChunkManager /
// DynamicObjectManager pointers. That is safe because:
//   - A template whose voxels ALL live on a movable part never reaches the
//     chunk bake path.
//   - The kinematic emit path only needs the wired KinematicVoxelManager.
// Mixed templates would crash on null chunkManager — covered by the
// MovableOnly variant, which is the slice we need to validate the routing.
// ============================================================================

TEST(VoxelTemplatePartSpawn, FullyMovableTemplateRoutesToKinematic) {
    auto path = writeTempTemplate("phasec0b_movable_only",
        "# part: lid hinge=back_top axis=x\n"
        "C 0 0 0 Wood\n"
        "C 1 0 0 Wood\n"
        "C 2 0 0 Wood\n");

    ObjectTemplateManager mgr(nullptr, nullptr);
    Core::KinematicVoxelManager kvm;
    mgr.setKinematicVoxelManager(&kvm);
    ASSERT_TRUE(mgr.loadTemplate(path.string()));

    EXPECT_EQ(kvm.count(), 0u);
    ASSERT_TRUE(mgr.spawnTemplate("phasec0b_movable_only", glm::vec3(10, 5, 20), true, 0));

    EXPECT_EQ(kvm.count(), 1u);
    ASSERT_EQ(mgr.lastSpawnedKinematicIds().size(), 1u);
    const auto& id = mgr.lastSpawnedKinematicIds().front();
    const auto& objs = kvm.getObjects();
    ASSERT_TRUE(objs.count(id));
    const auto& obj = objs.at(id);
    EXPECT_EQ(obj.voxels.size(), 3u);
}

TEST(VoxelTemplatePartSpawn, NoKinematicManagerKeepsBackwardCompat) {
    // Without a wired KinematicVoxelManager the manager must fall back to the
    // legacy bake-everything path. We can't safely run the chunk bake here
    // (null ChunkManager) but we CAN confirm the routing decision: with no
    // manager wired, lastSpawnedKinematicIds stays empty even though parts
    // are movable.
    auto path = writeTempTemplate("phasec0b_no_kvm",
        "# part: lid hinge=back axis=x\n");  // no voxels => safe to spawn
    ObjectTemplateManager mgr(nullptr, nullptr);
    ASSERT_TRUE(mgr.loadTemplate(path.string()));
    ASSERT_TRUE(mgr.spawnTemplate("phasec0b_no_kvm", glm::vec3(0), true, 0));
    EXPECT_TRUE(mgr.lastSpawnedKinematicIds().empty());
}

TEST(VoxelTemplatePartSpawn, ExplicitHingeIsHonored) {
    // hinge=2,0,0 — voxels should be re-centered so their local positions
    // are relative to the hinge, and the world transform's translation must
    // include basePos + hinge.
    auto path = writeTempTemplate("phasec0b_explicit_hinge",
        "# part: lid hinge=2,0,0 axis=x\n"
        "C 0 0 0 Wood\n"
        "C 1 0 0 Wood\n");
    ObjectTemplateManager mgr(nullptr, nullptr);
    Core::KinematicVoxelManager kvm;
    mgr.setKinematicVoxelManager(&kvm);
    ASSERT_TRUE(mgr.loadTemplate(path.string()));

    const glm::vec3 spawnAt(100.0f, 0.0f, 0.0f);
    ASSERT_TRUE(mgr.spawnTemplate("phasec0b_explicit_hinge", spawnAt, true, 0));
    ASSERT_EQ(mgr.lastSpawnedKinematicIds().size(), 1u);
    const auto& obj = kvm.getObjects().at(mgr.lastSpawnedKinematicIds().front());

    // The first voxel (cube at template-local (0,0,0)) had center (0.5,0.5,0.5).
    // After hinge subtraction (hinge=(2,0,0)) we expect localPos.x = -1.5.
    ASSERT_EQ(obj.voxels.size(), 2u);
    // Find the voxel that was originally at (0,0,0).
    bool found = false;
    for (const auto& v : obj.voxels) {
        if (std::abs(v.localPos.x - (-1.5f)) < 1e-4f) {
            EXPECT_NEAR(v.localPos.y, 0.5f, 1e-4f);
            EXPECT_NEAR(v.localPos.z, 0.5f, 1e-4f);
            found = true;
        }
    }
    EXPECT_TRUE(found);

    // World transform's translation column should equal spawnAt + hinge.
    const glm::mat4& xf = obj.currentTransform;
    EXPECT_NEAR(xf[3].x, spawnAt.x + 2.0f, 1e-4f);
    EXPECT_NEAR(xf[3].y, spawnAt.y + 0.0f, 1e-4f);
    EXPECT_NEAR(xf[3].z, spawnAt.z + 0.0f, 1e-4f);
}

TEST(VoxelTemplatePartSpawn, MultipleMovablePartsEachGetTheirOwnKinematic) {
    auto path = writeTempTemplate("phasec0b_two_movable",
        "# part: lid_left hinge=left_top axis=x\n"
        "C 0 0 0 Wood\n"
        "# part: lid_right hinge=right_top axis=x\n"
        "C 5 0 0 Wood\n");
    ObjectTemplateManager mgr(nullptr, nullptr);
    Core::KinematicVoxelManager kvm;
    mgr.setKinematicVoxelManager(&kvm);
    ASSERT_TRUE(mgr.loadTemplate(path.string()));
    ASSERT_TRUE(mgr.spawnTemplate("phasec0b_two_movable", glm::vec3(0), true, 0));

    EXPECT_EQ(kvm.count(), 2u);
    EXPECT_EQ(mgr.lastSpawnedKinematicIds().size(), 2u);
}
