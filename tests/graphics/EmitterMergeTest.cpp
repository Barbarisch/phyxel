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

// A chandelier cube: 8 glow micros in one cell become ONE light carrying 8x the intensity.
TEST_F(EmitterMergeTest, CoincidentMicrosInOneCellBecomeOneLight) {
    const auto one = single();
    ChunkRenderManager crm;
    std::vector<std::unique_ptr<Subcube>> subs;
    auto micros = glowMicros(glm::ivec3(3, 19, 6), 8);
    const auto& lights = mesh(crm, subs, micros);
    ASSERT_EQ(lights.size(), 1u) << "8 glow micros in one cube cell must register ONE light, not 8";
    EXPECT_FLOAT_EQ(lights[0].intensity, 8.0f * one.intensity);
    EXPECT_FLOAT_EQ(lights[0].radius, one.radius);
    EXPECT_EQ(lights[0].worldPos, glm::vec3(3.5f, 19.5f, 6.5f));
    EXPECT_NEAR(lights[0].color.r, one.color.r, 1e-6);
    EXPECT_NEAR(lights[0].color.g, one.color.g, 1e-6);
    EXPECT_NEAR(lights[0].color.b, one.color.b, 1e-6);
}

// Shading is linear in color*intensity, so a mixed-hue cell must merge to the SUM of its members.
TEST_F(EmitterMergeTest, MergedLightShadesAsTheSumOfItsMembers) {
    const auto warm = single("glow");
    const auto blue = single("glow_blue");
    ASSERT_FLOAT_EQ(warm.radius, blue.radius);
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
    const glm::vec3 want = 4.0f * warm.color * warm.intensity + 4.0f * blue.color * blue.intensity;
    const glm::vec3 got = lights[0].color * lights[0].intensity;
    EXPECT_NEAR(got.r, want.r, 1e-5);
    EXPECT_NEAR(got.g, want.g, 1e-5);
    EXPECT_NEAR(got.b, want.b, 1e-5);
}

// Different radius scales are different lights (a subcube emitter reaches further than a micro).
TEST_F(EmitterMergeTest, DifferentRadiiInOneCellStaySeparate) {
    ChunkRenderManager crm;
    std::vector<std::unique_ptr<Subcube>> subs;
    subs.push_back(std::make_unique<Subcube>(glm::ivec3(6, 6, 6), glm::ivec3(0, 0, 0), "glow"));
    auto micros = glowMicros(glm::ivec3(6, 6, 6), 3);
    const auto& lights = mesh(crm, subs, micros);
    ASSERT_EQ(lights.size(), 2u);
    EXPECT_NE(lights[0].radius, lights[1].radius);
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

// Chunked vs whole: emitters in the two cube cells either side of a chunk seam give exactly one light
// per cell, whichever chunk meshes them. A cube cell belongs to one chunk, so nothing can straddle.
TEST_F(EmitterMergeTest, ChunkSplitInvariant) {
    ChunkRenderManager left, right;
    std::vector<std::unique_ptr<Subcube>> subs;
    auto l = glowMicros(glm::ivec3(31, 2, 2), 5);
    auto r = glowMicros(glm::ivec3(32, 2, 2), 5);
    const auto& ll = mesh(left, subs, l, glm::ivec3(0, 0, 0));
    const auto& rl = mesh(right, subs, r, glm::ivec3(32, 0, 0));
    ASSERT_EQ(ll.size(), 1u);
    ASSERT_EQ(rl.size(), 1u);
    EXPECT_EQ(ll[0].worldPos, glm::vec3(31.5f, 2.5f, 2.5f));
    EXPECT_EQ(rl[0].worldPos, glm::vec3(32.5f, 2.5f, 2.5f));
}
