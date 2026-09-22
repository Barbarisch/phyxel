/**
 * R1a -- damage state and stage quantization (docs/VoxelDamageVisualization.md 6.1).
 *
 * L2, unit. Everything here is DamageSystem / Cube / the shared quantization, so it lives
 * in tests/core; the JSON-asserting half (R1b) cannot, because tests/CMakeLists.txt links
 * phyxel_core only while /api/world/voxel lives in the editor target (6.0).
 *
 * RED-BEFORE-GREEN, and which test is which:
 *   - AccumulationTracksSubThresholdHits / StageAdvancesAsDamageAccumulates /
 *     StageIsClampedBelowTheVariedFlagBit / StageChangePredicate*  -- characterize the
 *     contract P0 introduces; they go green with DamageStage.h.
 *   - NormalizationIsRelativeToMaterialToughness -- THE MEANINGFUL RED. It stays FAILING
 *     for the whole of P0 by design and goes green at P1 (3.2). Per 7's phase gate, P0 is
 *     not done unless this is still red: a green here during P0 would mean the test is not
 *     actually measuring normalization.
 */

#include <gtest/gtest.h>

#include "core/Cube.h"
#include "core/DamageStage.h"
#include "core/DamageSystem.h"
#include "core/MaterialRegistry.h"

#include <filesystem>
#include <glm/glm.hpp>
#include <memory>

using Phyxel::DamageSystem;
using Phyxel::Cube;
using Phyxel::Core::MaterialRegistry;
using Phyxel::Core::damageStage;
using Phyxel::Core::damageStageChanged;
using Phyxel::Core::kDamageDisplayRef;
using Phyxel::Core::kDamageStageMax;

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

class VoxelDamageStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        loaded_ = MaterialRegistry::instance().loadFromJson(findMaterialsJson());
        // responseFor() is pure w.r.t. the registry, so no ChunkManager is needed.
        ds_ = std::make_unique<DamageSystem>(nullptr, nullptr);
    }
    std::unique_ptr<DamageSystem> ds_;
    bool loaded_ = false;
};

} // namespace

// ---------------------------------------------------------------------------
// Accumulation: sub-threshold hits add up and leave the voxel intact.
// ---------------------------------------------------------------------------

TEST_F(VoxelDamageStateTest, AccumulationTracksSubThresholdHits) {
    ASSERT_TRUE(loaded_);
    const float toughness = ds_->responseFor("Stone").toughness;
    ASSERT_GT(toughness, 0.0f);

    Cube c(glm::ivec3(8, 17, 8), "Stone");
    EXPECT_FLOAT_EQ(c.getAccumulatedDamage(), 0.0f) << "a fresh voxel must read pristine";

    // Three hits at 0.3x toughness: 0.9x total, so it accumulates and never breaks.
    const float hit = 0.3f * toughness;
    for (int i = 1; i <= 3; ++i) {
        c.addDamage(hit);
        EXPECT_NEAR(c.getAccumulatedDamage(), hit * static_cast<float>(i), 1e-3f)
            << "accumulated damage must track the sum of hits after hit " << i;
        EXPECT_LT(c.getAccumulatedDamage(), toughness)
            << "0.9x toughness total must stay under the break threshold";
    }

    c.resetDamage();
    EXPECT_FLOAT_EQ(c.getAccumulatedDamage(), 0.0f);
}

// ---------------------------------------------------------------------------
// Quantization: the value the mesher packs into instance bits 11-14.
// ---------------------------------------------------------------------------

TEST_F(VoxelDamageStateTest, StageAdvancesAsDamageAccumulates) {
    // Against the shipped display reference, stage is a plain fraction of kDamageDisplayRef.
    EXPECT_EQ(damageStage(0.0f,                      kDamageDisplayRef), 0);
    EXPECT_EQ(damageStage(kDamageDisplayRef * 0.5f,  kDamageDisplayRef), 8);   // 7.5 -> 8
    EXPECT_EQ(damageStage(kDamageDisplayRef,         kDamageDisplayRef), 15);

    // Monotonic: more damage never reads as less damaged.
    uint8_t prev = 0;
    for (int i = 0; i <= 20; ++i) {
        const float d = kDamageDisplayRef * (static_cast<float>(i) / 20.0f);
        const uint8_t s = damageStage(d, kDamageDisplayRef);
        EXPECT_GE(s, prev) << "stage went backwards at damage " << d;
        prev = s;
    }
}

TEST_F(VoxelDamageStateTest, StageIsClampedBelowTheVariedFlagBit) {
    // Bit 15 of the instance `reserved` word is the `varied` texture-rotation flag. A stage
    // of 16+ would overflow bits 11-14 into it and silently hash-rotate the face texture.
    EXPECT_LE(damageStage(kDamageDisplayRef * 1000.0f, kDamageDisplayRef), kDamageStageMax);
    EXPECT_LE(damageStage(1e30f, kDamageDisplayRef), kDamageStageMax);
    EXPECT_EQ(damageStage(-5.0f, kDamageDisplayRef), 0) << "negative damage must read pristine";
    EXPECT_EQ(damageStage(10.0f, 0.0f), 0) << "a zero denominator must not divide by zero";
}

TEST_F(VoxelDamageStateTest, StageRespectsACoarserStageCount) {
    // P2 (3.5) lowers the emitted stage count without narrowing the 4-bit field.
    EXPECT_EQ(damageStage(0.0f,                     kDamageDisplayRef, 3), 0);
    EXPECT_EQ(damageStage(kDamageDisplayRef,        kDamageDisplayRef, 3), 3);
    EXPECT_LE(damageStage(kDamageDisplayRef * 0.5f, kDamageDisplayRef, 3), 3);
}

