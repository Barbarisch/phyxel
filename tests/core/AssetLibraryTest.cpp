#include <gtest/gtest.h>

#include "core/AssetLibrary.h"

using namespace Phyxel::Core;

namespace {
AssetRecord rec(const std::string& id, const std::string& archetype,
                int version, AssetStatus status, double quality = 0.5) {
    AssetRecord r;
    r.id = id;
    r.archetype = archetype;
    r.templateName = id;
    r.version = version;
    r.status = status;
    r.qualityScore = quality;
    r.gatesPassed = true;
    return r;
}
} // namespace

TEST(AssetLibraryTest, RegisterAndGet) {
    AssetLibrary lib;
    lib.registerAsset(rec("chair_v1", "chair_dining", 1, AssetStatus::Provisional));
    ASSERT_NE(lib.get("chair_v1"), nullptr);
    EXPECT_EQ(lib.get("chair_v1")->archetype, "chair_dining");
    EXPECT_EQ(lib.get("missing"), nullptr);
}

TEST(AssetLibraryTest, ProvisionalIsNotSelectableByTheRealizer) {
    AssetLibrary lib;
    lib.registerAsset(rec("chair_v1", "chair_dining", 1, AssetStatus::Provisional));
    // A provisional asset is quarantined — the realizer (approved-only) can't see it.
    EXPECT_TRUE(lib.approvedForArchetype("chair_dining").empty());
    EXPECT_EQ(lib.bestApprovedForArchetype("chair_dining"), nullptr);
}

TEST(AssetLibraryTest, ApprovalMakesItSelectable) {
    AssetLibrary lib;
    lib.registerAsset(rec("chair_v1", "chair_dining", 1, AssetStatus::Provisional));
    ASSERT_TRUE(lib.approve("chair_v1"));
    EXPECT_EQ(lib.approvedForArchetype("chair_dining").size(), 1u);
    ASSERT_NE(lib.bestApprovedForArchetype("chair_dining"), nullptr);
    EXPECT_EQ(lib.bestApprovedForArchetype("chair_dining")->id, "chair_v1");
}

// The golden-regression guarantee: a re-generated asset registers as a new
// provisional version and does NOT replace the blessed one until it too is approved.
TEST(AssetLibraryTest, NewVersionDoesNotReplaceApprovedUntilApproved) {
    AssetLibrary lib;
    lib.registerAsset(rec("chair_v1", "chair_dining", 1, AssetStatus::Approved));
    lib.registerAsset(rec("chair_v2", "chair_dining", 2, AssetStatus::Provisional, 0.9));

    // v2 is better but still provisional -> realizer keeps using the blessed v1.
    ASSERT_NE(lib.bestApprovedForArchetype("chair_dining"), nullptr);
    EXPECT_EQ(lib.bestApprovedForArchetype("chair_dining")->id, "chair_v1");

    // Approve v2 -> now it wins (higher version).
    ASSERT_TRUE(lib.approve("chair_v2"));
    EXPECT_EQ(lib.bestApprovedForArchetype("chair_dining")->id, "chair_v2");
}

TEST(AssetLibraryTest, DeprecatedIsExcluded) {
    AssetLibrary lib;
    lib.registerAsset(rec("chair_v1", "chair_dining", 1, AssetStatus::Approved));
    ASSERT_TRUE(lib.setStatus("chair_v1", AssetStatus::Deprecated));
    EXPECT_EQ(lib.bestApprovedForArchetype("chair_dining"), nullptr);
}

TEST(AssetLibraryTest, RoundTripsThroughJson) {
    AssetLibrary lib;
    AssetRecord r = rec("fence_v1", "fence_picket", 1, AssetStatus::Approved, 0.8);
    r.provenance = "variants->repair, model=claude";
    r.realizedDims["height"] = 0.889;
    lib.registerAsset(r);

    AssetLibrary loaded;
    ASSERT_TRUE(loaded.loadFromJson(lib.toJson()));
    const AssetRecord* g = loaded.get("fence_v1");
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->archetype, "fence_picket");
    EXPECT_EQ(g->status, AssetStatus::Approved);
    EXPECT_DOUBLE_EQ(g->qualityScore, 0.8);
    EXPECT_NEAR(g->realizedDims.at("height"), 0.889, 1e-9);
    EXPECT_EQ(g->provenance, "variants->repair, model=claude");
}
