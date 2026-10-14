// L1 duplicate-emitter merge (docs/PerfProgram2026-09.md §5 L1, §12).
//
// Every emissive subcube/microcube registered its OWN point light at its PARENT CUBE centre, so a
// chandelier cube with 8 glow micros uploaded 8 identical lights (measured live: a generated tavern
// registered 29 lights at 16 positions; 40-45% of uploaded slots were duplicates). Each duplicate runs
// its own per-fragment occupancy march.
//
// The merge key is (world cube cell, radius). Lights in one key group sit at the same position with
// the same radius, and shading is linear (lightColor * intensity * atten(dist, radius)), so one light
// with intensity = sum(t) and color = sum(c*t)/sum(t) shades identically. A cube cell lies in exactly
// one chunk, so the merge can never depend on the chunk grid.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <random>
#include <vector>

#include "core/Cube.h"
#include "core/MaterialRegistry.h"
#include "core/Microcube.h"
#include "core/Subcube.h"
#include "graphics/ChunkRenderManager.h"

using namespace Phyxel;
using namespace Phyxel::Graphics;

namespace {

std::vector<std::unique_ptr<Microcube>> glowMicros(const glm::ivec3& cell, int count, const char* mat = "glow") {
    std::vector<std::unique_ptr<Microcube>> v;
    for (int i = 0; i < count; ++i)
        v.push_back(std::make_unique<Microcube>(cell, glm::ivec3(i % 3, (i / 3) % 3, 1), glm::ivec3(1, 1, 1), mat));
    return v;
}

const std::vector<ChunkRenderManager::EmissiveLight>& mesh(ChunkRenderManager& crm,
                                                            std::vector<std::unique_ptr<Subcube>>& subs,
                                                            std::vector<std::unique_ptr<Microcube>>& micros,
                                                            const glm::ivec3& origin = glm::ivec3(0)) {
    std::vector<std::unique_ptr<Cube>> cubes;
    crm.rebuildAllFaces(cubes, subs, micros, origin);
    return crm.getEmissiveLights();
}

// The single-emitter reference: one glow micro alone in a cell.
ChunkRenderManager::EmissiveLight single(const char* mat = "glow") {
    ChunkRenderManager crm;
    std::vector<std::unique_ptr<Subcube>> subs;
    auto micros = glowMicros(glm::ivec3(4, 4, 4), 1, mat);
    const auto& l = mesh(crm, subs, micros);
    EXPECT_EQ(l.size(), 1u);
    return l.empty() ? ChunkRenderManager::EmissiveLight{} : l[0];
}

}  // namespace

// Emissiveness comes from resources/materials.json. Without the registry loaded, getMaterial("glow")
// returns null and the scan registers NOTHING, which reads as "0 lights" rather than as a failure.
// The first run of these tests hit exactly that, so the load is asserted, not assumed.
class EmitterMergeTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string path = "resources/materials.json";
        for (const char* p : {"resources/materials.json", "../resources/materials.json",
                              "../../resources/materials.json", "../../../resources/materials.json"})
            if (std::filesystem::exists(p)) { path = p; break; }
        ASSERT_TRUE(Phyxel::Core::MaterialRegistry::instance().loadFromJson(path)) << "materials.json not found";
        const auto* glow = Phyxel::Core::MaterialRegistry::instance().getMaterial("glow");
        ASSERT_NE(glow, nullptr);
        ASSERT_TRUE(glow->emissive) << "the rig relies on 'glow' being emissive";
    }
};

// WHAT THESE PIN (2026-10-14). Two sessions removed the same duplicates independently: upstream's
// L1 post-pass (76eb3385) merged coincident lights by SUMMING intensity, and G-158 (f4024b42,
// three days earlier, look confirmed by the user) accumulates per PARENT CUBE and emits one light
// at the power-weighted centroid with intensity = the BRIGHTEST constituent (66 glow cells are one
// bed of coals, not 66 torches; summing was the over-brightness bug) and reach from the emitting
// VOLUME. G-158 runs first, so L1 sees at most one light per cube and is a no-op guard. The tests
// below assert G-158's contract; their L1 forms (sum, cell centre, radii kept separate) are gone.

