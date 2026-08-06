// Fine-resolution item asset class — L2 validation.
//
// Contract under test (docs/FeatureDesignKeys gate, 2026-08-06):
//  1. PARSE: a `# grid: N` header (N in {27, 81}) switches a template to the
//     fine tier; `V x y z Material [tint=#rrggbb] [state=...]` lines populate
//     fineVoxels. Violations (V before # grid, V mixed with C/S/M, invalid N)
//     reject the WHOLE template — loadTemplate returns false and nothing is
//     registered, so a broken file cannot half-load silently.
//  2. MERGE: ItemPropManager::voxelsFromTemplate greedily merges same-
//     material/tint/state fine cells into arbitrary-scale boxes. The merged
//     boxes must tile the source cells exactly: identical cell coverage, no
//     overlap, deterministic across loads.
//  3. CULL: KinematicVoxelManager::buildFaces must cull faces correctly for
//     voxels FINER than 1/9 — the historical lattice was hardcoded to micro
//     resolution ("every voxel scale in the engine is an exact multiple of
//     [1/9]"), which collapses distinct 1/27 voxels onto one cell.
//  4. CONTROLS: existing cube/micro culling behavior is pinned so the lattice
//     generalization cannot move shipped content (sword_fine, felled trees).
//
// All assertions run on real engine output via public APIs (loadTemplate,
// voxelsFromTemplate, add()/getObjects()) — no test doubles.

#include <gtest/gtest.h>

#include "core/ObjectTemplateManager.h"
#include "core/VoxelTemplate.h"
#include "core/ItemPropManager.h"
#include "core/KinematicVoxelManager.h"

#include <glm/glm.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
using namespace Phyxel;
using Phyxel::Core::ItemPropManager;
using Phyxel::Core::KinematicVoxel;
using Phyxel::Core::KinematicVoxelManager;

