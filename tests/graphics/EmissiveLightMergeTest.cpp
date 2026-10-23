// EmissiveLightMergeTest.cpp -- one light per fixture, at the emitting voxel (G-158).
//
// RED-BEFORE-GREEN for the duplicate-light bug found while measuring G-18/G-52.
// ChunkRenderManager::rebuildAllFaces emits one EmissiveLight per emissive SUB-VOXEL but
// positions it at the PARENT CUBE (sc->getPosition(), mc->getParentCubePosition()), two lines
// below a flaming-voxel push that correctly uses getWorldPosition(). So every emissive
// sub-voxel inside one cube becomes a CO-LOCATED DUPLICATE -- violating EmissiveLight's own
// documented contract ("centre of the emitting voxel, in world units").
//
// Measured consequences, shipped Release, Ravenmere town (docs/evidence/ravenmere/rv_perf8.json):
// 143 registered point lights, 32 uploaded, ALL 32 at the identical position -- one distinct
// position among 32 rows. Three defects from one cause: ~31 of every 32 per-fragment occupancy
// marches are exact duplicates (the 236 ms light bill is ~97% redundant); the 32-light budget is
// consumed by ONE fixture so every other light in the town is starved out; and the copies SUM, so
// that fixture runs ~32x over-bright. Across the shipped asset library: 3518 emitted lights from
// 199 distinct parent cubes (17.7x; worst forge_bush_glow_green 1270 from 38).
//
// The mesher path under test is pure CPU -- it fills vectors and never touches Vulkan.

#include <gtest/gtest.h>

#include "graphics/ChunkRenderManager.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
#include "core/MaterialRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace Phyxel;
using namespace Phyxel::Graphics;
using Phyxel::Core::MaterialRegistry;

namespace {

std::string findMaterialsJson() {
    for (const auto& path : {"resources/materials.json", "../resources/materials.json",
                             "../../resources/materials.json", "../../../resources/materials.json"})
        if (std::filesystem::exists(path)) return path;
    return "resources/materials.json";
}

using Cubes  = std::vector<std::unique_ptr<Cube>>;
using Subs   = std::vector<std::unique_ptr<Subcube>>;
using Micros = std::vector<std::unique_ptr<Microcube>>;

// The TRUE centre of a microcube, computed independently of the code under test: parent cube +
// subcube offset (1/3) + microcube offset (1/9) + half a microcube (1/18).
glm::vec3 microCentre(const glm::ivec3& cube, const glm::ivec3& sub, const glm::ivec3& micro) {
    return glm::vec3(cube) + glm::vec3(sub) / 3.0f + glm::vec3(micro) / 9.0f + glm::vec3(1.0f / 18.0f);
}

glm::vec3 subCentre(const glm::ivec3& cube, const glm::ivec3& sub) {
    return glm::vec3(cube) + glm::vec3(sub) / 3.0f + glm::vec3(1.0f / 6.0f);
}

// n emissive microcubes packed from the subcube grid origin of one parent cube, in a fixed,
// reproducible order -- the way an authored fixture fills a flame or a bed of coals.
Micros glowMicrosInCube(const glm::ivec3& cube, int n, const char* material = "glow") {
    Micros out;
    int made = 0;
    for (int sx = 0; sx < 3 && made < n; ++sx)
    for (int sy = 0; sy < 3 && made < n; ++sy)
    for (int sz = 0; sz < 3 && made < n; ++sz)
    for (int mx = 0; mx < 3 && made < n; ++mx)
    for (int my = 0; my < 3 && made < n; ++my)
    for (int mz = 0; mz < 3 && made < n; ++mz) {
        out.push_back(std::make_unique<Microcube>(cube, glm::ivec3(sx, sy, sz),
                                                  glm::ivec3(mx, my, mz), material));
        ++made;
    }
    return out;
}

class EmissiveLightMerge : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(MaterialRegistry::instance().loadFromJson(findMaterialsJson()))
            << "materials.json must load: 'glow' is what makes a voxel an emitter";
        ASSERT_NE(MaterialRegistry::instance().getMaterial("glow"), nullptr)
            << "no 'glow' material -- the whole fixture is meaningless";
    }
};

}  // namespace

