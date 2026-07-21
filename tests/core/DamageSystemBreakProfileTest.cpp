/**
 * Phase 0 (docs/DestructionSystemV2.md §5.A) — data-driven material break model.
 *
 * Proves DamageSystem::responseFor is driven by the "break" block in materials.json
 * (not hardcoded C++), with a bondStrength-derived fallback for materials that have
 * no "break" block. Red-before-green: with the JSON parse wiring absent, Stone's
 * break profile is unparsed (hasProfile=false) so responseFor("Stone") returns the
 * FALLBACK (bondStrength 0.9 * 120 = 108), and this test's EXPECT of 110 FAILS. Once
 * MaterialRegistry parses the "break" block, hasProfile=true and it returns 110.
 */

#include <gtest/gtest.h>
#include "core/DamageSystem.h"
#include "core/MaterialRegistry.h"
#include <cmath>
#include <filesystem>
#include <vector>
#include <glm/glm.hpp>

using namespace Phyxel;
using Phyxel::Core::MaterialRegistry;

namespace {

std::string findMaterialsJson() {
    for (const auto& path : {
        "resources/materials.json",
        "../resources/materials.json",
        "../../resources/materials.json",
        "../../../resources/materials.json"
    }) {
        if (std::filesystem::exists(path)) return path;
    }
    return "resources/materials.json";
}

class DamageSystemBreakProfileTest : public ::testing::Test {
protected:
    void SetUp() override {
        // responseFor() reads MaterialRegistry::instance(), so load the singleton.
        loaded_ = MaterialRegistry::instance().loadFromJson(findMaterialsJson());
        ds_ = std::make_unique<DamageSystem>(nullptr, nullptr);
    }
    std::unique_ptr<DamageSystem> ds_;
    bool loaded_ = false;
};

} // namespace

// ---- Parse: the "break" block reaches MaterialDef ----

TEST_F(DamageSystemBreakProfileTest, ParsesBreakBlockForExemplars) {
    ASSERT_TRUE(loaded_);
    const auto* stone = MaterialRegistry::instance().getMaterial("Stone");
    ASSERT_NE(stone, nullptr);
    EXPECT_TRUE(stone->breakProfile.hasProfile);
    EXPECT_FLOAT_EQ(stone->breakProfile.toughness,  110.0f);
    EXPECT_FLOAT_EQ(stone->breakProfile.brittleS1,  1.8f);
    EXPECT_FLOAT_EQ(stone->breakProfile.brittleS2,  4.0f);
    EXPECT_FLOAT_EQ(stone->breakProfile.absorption, 0.9f);
}

TEST_F(DamageSystemBreakProfileTest, MaterialWithoutBreakBlockHasNoProfile) {
    ASSERT_TRUE(loaded_);
    const auto* grass = MaterialRegistry::instance().getMaterial("Grass");
    ASSERT_NE(grass, nullptr);
    EXPECT_FALSE(grass->breakProfile.hasProfile);
}

// ---- Consume: responseFor returns the JSON values (the data-driven contract) ----

TEST_F(DamageSystemBreakProfileTest, ResponseForReadsJsonProfile) {
    ASSERT_TRUE(loaded_);
    // These come from materials.json "break" blocks, not C++.
    auto stone = ds_->responseFor("Stone");
    EXPECT_FLOAT_EQ(stone.toughness,  110.0f);   // fallback would be 0.9*120 = 108 (RED)
    EXPECT_FLOAT_EQ(stone.s1,         1.8f);
    EXPECT_FLOAT_EQ(stone.s2,         4.0f);
    EXPECT_FLOAT_EQ(stone.absorption, 0.9f);

    auto glass = ds_->responseFor("Glass");
    EXPECT_FLOAT_EQ(glass.toughness,  35.0f);
    EXPECT_FLOAT_EQ(glass.absorption, 0.3f);

    auto metal = ds_->responseFor("Metal");
    EXPECT_FLOAT_EQ(metal.toughness,  200.0f);
    EXPECT_FLOAT_EQ(metal.s2,         11.0f);
}

TEST_F(DamageSystemBreakProfileTest, ResponseForTracksMaterialDefProfile) {
    ASSERT_TRUE(loaded_);
    // Tie responseFor's output to the parsed MaterialDef so the two can't drift.
    for (const char* name : {"Stone", "Glass", "Wood", "Metal", "Dirt"}) {
        const auto* def = MaterialRegistry::instance().getMaterial(name);
        ASSERT_NE(def, nullptr) << name;
        ASSERT_TRUE(def->breakProfile.hasProfile) << name;
        auto r = ds_->responseFor(name);
        EXPECT_FLOAT_EQ(r.toughness,  def->breakProfile.toughness)  << name;
        EXPECT_FLOAT_EQ(r.s1,         def->breakProfile.brittleS1)  << name;
        EXPECT_FLOAT_EQ(r.s2,         def->breakProfile.brittleS2)  << name;
        EXPECT_FLOAT_EQ(r.absorption, def->breakProfile.absorption) << name;
    }
}