// A chandelier cube: 8 glow micros in one cell become ONE light, as bright as one of them, reaching
// further than one of them, centred on the glowing cells rather than on the cube.
TEST_F(EmitterMergeTest, CoincidentMicrosInOneCellBecomeOneLight) {
    const auto one = single();
    ChunkRenderManager crm;
    std::vector<std::unique_ptr<Subcube>> subs;
    auto micros = glowMicros(glm::ivec3(3, 19, 6), 8);
    const auto& lights = mesh(crm, subs, micros);
    ASSERT_EQ(lights.size(), 1u) << "8 glow micros in one cube cell must register ONE light, not 8";
    EXPECT_FLOAT_EQ(lights[0].intensity, one.intensity) << "brightest constituent, not the sum (G-158)";
    EXPECT_GT(lights[0].radius, one.radius) << "8 cells of emitting volume reach further than 1";
    EXPECT_LT(lights[0].radius, 15.0f * one.intensity) << "but less than a full glow cube";
    // The centroid of the 8 micro centres: glowMicros puts micro i in SUBCUBE (i%3, i/3%3, 1) at
    // micro slot (1,1,1), so its centre is cell + (sub*3 + 1 + 0.5) / 9 on each axis.
    glm::vec3 centroid(0.0f);
    for (int i = 0; i < 8; ++i)
        centroid += glm::vec3(3.0f, 19.0f, 6.0f) + (glm::vec3(i % 3, (i / 3) % 3, 1) * 3.0f + 1.5f) / 9.0f;
    centroid /= 8.0f;
    EXPECT_NEAR(glm::length(lights[0].worldPos - centroid), 0.0f, 1e-5f) << "centre of EMISSION, not of the cube";
    EXPECT_NEAR(lights[0].color.r, one.color.r, 1e-6);
    EXPECT_NEAR(lights[0].color.g, one.color.g, 1e-6);
    EXPECT_NEAR(lights[0].color.b, one.color.b, 1e-6);
    // The G-157 extent covers the emitting cells: at least the half-width of a micro, well under a cube's.
    EXPECT_GT(lights[0].extent, 0.05f);
    EXPECT_LT(lights[0].extent, 0.866f);
}

// A mixed-hue cell: the hue is the POWER-WEIGHTED mix of its members and the brightness stays
// that of the brightest member - it does not add up to a light 8x as bright.
TEST_F(EmitterMergeTest, MergedLightMixesHueByPowerAndKeepsTheBrightestIntensity) {
    const auto warm = single("glow");
    const auto blue = single("glow_blue");
    ASSERT_FLOAT_EQ(warm.radius, blue.radius);
    ASSERT_FLOAT_EQ(warm.intensity, blue.intensity);
    ChunkRenderManager crm;
    std::vector<std::unique_ptr<Subcube>> subs;
    auto micros = glowMicros(glm::ivec3(5, 5, 5), 4, "glow");
    auto blues = glowMicros(glm::ivec3(5, 5, 5), 4, "glow_blue");
    for (auto& b : blues) {
        b->setMicrocubeLocalPosition(glm::ivec3(0, 0, 0));   // distinct micro slots from the warm ones
        micros.push_back(std::move(b));
    }
    const auto& lights = mesh(crm, subs, micros);
    ASSERT_EQ(lights.size(), 1u);
    EXPECT_FLOAT_EQ(lights[0].intensity, warm.intensity);
    const glm::vec3 want = 0.5f * warm.color + 0.5f * blue.color;   // equal power each side
    EXPECT_NEAR(lights[0].color.r, want.r, 1e-5);
    EXPECT_NEAR(lights[0].color.g, want.g, 1e-5);
    EXPECT_NEAR(lights[0].color.b, want.b, 1e-5);
}