// =============================================================================================
// THE BUG ITSELF
// =============================================================================================

// A torch flame or a bed of coals is many emissive sub-voxels in ONE cube. It is one light.
TEST_F(EmissiveLightMerge, ManyEmissiveMicrocubesInOneCubeMakeExactlyOneLight) {
    for (int n : {2, 8, 66, 200}) {
        auto micros = glowMicrosInCube(glm::ivec3(4, 5, 6), n);
        ChunkRenderManager crm;
        crm.rebuildAllFaces(Cubes{}, Subs{}, micros, glm::ivec3(0, 0, 0));
        EXPECT_EQ(crm.getEmissiveLights().size(), 1u)
            << n << " emissive microcubes in ONE cube produced "
            << crm.getEmissiveLights().size() << " lights -- every extra one is a CO-LOCATED "
            << "duplicate that burns a slot in the 32-light cap, marches the same occupancy ray "
            << "again, and adds its contribution again (G-158)";
    }
}

// The general form of the defect, stated as an invariant that must hold for ANY input.
TEST_F(EmissiveLightMerge, NoTwoEmittedLightsEverShareAPosition) {
    auto micros = glowMicrosInCube(glm::ivec3(1, 1, 1), 40);
    for (auto& m : glowMicrosInCube(glm::ivec3(9, 2, 3), 12, "glow_blue")) micros.push_back(std::move(m));
    Subs subs;
    subs.push_back(std::make_unique<Subcube>(glm::ivec3(5, 5, 5), glm::ivec3(1, 2, 1), "glow"));
    subs.push_back(std::make_unique<Subcube>(glm::ivec3(5, 5, 5), glm::ivec3(2, 2, 1), "glow"));
    Cubes cubes;
    cubes.push_back(std::make_unique<Cube>(glm::ivec3(20, 3, 7), "glow_green"));

    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, subs, micros, glm::ivec3(0, 0, 0));

    std::set<std::array<int, 3>> seen;
    for (const auto& e : crm.getEmissiveLights()) {
        // Quantise to 1/1000 u so the comparison is exact rather than float-fuzzy.
        std::array<int, 3> key{static_cast<int>(std::lround(e.worldPos.x * 1000.0f)),
                               static_cast<int>(std::lround(e.worldPos.y * 1000.0f)),
                               static_cast<int>(std::lround(e.worldPos.z * 1000.0f))};
        EXPECT_TRUE(seen.insert(key).second)
            << "two lights at the same position (" << e.worldPos.x << ", " << e.worldPos.y << ", "
            << e.worldPos.z << ") -- co-located lights cannot be culled or clustered apart, they "
            << "just multiply the per-fragment march and the brightness (G-158)";
    }
    // Four distinct fixtures went in: two microcube clusters, one subcube pair, one cube.
    EXPECT_EQ(crm.getEmissiveLights().size(), 4u);
}

// The struct documents worldPos as "centre of the emitting voxel"; the code writes the cube centre.
TEST_F(EmissiveLightMerge, TheLightSitsAtTheEmittingVoxelNotTheParentCubeCentre) {
    const glm::ivec3 cube(3, 4, 5), sub(1, 2, 1), micro(0, 1, 2);
    Micros micros;
    micros.push_back(std::make_unique<Microcube>(cube, sub, micro, "glow"));

    ChunkRenderManager crm;
    crm.rebuildAllFaces(Cubes{}, Subs{}, micros, glm::ivec3(0, 0, 0));
    ASSERT_EQ(crm.getEmissiveLights().size(), 1u);

    const glm::vec3 want = microCentre(cube, sub, micro);
    const glm::vec3 got  = crm.getEmissiveLights()[0].worldPos;
    const glm::vec3 cubeCentre = glm::vec3(cube) + 0.5f;
    // Control FIRST: the right answer and the wrong one are genuinely distinguishable here, so a
    // pass means something.
    ASSERT_GT(glm::length(want - cubeCentre), 0.05f)
        << "test is degenerate -- pick a microcube that is not at the cube centre";
    EXPECT_NEAR(got.x, want.x, 1e-4f);
    EXPECT_NEAR(got.y, want.y, 1e-4f);
    EXPECT_NEAR(got.z, want.z, 1e-4f);
}