// ---- Fallback: no "break" block → bondStrength-derived toughness ----

TEST_F(DamageSystemBreakProfileTest, FallbackUsesBondStrength) {
    ASSERT_TRUE(loaded_);
    const auto* grass = MaterialRegistry::instance().getMaterial("Grass");
    ASSERT_NE(grass, nullptr);
    ASSERT_FALSE(grass->breakProfile.hasProfile);
    float expected = std::max(0.05f, grass->physics.bondStrength) * 120.0f;
    auto r = ds_->responseFor("Grass");
    EXPECT_FLOAT_EQ(r.toughness,  expected);
    EXPECT_FLOAT_EQ(r.s1,         2.5f);
    EXPECT_FLOAT_EQ(r.s2,         6.0f);
    EXPECT_FLOAT_EQ(r.absorption, 0.6f);
}

TEST_F(DamageSystemBreakProfileTest, UnknownMaterialGetsSafeFallback) {
    ASSERT_TRUE(loaded_);
    auto r = ds_->responseFor("NoSuchMaterialXYZ");
    EXPECT_FLOAT_EQ(r.toughness, 0.5f * 120.0f);  // bond default 0.5
    EXPECT_GT(r.toughness, 0.0f);
}

// ---- Stress: every material yields a finite, valid response ----

TEST_F(DamageSystemBreakProfileTest, AllMaterialsProduceFiniteResponse) {
    ASSERT_TRUE(loaded_);
    const auto& all = MaterialRegistry::instance().getAllMaterials();
    ASSERT_FALSE(all.empty());
    for (const auto& def : all) {
        auto r = ds_->responseFor(def.name);
        EXPECT_TRUE(std::isfinite(r.toughness))  << def.name;
        EXPECT_TRUE(std::isfinite(r.s1))         << def.name;
        EXPECT_TRUE(std::isfinite(r.s2))         << def.name;
        EXPECT_TRUE(std::isfinite(r.absorption)) << def.name;
        EXPECT_GT(r.toughness, 0.0f)  << def.name;
        EXPECT_GT(r.s1, 0.0f)         << def.name;
        EXPECT_GT(r.s2, r.s1)         << def.name;  // microcube threshold above subcube
        EXPECT_GE(r.absorption, 0.0f) << def.name;
    }
}

// ---- §15.6 C: satellite consolidation (the "floating branches" fix) ----
// A felled tree sheds its micro-thin branch tips as separate tiny components (their wood
// isn't cube-connected to the trunk), each of which perches on the pile as a floating
// branch. consolidateSatelliteComponents folds small, spatially-CONTAINED tips into the
// crown while leaving genuinely distinct pieces (a far chunk, a second large body) alone.
TEST(DamageSystemSatelliteConsolidation, FoldsContainedTipsKeepsDistinctPieces) {
    using glm::ivec3;
    std::vector<std::vector<ivec3>> comps;

    // Crown: 5x5x5 = 125 cells at the origin, bounds [0,4]^3 (expanded [-2,6]^3).
    std::vector<ivec3> crown;
    for (int x = 0; x < 5; ++x)
        for (int y = 0; y < 5; ++y)
            for (int z = 0; z < 5; ++z) crown.push_back({x, y, z});
    comps.push_back(crown);

    comps.push_back({{2, 6, 2}, {2, 6, 3}});          // tip A: 2 cells INSIDE expanded bounds -> folds in
    comps.push_back({{50, 0, 0}});                     // tip B: 1 cell FAR away -> stays
    std::vector<ivec3> other;                          // a SECOND large block far off -> stays (distinct)
    for (int x = 0; x < 5; ++x)
        for (int y = 0; y < 5; ++y)
            for (int z = 0; z < 5; ++z) other.push_back({40 + x, y, z});
    comps.push_back(other);

    DamageSystem::consolidateSatelliteComponents(comps);

    // Tip A folded into the crown (125 -> 127); tip B and the distant block survive: 4 -> 3.
    ASSERT_EQ(comps.size(), 3u) << "contained tip should have folded into the crown";
    size_t maxSz = 0, ones = 0, fives = 0;
    for (const auto& c : comps) {
        maxSz = std::max(maxSz, c.size());
        if (c.size() == 1u)   ++ones;    // tip B intact
        if (c.size() == 125u) ++fives;   // the distinct far block, untouched
    }
    EXPECT_EQ(maxSz, 127u) << "crown should have absorbed the 2-cell tip";
    EXPECT_EQ(ones, 1u)    << "the far single-cell tip must NOT be folded in";
    EXPECT_EQ(fives, 1u)   << "the distinct far block must NOT be folded in";
}