// A subcube and three micros in one cell are ONE fixture: one light, whose reach grows with the
// combined emitting volume - at least the subcube's alone, more than a micro's.
TEST_F(EmitterMergeTest, MixedResolutionsInOneCellAreOneFixture) {
    const auto micro = single();
    ChunkRenderManager ref;
    std::vector<std::unique_ptr<Subcube>> refSubs;
    refSubs.push_back(std::make_unique<Subcube>(glm::ivec3(6, 6, 6), glm::ivec3(0, 0, 0), "glow"));
    std::vector<std::unique_ptr<Microcube>> none;
    const auto subAlone = mesh(ref, refSubs, none);
    ASSERT_EQ(subAlone.size(), 1u);

    ChunkRenderManager crm;
    std::vector<std::unique_ptr<Subcube>> subs;
    subs.push_back(std::make_unique<Subcube>(glm::ivec3(6, 6, 6), glm::ivec3(0, 0, 0), "glow"));
    auto micros = glowMicros(glm::ivec3(6, 6, 6), 3);
    const auto& lights = mesh(crm, subs, micros);
    ASSERT_EQ(lights.size(), 1u) << "one cube cell = one light, whatever resolutions emit in it";
    EXPECT_GE(lights[0].radius, subAlone[0].radius);
    EXPECT_GT(lights[0].radius, micro.radius);
    EXPECT_FLOAT_EQ(lights[0].intensity, subAlone[0].intensity);
}

// Output order is a pure function of the voxels: shuffling the input changes nothing.
TEST_F(EmitterMergeTest, OrderIndependent) {
    auto build = [](unsigned seed) {
        std::vector<std::unique_ptr<Microcube>> micros;
        for (int cell = 0; cell < 6; ++cell) {
            auto m = glowMicros(glm::ivec3(cell * 2, 3, 7), 1 + cell % 4, cell % 2 ? "glow" : "glow_blue");
            for (auto& x : m) micros.push_back(std::move(x));
        }
        std::mt19937 rng(seed);
        std::shuffle(micros.begin(), micros.end(), rng);
        ChunkRenderManager crm;
        std::vector<std::unique_ptr<Subcube>> subs;
        return std::vector<ChunkRenderManager::EmissiveLight>(mesh(crm, subs, micros));
    };
    const auto a = build(1), b = build(99);
    ASSERT_EQ(a.size(), 6u);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].worldPos, b[i].worldPos);
        EXPECT_FLOAT_EQ(a[i].intensity, b[i].intensity);
        EXPECT_FLOAT_EQ(a[i].radius, b[i].radius);
    }
}

// Chunked vs whole: the same five-micro pattern in the two cube cells either side of a chunk seam
// gives exactly one light per cell, whichever chunk meshes them, and the right-hand light is the
// left-hand one shifted by exactly one cube. A cube cell belongs to one chunk, so nothing can straddle.
TEST_F(EmitterMergeTest, ChunkSplitInvariant) {
    ChunkRenderManager left, right;
    std::vector<std::unique_ptr<Subcube>> subs;
    auto l = glowMicros(glm::ivec3(31, 2, 2), 5);
    auto r = glowMicros(glm::ivec3(32, 2, 2), 5);
    const auto& ll = mesh(left, subs, l, glm::ivec3(0, 0, 0));
    const auto& rl = mesh(right, subs, r, glm::ivec3(32, 0, 0));
    ASSERT_EQ(ll.size(), 1u);
    ASSERT_EQ(rl.size(), 1u);
    EXPECT_EQ(glm::ivec3(glm::floor(ll[0].worldPos)), glm::ivec3(31, 2, 2)) << "the light lies in its cell";
    EXPECT_EQ(glm::ivec3(glm::floor(rl[0].worldPos)), glm::ivec3(32, 2, 2));
    EXPECT_NEAR(glm::length(rl[0].worldPos - (ll[0].worldPos + glm::vec3(1, 0, 0))), 0.0f, 1e-5f);
    EXPECT_FLOAT_EQ(ll[0].intensity, rl[0].intensity);
    EXPECT_FLOAT_EQ(ll[0].radius, rl[0].radius);
    EXPECT_NEAR(ll[0].extent, rl[0].extent, 1e-4f);   // float rounding at |x| 31 vs 32
}