// A merged fixture lights from the middle of the fire, not the middle of the cube.
TEST_F(EmissiveLightMerge, TheMergedLightSitsAtTheEmissiveCentroid) {
    const glm::ivec3 cube(2, 2, 2);
    // An ASYMMETRIC pair, so the cube centre is the WRONG answer and cannot pass by luck.
    Micros micros;
    micros.push_back(std::make_unique<Microcube>(cube, glm::ivec3(0, 0, 0), glm::ivec3(0, 0, 0), "glow"));
    micros.push_back(std::make_unique<Microcube>(cube, glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0), "glow"));

    ChunkRenderManager crm;
    crm.rebuildAllFaces(Cubes{}, Subs{}, micros, glm::ivec3(0, 0, 0));
    ASSERT_EQ(crm.getEmissiveLights().size(), 1u);

    const glm::vec3 want = 0.5f * (microCentre(cube, glm::ivec3(0, 0, 0), glm::ivec3(0, 0, 0)) +
                                   microCentre(cube, glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0)));
    const glm::vec3 got = crm.getEmissiveLights()[0].worldPos;
    ASSERT_GT(glm::length(want - (glm::vec3(cube) + 0.5f)), 0.3f) << "test is degenerate";
    EXPECT_NEAR(got.x, want.x, 1e-4f);
    EXPECT_NEAR(got.y, want.y, 1e-4f);
    EXPECT_NEAR(got.z, want.z, 1e-4f);
}

// Merging is PER PARENT CUBE, which is chunk-local by construction. It must not over-merge two
// fixtures that happen to sit next to each other.
TEST_F(EmissiveLightMerge, SeparateCubesKeepSeparateLights) {
    auto micros = glowMicrosInCube(glm::ivec3(4, 4, 4), 20);
    for (auto& m : glowMicrosInCube(glm::ivec3(5, 4, 4), 20)) micros.push_back(std::move(m));  // adjacent
    ChunkRenderManager crm;
    crm.rebuildAllFaces(Cubes{}, Subs{}, micros, glm::ivec3(0, 0, 0));
    EXPECT_EQ(crm.getEmissiveLights().size(), 2u)
        << "two neighbouring cubes of emissive voxels are two fixtures, not one";
}

// =============================================================================================
// NO REGRESSION FOR WHAT ALREADY WORKED
// =============================================================================================

TEST_F(EmissiveLightMerge, AFullGlowCubeIsCompletelyUnchanged) {
    Cubes cubes;
    cubes.push_back(std::make_unique<Cube>(glm::ivec3(6, 7, 8), "glow"));
    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, Subs{}, Micros{}, glm::ivec3(0, 0, 0));
    ASSERT_EQ(crm.getEmissiveLights().size(), 1u);
    const auto& e = crm.getEmissiveLights()[0];
    EXPECT_NEAR(e.worldPos.x, 6.5f, 1e-4f);
    EXPECT_NEAR(e.worldPos.y, 7.5f, 1e-4f);
    EXPECT_NEAR(e.worldPos.z, 8.5f, 1e-4f);
    EXPECT_NEAR(e.intensity, 1.0f, 1e-4f);
    EXPECT_NEAR(e.radius, 15.0f, 1e-3f) << "a full glow cube's reach is the shipped 15 u anchor";
}

// A lone sub-voxel emitter keeps the brightness and reach it ships with today -- only its
// POSITION is corrected. Without this, "merging" could quietly dim every single-cell torch.
TEST_F(EmissiveLightMerge, ALoneMicrocubeKeepsItsShippedBrightnessAndReach) {
    Micros micros;
    micros.push_back(std::make_unique<Microcube>(glm::ivec3(1, 1, 1), glm::ivec3(1, 1, 1),
                                                 glm::ivec3(1, 1, 1), "glow"));
    ChunkRenderManager crm;
    crm.rebuildAllFaces(Cubes{}, Subs{}, micros, glm::ivec3(0, 0, 0));
    ASSERT_EQ(crm.getEmissiveLights().size(), 1u);
    EXPECT_NEAR(crm.getEmissiveLights()[0].intensity, 1.0f, 1e-4f);
    EXPECT_NEAR(crm.getEmissiveLights()[0].radius, 7.5f, 1e-3f)
        << "shipped microcube reach is 15 * t * 0.5";
}

