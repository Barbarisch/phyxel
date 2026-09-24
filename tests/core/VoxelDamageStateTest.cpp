/**
 * R1a -- damage state and stage quantization (docs/VoxelDamageVisualization.md §7).
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
#include <set>

using Phyxel::DamageSystem;
using Phyxel::Cube;
using Phyxel::Core::MaterialRegistry;
using Phyxel::Core::damageStage;
using Phyxel::Core::damageStageChanged;
using Phyxel::Core::kDamageDisplayRef;
using Phyxel::Core::kDamageStageMax;
using Phyxel::Core::kDamageStagesVisible;
using Phyxel::Core::packDamageStage;

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
// P2 (3.5) -- three VISIBLE stages, packed back across the full 4-bit field.
// ---------------------------------------------------------------------------

TEST_F(VoxelDamageStateTest, StageQuantizationAtEveryBoundary) {
    ASSERT_TRUE(loaded_);
    // The P2 gate: "stage mapping unit-tested at boundaries". damageStage ROUNDS, so band k
    // covers [(k-0.5)/n, (k+0.5)/n) of toughness -- the boundary sits at the MIDPOINT between
    // bands, not at a band edge.
    //
    // DERIVED from kDamageStagesVisible, never written as literals. The earlier version of this
    // test spelled the boundaries out by hand (1/6, 1/2, 5/6), which encoded "3 stages" in three
    // more places than the constant. When P4 (6.4) moved the count 3 -> 7 on measured evidence,
    // that table was simply wrong rather than informative -- a pinned-default test should
    // re-derive the contract, and fail only when the CONTRACT breaks.
    const float t   = DamageSystem::responseFor("Stone").toughness;
    const int   n   = kDamageStagesVisible;
    const float eps = 0.25f / float(n);          // a quarter-band: safely inside one band

    EXPECT_EQ(int(DamageSystem::displayStage("Stone", 0.0f)), 0) << "pristine";

    for (int k = 0; k < n; ++k) {
        const float boundary = (float(k) + 0.5f) / float(n);
        EXPECT_EQ(int(DamageSystem::displayStage("Stone", t * (boundary - eps))), k)
            << "just BELOW the stage " << k << "/" << (k + 1) << " boundary at "
            << boundary << "x toughness";
        EXPECT_EQ(int(DamageSystem::displayStage("Stone", t * (boundary + eps))), k + 1)
            << "just ABOVE the stage " << k << "/" << (k + 1) << " boundary at "
            << boundary << "x toughness";
    }

    // The first band is the one P4 cared about: a voxel shows NOTHING below 0.5/n of toughness,
    // so the stage count decides how much of a voxel's life is invisible. At 3 stages that was
    // the first 17%, which is why the count moved. Stated here so the cost of coarsening is
    // visible at the place it is defined, not only in the plan.
    EXPECT_EQ(int(DamageSystem::displayStage("Stone", t * (0.5f / float(n)) * 0.9f)), 0)
        << "damage below 0.5/n of toughness must render pristine -- this is the blind band";

    // Clamping at and beyond the break threshold: the top stage must hold, never wrap.
    EXPECT_EQ(int(DamageSystem::displayStage("Stone", t)), n) << "at the break threshold";
    EXPECT_EQ(int(DamageSystem::displayStage("Stone", t * 5.0f)), n)
        << "far past the break threshold -- must clamp, never wrap";
}

TEST_F(VoxelDamageStateTest, PackingSpreadsStagesAcrossTheFullFieldRange) {
    // voxel.frag divides the packed value by 15.0 and is NOT changed when the stage count
    // changes, so the visible stages must spread across the WHOLE 4-bit field. If packing
    // collapsed toward the bottom of the range, every crack would render fainter -- silently,
    // with no failing test and nothing in the log.
    //
    // Asserted as PROPERTIES of the spread rather than as a literal value list. The earlier
    // version pinned {0, 5, 10, 15}, which is the answer for 3 stages only; P4 moved the count
    // to 7 and those literals became noise. A pinned test that must be rewritten on every
    // legitimate change is one that eventually gets deleted instead.
    const int n = kDamageStagesVisible;

    EXPECT_EQ(int(packDamageStage(0)), 0) << "pristine must pack to exactly zero";
    EXPECT_EQ(int(packDamageStage(n)), kDamageStageMax)
        << "the top stage must reach the field maximum exactly, or a fully-damaged voxel never "
           "renders at full strength";

    int prev = -1;
    for (int k = 0; k <= n; ++k) {
        const int packed = int(packDamageStage(k));
        EXPECT_GT(packed, prev)
            << "stage " << k << " must pack STRICTLY above stage " << (k - 1)
            << ", or two visible stages render identically and one of them is wasted merge cost";
        EXPECT_LE(packed, kDamageStageMax)
            << "stage " << k << " overflows bits 11-14 into bit 15 (the varied flag)";
        prev = packed;
    }

    // Out of range must clamp, NEVER overflow into bit 15 (the `varied` texture-rotation flag).
    EXPECT_EQ(int(packDamageStage(99)), kDamageStageMax);
    EXPECT_EQ(int(packDamageStage(-4)), 0);
    EXPECT_LE(int(packDamageStage(kDamageStagesVisible * 10)), kDamageStageMax);
}

TEST_F(VoxelDamageStateTest, ExactlyOnePackedValuePerStageAcrossTheWholeDamageRange) {
    ASSERT_TRUE(loaded_);
    // This is the MERGE-COST property, stated as a test rather than trusted. Damage is part of
    // the greedy-merge key, so every distinct packed value is a potential merge-run break. A
    // blast produces a continuous damage gradient; what must NOT happen is that gradient
    // becoming many distinct values and shattering merge runs into thin bands.
    const float t = DamageSystem::responseFor("Stone").toughness;
    std::set<int> distinct;
    for (int i = 0; i <= 1000; ++i) {
        distinct.insert(int(DamageSystem::displayStageBits("Stone", t * (i / 1000.0f))));
    }
    EXPECT_EQ(distinct.size(), size_t(kDamageStagesVisible + 1))
        << "a continuous damage gradient must collapse to exactly kDamageStagesVisible + 1 "
           "distinct packed values (pristine plus each visible stage); more means more "
           "merge-run breaks and more faces, which is the cost P4 measured at 3.84x between "
           "15 stages and 3";
    EXPECT_EQ(*distinct.begin(), 0);
    EXPECT_EQ(*distinct.rbegin(), kDamageStageMax);
}

TEST_F(VoxelDamageStateTest, CoarseStagesBoundTheRemeshCountPerVoxel) {
    ASSERT_TRUE(loaded_);
    // 3.7 re-meshes a chunk only when a graze crosses a stage boundary, so the number of
    // rebuilds a single voxel can ever cause equals the number of boundaries it can cross.
    // Walk it from pristine to break in 1% steps and count transitions.
    const float t = DamageSystem::responseFor("Stone").toughness;
    int transitions = 0;
    uint8_t prev = DamageSystem::displayStage("Stone", 0.0f);
    for (int i = 1; i <= 100; ++i) {
        const uint8_t cur = DamageSystem::displayStage("Stone", t * (i / 100.0f));
        if (cur != prev) ++transitions;
        prev = cur;
    }
    EXPECT_EQ(transitions, kDamageStagesVisible)
        << "a voxel must cause at most " << kDamageStagesVisible << " re-meshes over its whole "
           "life; at the old 15 stages this was 15";
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
    //
    // NOTE the constant: kDamageStagesVisible (the number of stages the engine DISTINGUISHES),
    // never kDamageStageMax (the width of the 4-bit field it is packed into). Writing 15 here
    // is how this test broke at P2 -- it meant "the top stage" and said "the field maximum",
    // and those stopped being the same number the moment the stage count changed. Same class
    // of staleness as the hardcoded denominator this test carried before P1.
    EXPECT_LT(DamageSystem::displayStage("Stone", stoneT * 0.30f), kDamageStagesVisible)
        << "Stone at 30% of the way to breaking must NOT already be at maximum display";
    EXPECT_EQ(DamageSystem::displayStage("Stone", stoneT), kDamageStagesVisible)
        << "a voxel at its break threshold must display the top stage";
}

TEST_F(VoxelDamageStateTest, DisplayStageAgreesWithTheEchoedDenominator) {
    ASSERT_TRUE(loaded_);
    // /api/world/voxel echoes BOTH `toughness` and `damage_stages_max`, because reproducing
    // the rendered stage needs both: the denominator says how far along the voxel is, the stage
    // count says how finely that is reported. Echoing only the denominator was enough at P1 and
    // stopped being enough at P2 -- which is exactly why the response carries the stage count
    // rather than leaving callers to assume 15.
    for (const char* mat : {"Stone", "Glass", "Wood", "Metal", "Dirt"}) {
        const float t = DamageSystem::responseFor(mat).toughness;
        ASSERT_GT(t, 0.0f) << mat;
        for (float frac : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f, 2.0f}) {
            EXPECT_EQ(DamageSystem::displayStage(mat, t * frac),
                      damageStage(t * frac, t, kDamageStagesVisible))
                << mat << " at " << frac << "x toughness: the echoed denominator and stage "
                << "count must let a caller reproduce the stage the engine renders";
            // And the packed form, which is what voxel.frag actually samples.
            EXPECT_EQ(DamageSystem::displayStageBits(mat, t * frac),
                      packDamageStage(damageStage(t * frac, t, kDamageStagesVisible)))
                << mat << " at " << frac << "x toughness: packed bits must follow from the "
                << "same two echoed numbers";
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