namespace {

fs::path writeTempTemplate(const std::string& name, const std::string& body) {
    auto path = fs::temp_directory_path() / (name + ".voxel");
    std::ofstream f(path);
    f << body;
    return path;
}

// Load and expect success; returns the registered template.
VoxelTemplate loadOk(const fs::path& path) {
    ObjectTemplateManager mgr(nullptr, nullptr);
    EXPECT_TRUE(mgr.loadTemplate(path.string())) << path;
    const auto* tmpl = mgr.getTemplate(path.stem().string());
    EXPECT_NE(tmpl, nullptr);
    return tmpl ? *tmpl : VoxelTemplate{};
}

// Load and expect the template to be REJECTED (returns false, not registered).
void expectRejected(const fs::path& path) {
    ObjectTemplateManager mgr(nullptr, nullptr);
    EXPECT_FALSE(mgr.loadTemplate(path.string()))
        << path << " should have been rejected";
    EXPECT_EQ(mgr.getTemplate(path.stem().string()), nullptr)
        << path << " must not be registered after rejection";
}

// Reconstruct the exact integer cell coverage of a merged voxel list on an
// N-cells-per-cube grid. Fails the test if any voxel is not cell-aligned.
std::multiset<std::tuple<int, int, int>> cellsCovered(
    const std::vector<KinematicVoxel>& voxels, int gridRes) {
    const float cell = 1.0f / static_cast<float>(gridRes);
    std::multiset<std::tuple<int, int, int>> covered;
    for (const auto& v : voxels) {
        const glm::vec3 mn = v.localPos - v.scale * 0.5f;
        glm::ivec3 base, span;
        for (int a = 0; a < 3; ++a) {
            const float b = mn[a] / cell;
            const float s = v.scale[a] / cell;
            base[a] = static_cast<int>(std::round(b));
            span[a] = static_cast<int>(std::round(s));
            EXPECT_NEAR(b, static_cast<float>(base[a]), 1e-3f) << "voxel not cell-aligned";
            EXPECT_NEAR(s, static_cast<float>(span[a]), 1e-3f) << "voxel span not integral";
            EXPECT_GE(span[a], 1);
        }
        for (int x = 0; x < span.x; ++x)
            for (int y = 0; y < span.y; ++y)
                for (int z = 0; z < span.z; ++z)
                    covered.insert({base.x + x, base.y + y, base.z + z});
    }
    return covered;
}

// Hand-build an axis-aligned block of fine voxels at 1/gridRes scale with
// min corner at the origin: nx x ny x nz cells.
std::vector<KinematicVoxel> fineBlock(int nx, int ny, int nz, int gridRes,
                                      const std::string& mat = "Metal") {
    const float cell = 1.0f / static_cast<float>(gridRes);
    std::vector<KinematicVoxel> voxels;
    for (int x = 0; x < nx; ++x)
        for (int y = 0; y < ny; ++y)
            for (int z = 0; z < nz; ++z) {
                KinematicVoxel v;
                v.localPos = glm::vec3((x + 0.5f) * cell, (y + 0.5f) * cell,
                                       (z + 0.5f) * cell);
                v.scale = glm::vec3(cell);
                v.materialName = mat;
                voxels.push_back(v);
            }
    return voxels;
}

size_t faceCount(KinematicVoxelManager& kvm, const std::string& id) {
    return kvm.getObjects().at(id).faces.size();
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. PARSE
// ---------------------------------------------------------------------------

TEST(FineGridParse, LoadsDeclaredGridAndVoxels) {
    auto path = writeTempTemplate("fine_parse_ok",
        "# grid: 27\n"
        "# category: item\n"
        "V 0 0 0 Wood\n"
        "V 0 1 0 Wood\n"
        "V 0 2 0 Metal tint=#8090a0\n"
        "V 1 2 0 Metal state=flaming\n");
    auto tmpl = loadOk(path);

    EXPECT_EQ(tmpl.fineGridResolution, 27);
    EXPECT_TRUE(tmpl.isFineGrid());
    ASSERT_EQ(tmpl.fineVoxels.size(), 4u);
    EXPECT_EQ(tmpl.fineVoxels[0].pos, glm::ivec3(0, 0, 0));
    EXPECT_EQ(tmpl.fineVoxels[0].material, "Wood");
    EXPECT_EQ(tmpl.fineVoxels[0].tint, 0xFFFFFFu);
    EXPECT_EQ(tmpl.fineVoxels[2].tint, 0x8090A0u);
    EXPECT_EQ(tmpl.fineVoxels[3].state, 1u);  // flaming
    EXPECT_EQ(tmpl.category, "item");
    // Legacy tiers stay empty.
    EXPECT_TRUE(tmpl.cubes.empty());
    EXPECT_TRUE(tmpl.subcubes.empty());
    EXPECT_TRUE(tmpl.microcubes.empty());
}

TEST(FineGridParse, Grid81Accepted) {
    auto path = writeTempTemplate("fine_parse_81",
        "# grid: 81\n"
        "V 0 0 0 Gold\n");
    auto tmpl = loadOk(path);
    EXPECT_EQ(tmpl.fineGridResolution, 81);
    ASSERT_EQ(tmpl.fineVoxels.size(), 1u);
}

TEST(FineGridParse, RejectsInvalidGridValue) {
    // 20 is not 9*3^k — a lattice that doesn't divide 1/9 would corrupt
    // adjacency culling (microSpan rounding failure class).
    expectRejected(writeTempTemplate("fine_parse_bad_grid",
        "# grid: 20\n"
        "V 0 0 0 Wood\n"));
}

TEST(FineGridParse, RejectsVWithoutGridHeader) {
    // A V line with no declared grid has no defined scale.
    expectRejected(writeTempTemplate("fine_parse_no_grid",
        "V 0 0 0 Wood\n"));
}

TEST(FineGridParse, RejectsMixingFineWithLegacy) {
    // One lattice per file: C/S/M after # grid is a contract violation...
    expectRejected(writeTempTemplate("fine_parse_mixed_a",
        "# grid: 27\n"
        "V 0 0 0 Wood\n"
        "C 1 0 0 Stone\n"));
    // ...and so is declaring a grid after legacy geometry.
    expectRejected(writeTempTemplate("fine_parse_mixed_b",
        "C 0 0 0 Stone\n"
        "# grid: 27\n"
        "V 0 0 0 Wood\n"));
}

TEST(FineGridParse, LegacyTemplatesUnchangedControl) {
    // CONTROL: a C/S/M file must parse exactly as before this feature.
    auto path = writeTempTemplate("fine_parse_legacy_control",
        "C 0 0 0 Stone\n"
        "S 1 0 0 0 1 2 Wood\n"
        "M 2 0 0 1 1 1 0 2 1 Metal tint=#112233\n");
    auto tmpl = loadOk(path);
    EXPECT_EQ(tmpl.fineGridResolution, 0);
    EXPECT_FALSE(tmpl.isFineGrid());
    ASSERT_EQ(tmpl.cubes.size(), 1u);
    ASSERT_EQ(tmpl.subcubes.size(), 1u);
    ASSERT_EQ(tmpl.microcubes.size(), 1u);
    EXPECT_EQ(tmpl.microcubes[0].tint, 0x112233u);
}

// ---------------------------------------------------------------------------
// 2. MERGE (voxelsFromTemplate)
// ---------------------------------------------------------------------------

TEST(FineVoxelMerge, BoxesTileSourceCellsExactly) {
    // A sword-ish asset: 1x9x1 Wood grip, 1x9x1 Metal blade above it, and a
    // 5x1x1 Metal guard between — 23 cells across two materials.
    std::string body = "# grid: 27\n";
    for (int y = 0; y < 9; ++y)  body += "V 2 " + std::to_string(y) + " 0 Wood\n";
    for (int y = 10; y < 19; ++y) body += "V 2 " + std::to_string(y) + " 0 Metal\n";
    for (int x = 0; x < 5; ++x)  body += "V " + std::to_string(x) + " 9 0 Metal\n";
    auto tmpl = loadOk(writeTempTemplate("fine_merge_sword", body));
    ASSERT_EQ(tmpl.fineVoxels.size(), 23u);

    auto voxels = ItemPropManager::voxelsFromTemplate(tmpl);
    ASSERT_FALSE(voxels.empty()) << "fine tier ignored by voxelsFromTemplate";

    // Exact tiling: the multiset of covered cells equals the source cells,
    // each exactly once (no overlap, no hole).
    auto covered = cellsCovered(voxels, 27);
    std::multiset<std::tuple<int, int, int>> expected;
    for (const auto& fv : tmpl.fineVoxels)
        expected.insert({fv.pos.x, fv.pos.y, fv.pos.z});
    EXPECT_EQ(covered, expected);

    // The merge must actually merge: 23 cells in 3 straight runs must not
    // stay 23 separate voxels.
    EXPECT_LE(voxels.size(), 6u) << "greedy merge ineffective";
}

TEST(FineVoxelMerge, DifferentMaterialOrTintNeverMerges) {
    auto tmpl = loadOk(writeTempTemplate("fine_merge_boundary",
        "# grid: 27\n"
        "V 0 0 0 Wood\n"
        "V 1 0 0 Metal\n"            // material boundary
        "V 2 0 0 Metal tint=#ff0000\n"));  // tint boundary
    auto voxels = ItemPropManager::voxelsFromTemplate(tmpl);
    ASSERT_EQ(voxels.size(), 3u);
    // Each keeps its own identity.
    std::multiset<std::string> mats;
    for (const auto& v : voxels) mats.insert(v.materialName);
    EXPECT_EQ(mats.count("Wood"), 1u);
    EXPECT_EQ(mats.count("Metal"), 2u);
}

TEST(FineVoxelMerge, DeterministicAcrossLoads) {
    std::string body = "# grid: 27\n";
    // An asymmetric L of two materials.
    for (int y = 0; y < 12; ++y) body += "V 0 " + std::to_string(y) + " 0 Wood\n";
    for (int x = 1; x < 7; ++x)  body += "V " + std::to_string(x) + " 11 0 Metal\n";
    auto t1 = loadOk(writeTempTemplate("fine_merge_det_a", body));
    auto t2 = loadOk(writeTempTemplate("fine_merge_det_b", body));

    auto v1 = ItemPropManager::voxelsFromTemplate(t1);
    auto v2 = ItemPropManager::voxelsFromTemplate(t2);
    ASSERT_EQ(v1.size(), v2.size());
    for (size_t i = 0; i < v1.size(); ++i) {
        EXPECT_EQ(v1[i].localPos, v2[i].localPos);
        EXPECT_EQ(v1[i].scale, v2[i].scale);
        EXPECT_EQ(v1[i].materialName, v2[i].materialName);
        EXPECT_EQ(v1[i].tint, v2[i].tint);
    }
}

// ---------------------------------------------------------------------------
// 3. CULL — fine scales (RED against the hardcoded 1/9 lattice)
// ---------------------------------------------------------------------------

TEST(FineVoxelCulling, SolidFineBlockExposesOnlySurface) {
    // A solid 3x3x3 block of 1/27 voxels (1/9 of a cube across). Interior
    // faces must be culled; every exterior face must survive:
    //   exposed cell-faces = 6 sides * 3*3 = 54.
    // The historical culler collapses these 27 voxels onto ~8 micro cells
    // (round(1/27 * 9) = 0 -> span clamps to 1), so both over- and
    // mis-culling occur.
    KinematicVoxelManager kvm;
    auto id = kvm.add("fine_block", fineBlock(3, 3, 3, 27), glm::mat4(1.0f), "", true);
    EXPECT_EQ(faceCount(kvm, id), 54u);
}

TEST(FineVoxelCulling, FineColumnCullsOnlyTouchingFaces) {
    // 1x3x1 column of 1/27 voxels: 3*6 - 2*2 shared = 14 faces.
    KinematicVoxelManager kvm;
    auto id = kvm.add("fine_column", fineBlock(1, 3, 1, 27), glm::mat4(1.0f), "", true);
    EXPECT_EQ(faceCount(kvm, id), 14u);
}

TEST(FineVoxelCulling, Grid81BlockExposesOnlySurface) {
    // Same invariant one tier finer: 3x3x3 at 1/81 -> 54 faces.
    KinematicVoxelManager kvm;
    auto id = kvm.add("fine81_block", fineBlock(3, 3, 3, 81), glm::mat4(1.0f), "", true);
    EXPECT_EQ(faceCount(kvm, id), 54u);
}

TEST(FineVoxelCulling, NonCubicMergedBoxesCullPerAxis) {
    // Two adjacent merged SLABS (as the greedy merge emits): a 3x1x3-cell
    // plate at y=0 and another at y=1 (grid 27). The shared 3x3 interface
    // must cull both touching faces: 2 boxes * 6 - 2 = 10 faces.
    // The historical culler reads only scale.x for the span, so non-cubic
    // boxes mis-stamp their occupancy.
    const float cell = 1.0f / 27.0f;
    std::vector<KinematicVoxel> voxels;
    for (int y = 0; y < 2; ++y) {
        KinematicVoxel v;
        v.localPos = glm::vec3(1.5f * cell, (y + 0.5f) * cell, 1.5f * cell);
        v.scale = glm::vec3(3.0f * cell, 1.0f * cell, 3.0f * cell);
        v.materialName = "Metal";
        voxels.push_back(v);
    }
    KinematicVoxelManager kvm;
    auto id = kvm.add("fine_slabs", std::move(voxels), glm::mat4(1.0f), "", true);
    EXPECT_EQ(faceCount(kvm, id), 10u);
}

// ---------------------------------------------------------------------------
// 4. CONTROLS — pinned BEFORE the lattice generalization; must stay green
//    after it, or the change broke shipped content (felled trees, sword_fine).
// ---------------------------------------------------------------------------

TEST(FineVoxelCullingControl, TwoStackedFullCubes) {
    std::vector<KinematicVoxel> voxels;
    for (int y = 0; y < 2; ++y) {
        KinematicVoxel v;
        v.localPos = glm::vec3(0.5f, y + 0.5f, 0.5f);
        v.scale = glm::vec3(1.0f);
        v.materialName = "Wood";
        voxels.push_back(v);
    }
    KinematicVoxelManager kvm;
    auto id = kvm.add("control_stack", std::move(voxels), glm::mat4(1.0f), "", true);
    EXPECT_EQ(faceCount(kvm, id), 10u);  // 12 - the 2 touching faces
}

TEST(FineVoxelCullingControl, MixedScaleMicroOnCube) {
    // A microcube centered on a full cube's top: the micro's bottom face is
    // covered (culled); the cube's top face is only 1/81 covered (survives).
    std::vector<KinematicVoxel> voxels;
    {
        KinematicVoxel cube;
        cube.localPos = glm::vec3(0.5f);
        cube.scale = glm::vec3(1.0f);
        cube.materialName = "Wood";
        voxels.push_back(cube);
        KinematicVoxel micro;
        const float m = 1.0f / 9.0f;
        micro.localPos = glm::vec3(4.5f * m, 1.0f + 0.5f * m, 4.5f * m);
        micro.scale = glm::vec3(m);
        micro.materialName = "Metal";
        voxels.push_back(micro);
    }
    KinematicVoxelManager kvm;
    auto id = kvm.add("control_mixed", std::move(voxels), glm::mat4(1.0f), "", true);
    EXPECT_EQ(faceCount(kvm, id), 11u);  // cube 6 + micro 5
}

// ---------------------------------------------------------------------------
// 5. STATIC-BAKE REFUSAL — fine templates are kinematic-only.
// ---------------------------------------------------------------------------

TEST(FineGridStaticBake, FineTemplateRefusedForChunkBake) {
    // The chunk-bake path expands templates onto a hard 9-per-cube micro grid
    // (ObjectTemplateManager.cpp addBox) and cannot represent 1/27 cells; it
    // must refuse a fine template loudly instead of silently degrading.
    ObjectTemplateManager mgr(nullptr, nullptr);
    auto path = writeTempTemplate("fine_bake_refusal",
        "# grid: 27\n"
        "V 0 0 0 Wood\n");
    ASSERT_TRUE(mgr.loadTemplate(path.string()));
    const auto* tmpl = mgr.getTemplate("fine_bake_refusal");
    ASSERT_NE(tmpl, nullptr);
    // With a null ChunkManager placement would fail anyway — the refusal must
    // come from the fine-grid check, which we can observe via the dedicated
    // predicate used by every bake entry point.
    EXPECT_FALSE(ObjectTemplateManager::canBakeStatic(*tmpl));
    // A legacy template CAN bake.
    auto legacyPath = writeTempTemplate("fine_bake_legacy_ok", "C 0 0 0 Stone\n");
    ASSERT_TRUE(mgr.loadTemplate(legacyPath.string()));
    EXPECT_TRUE(ObjectTemplateManager::canBakeStatic(*mgr.getTemplate("fine_bake_legacy_ok")));
}