// ---------------------------------------------------------------------------
// 3.7's re-mesh decision, as a pure predicate (the ChunkManager wiring is covered
// in tests/integration -- ChunkManagerTestFixture needs Vulkan, so it cannot build here).
// ---------------------------------------------------------------------------

TEST_F(VoxelDamageStateTest, StageChangePredicateFiresOnlyOnACrossing) {
    const float ref = kDamageDisplayRef;
    const float perStage = ref / static_cast<float>(kDamageStageMax);

    // A hit well inside one stage band changes no pixels -> must NOT request a re-mesh.
    EXPECT_FALSE(damageStageChanged(perStage * 4.10f, perStage * 4.20f, ref))
        << "a graze that stays inside one stage band must not dirty the chunk";

    // A hit that crosses a band boundary must request one.
    EXPECT_TRUE(damageStageChanged(0.0f, perStage * 1.5f, ref))
        << "a graze that crosses a stage boundary must dirty the chunk";
}

TEST_F(VoxelDamageStateTest, StageChangePredicateIgnoresDamageAboveFullyDamaged) {
    // Past the display reference every voxel reads at stageMax, so further grazes are
    // invisible and must not keep re-meshing.
    const float ref = kDamageDisplayRef;
    EXPECT_FALSE(damageStageChanged(ref * 1.5f, ref * 9.0f, ref));
}

// ---------------------------------------------------------------------------
// THE P0 RED (3.2 / 7 P1 gate). Expected to FAIL for all of P0.
// ---------------------------------------------------------------------------

TEST_F(VoxelDamageStateTest, NormalizationIsRelativeToMaterialToughness) {
    ASSERT_TRUE(loaded_);

    // Asserted through DamageSystem::displayStage -- the SAME entry point the mesher and the
    // /api/world/voxel readback resolve through. Calling damageStage() with a hand-picked
    // denominator here would be circular: it would test this test's arithmetic, not the
    // engine's choice of denominator.
    const float stoneT = DamageSystem::responseFor("Stone").toughness;   // 110 in materials.json
    const float glassT = DamageSystem::responseFor("Glass").toughness;   //  35
    ASSERT_GT(stoneT, 0.0f);
    ASSERT_GT(glassT, 0.0f);
    ASSERT_GT(stoneT, glassT) << "this test is meaningless unless the two differ";

    // Damage each to the SAME fraction of its OWN break toughness: equally close to failing.
    // The feature exists to communicate proximity to breaking, so they must display the same.
    for (float fraction : {0.25f, 0.5f, 0.75f, 1.0f}) {
        const uint8_t stoneStage = DamageSystem::displayStage("Stone", stoneT * fraction);
        const uint8_t glassStage = DamageSystem::displayStage("Glass", glassT * fraction);
        EXPECT_EQ(stoneStage, glassStage)
            << "at " << (fraction * 100.0f) << "% of their own toughness, Stone displays stage "
            << int(stoneStage) << " and Glass " << int(glassStage) << " -- a rendered stage must "
            << "mean the same fraction of the way to failure on every material";
    }

    // And the whole range is used, rather than saturating early. Under the old global
    // reference of 30, Stone pinned at max by 27% of the way to breaking and said nothing for
    // the remaining 73%.
    EXPECT_LT(DamageSystem::displayStage("Stone", stoneT * 0.30f), kDamageStageMax)
        << "Stone at 30% of the way to breaking must NOT already be at maximum display";
    EXPECT_EQ(DamageSystem::displayStage("Stone", stoneT), kDamageStageMax)
        << "a voxel at its break threshold must display the top stage";
}

TEST_F(VoxelDamageStateTest, DisplayStageAgreesWithTheEchoedDenominator) {
    ASSERT_TRUE(loaded_);
    // /api/world/voxel echoes `toughness` so a caller can reproduce damage01 and the stage
    // itself. Pin that promise: displayStage(mat, d) must equal damageStage(d, toughness).
    for (const char* mat : {"Stone", "Glass", "Wood", "Metal", "Dirt"}) {
        const float t = DamageSystem::responseFor(mat).toughness;
        ASSERT_GT(t, 0.0f) << mat;
        for (float frac : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f, 2.0f}) {
            EXPECT_EQ(DamageSystem::displayStage(mat, t * frac), damageStage(t * frac, t))
                << mat << " at " << frac << "x toughness: the echoed denominator must let a "
                << "caller reproduce the stage the engine renders";
        }
    }
}

TEST_F(VoxelDamageStateTest, UntouchedMaterialsStillFallBackToADerivedToughness) {
    ASSERT_TRUE(loaded_);
    // Only 7 of 108 materials carry a `break` block; the rest derive toughness from
    // bondStrength * 120 (DamageSystem.cpp). That fallback is a PRE-EXISTING open question
    // (DestructionSystemV2 #5) and 3.2 does not resolve it -- it only makes it visible. What
    // must hold regardless: every material resolves to a usable positive denominator, or the
    // display silently divides by zero and reads pristine forever.
    for (const char* mat : {"Sand", "Gravel", "Ice", "Leaf", "Sandstone", "Cobblestone"}) {
        EXPECT_GT(DamageSystem::responseFor(mat).toughness, 0.0f)
            << mat << " must have a positive damage-display denominator";
    }
    EXPECT_GT(DamageSystem::responseFor("NoSuchMaterialXyz").toughness, 0.0f)
        << "an unknown material must not produce a zero denominator";
}