TEST_F(EmissiveLightMerge, ALoneSubcubeKeepsItsShippedBrightnessAndReach) {
    Subs subs;
    subs.push_back(std::make_unique<Subcube>(glm::ivec3(1, 1, 1), glm::ivec3(1, 2, 1), "glow"));
    ChunkRenderManager crm;
    crm.rebuildAllFaces(Cubes{}, subs, Micros{}, glm::ivec3(0, 0, 0));
    ASSERT_EQ(crm.getEmissiveLights().size(), 1u);
    const auto& e = crm.getEmissiveLights()[0];
    EXPECT_NEAR(e.intensity, 1.0f, 1e-4f);
    EXPECT_NEAR(e.radius, 11.25f, 1e-3f) << "shipped subcube reach is 15 * t * 0.75";
    const glm::vec3 want = subCentre(glm::ivec3(1, 1, 1), glm::ivec3(1, 2, 1));
    EXPECT_NEAR(e.worldPos.x, want.x, 1e-4f);
    EXPECT_NEAR(e.worldPos.y, want.y, 1e-4f);
    EXPECT_NEAR(e.worldPos.z, want.z, 1e-4f);
}

// =============================================================================================
// WHAT MERGING MUST NOT LOSE: a bed of coals outshines one spark
// =============================================================================================

TEST_F(EmissiveLightMerge, ReachGrowsWithTheEmittingVolume) {
    auto reachOf = [](int n) {
        auto micros = glowMicrosInCube(glm::ivec3(0, 0, 0), n);
        ChunkRenderManager crm;
        crm.rebuildAllFaces(Cubes{}, Subs{}, micros, glm::ivec3(0, 0, 0));
        EXPECT_EQ(crm.getEmissiveLights().size(), 1u);
        return crm.getEmissiveLights().empty() ? 0.0f : crm.getEmissiveLights()[0].radius;
    };
    const float one = reachOf(1), oven = reachOf(66), packed = reachOf(729);
    EXPECT_LT(one, oven) << "a 66-cell bed of coals must reach further than a single spark";
    EXPECT_LT(oven, packed) << "reach must keep growing with emitting volume";
    EXPECT_NEAR(one, 7.5f, 1e-3f) << "the single-cell case stays exactly as shipped";
    EXPECT_NEAR(packed, 15.0f, 1e-3f)
        << "a cube FULLY packed with glow microcubes is a glow cube, and must reach like one";
}

// Order must not wobble between rebuilds: RenderCoordinator hashes this list IN ORDER to decide
// whether to re-register, and LightManager's stable selection tie-breaks on the resulting ids.
TEST_F(EmissiveLightMerge, TheEmittedListIsDeterministic) {
    auto build = []() {
        auto micros = glowMicrosInCube(glm::ivec3(7, 1, 2), 30);
        for (auto& m : glowMicrosInCube(glm::ivec3(2, 9, 4), 15, "glow_blue")) micros.push_back(std::move(m));
        for (auto& m : glowMicrosInCube(glm::ivec3(5, 5, 5), 9, "glow_green")) micros.push_back(std::move(m));
        ChunkRenderManager crm;
        crm.rebuildAllFaces(Cubes{}, Subs{}, micros, glm::ivec3(0, 0, 0));
        return crm.getEmissiveLights();
    };
    const auto a = build(), b = build();
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i].worldPos.x, b[i].worldPos.x) << "light " << i << " moved between rebuilds";
        EXPECT_FLOAT_EQ(a[i].worldPos.y, b[i].worldPos.y);
        EXPECT_FLOAT_EQ(a[i].worldPos.z, b[i].worldPos.z);
        EXPECT_FLOAT_EQ(a[i].radius, b[i].radius);
    }
}

// =============================================================================================
// THE EMITTER'S OWN SIZE (G-157) -- what the shadow march is allowed to exclude around a light
// =============================================================================================

// A light must report the half-extent of its own emitting body, because `phxLightVisibility`
// excludes that much solid around the light so an emissive voxel does not shadow itself. Before
// this existed the march walked the whole contiguous solid run, so a sconce mounted flush on a
// wall excluded the WALL as well and lit straight through it at every thickness.
TEST_F(EmissiveLightMerge, ALoneMicrocubeReportsItsOwnTinyExtent) {
    Micros micros;
    micros.push_back(std::make_unique<Microcube>(glm::ivec3(2, 2, 2), glm::ivec3(1, 1, 1),
                                                 glm::ivec3(1, 1, 1), "glow"));
    ChunkRenderManager crm;
    crm.rebuildAllFaces(Cubes{}, Subs{}, micros, glm::ivec3(0, 0, 0));
    ASSERT_EQ(crm.getEmissiveLights().size(), 1u);
    // Bounding sphere of a 1/9 cube: sqrt(3)/18.
    const float want = std::sqrt(3.0f) / 18.0f;
    EXPECT_NEAR(crm.getEmissiveLights()[0].extent, want, 1e-4f)
        << "a one-microcube flame must exclude ~0.096 u of solid, not a whole wall";
    // The point of the number: a wall one micro cell away is FURTHER than the extent, so the
    // march still reaches it. Without this the fixture proves nothing about the leak.
    EXPECT_LT(crm.getEmissiveLights()[0].extent, 1.0f / 9.0f)
        << "extent must be smaller than one micro cell, or a 1-micro wall is still swallowed";
}

TEST_F(EmissiveLightMerge, AGlowCubeReportsHalfItsDiagonal) {
    Cubes cubes;
    cubes.push_back(std::make_unique<Cube>(glm::ivec3(3, 3, 3), "glow"));
    ChunkRenderManager crm;
    crm.rebuildAllFaces(cubes, Subs{}, Micros{}, glm::ivec3(0, 0, 0));
    ASSERT_EQ(crm.getEmissiveLights().size(), 1u);
    EXPECT_NEAR(crm.getEmissiveLights()[0].extent, std::sqrt(3.0f) / 2.0f, 1e-4f)
        << "a full glow cube must exclude exactly itself -- half its diagonal";
}

// A merged fixture's extent must COVER all of its cells, or the fixture shadows its own far side.
TEST_F(EmissiveLightMerge, AMergedFixtureExtentCoversAllOfItsCells) {
    const glm::ivec3 cube(1, 1, 1);
    auto micros = glowMicrosInCube(cube, 66);
    ChunkRenderManager crm;
    crm.rebuildAllFaces(Cubes{}, Subs{}, micros, glm::ivec3(0, 0, 0));
    ASSERT_EQ(crm.getEmissiveLights().size(), 1u);
    const auto& e = crm.getEmissiveLights()[0];

    // The requirement is that the extent reaches the furthest POINT of any emitting cell, so
    // measure the furthest CORNER of each cell -- not its bounding sphere, which would overstate
    // the requirement for any cell that is not diagonally aligned with the centroid.
    float worst = 0.0f;
    for (const auto& m : micros) {
        const glm::vec3 c = m->getWorldPosition() + glm::vec3(m->getScale() * 0.5f);
        const float half = m->getScale() * 0.5f;
        for (int k = 0; k < 8; ++k) {
            const glm::vec3 corner = c + glm::vec3((k & 1) ? half : -half,
                                                   (k & 2) ? half : -half,
                                                   (k & 4) ? half : -half);
            worst = std::max(worst, glm::length(corner - e.worldPos));
        }
    }
    EXPECT_GE(e.extent, worst - 1e-3f)
        << "the extent must reach every emitting cell (" << e.extent << " < " << worst
        << "), or the bed of coals casts a shadow from its own far side";
    EXPECT_LT(e.extent, std::sqrt(3.0f) / 2.0f + 1e-3f)
        << "and must never exceed the parent cube it was merged within";
}
