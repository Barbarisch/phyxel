#include <gtest/gtest.h>
#include "core/ChopManager.h"

using namespace Phyxel::Core;

namespace {
glm::ivec3 base(int x, int y, int z) { return glm::ivec3(x, y, z); }
}

// A single swing below the hardness threshold makes partial progress and does
// not fell the tree.
TEST(ChopManagerTest, SingleChopProgressNoFell) {
    ChopManager cm;
    auto r = cm.addChop(base(10, 16, 10), "Log", 6, /*chopPower=*/4.0f, /*hardness=*/24.0f);
    EXPECT_FALSE(r.felled);
    EXPECT_FALSE(r.alreadyFelled);
    EXPECT_NEAR(r.progress, 4.0f / 24.0f, 1e-4f);
    EXPECT_NEAR(cm.progressAt(base(10, 16, 10)), 4.0f / 24.0f, 1e-4f);
    EXPECT_FALSE(cm.isFelled(base(10, 16, 10)));
}

// Progress is monotonic across swings and the tree fells exactly on the swing
// that crosses the hardness threshold — fired once.
TEST(ChopManagerTest, FellsOnceOnThresholdCrossing) {
    ChopManager cm;
    int fellCount = 0;
    TreeFellEvent captured;
    cm.setOnTreeFelled([&](const TreeFellEvent& ev) { ++fellCount; captured = ev; });

    const glm::ivec3 b = base(0, 20, 0);
    float lastProgress = -1.0f;
    bool sawFell = false;
    for (int i = 0; i < 6; ++i) {  // 6 * 4 = 24 == hardness
        auto r = cm.addChop(b, "Log", 6, 4.0f, 24.0f);
        EXPECT_GE(r.progress, lastProgress) << "progress regressed at swing " << i;
        lastProgress = r.progress;
        if (r.felled) {
            EXPECT_FALSE(sawFell) << "felled fired more than once";
            sawFell = true;
            EXPECT_EQ(i, 5) << "felled on the wrong swing";
        }
    }
    EXPECT_TRUE(sawFell);
    EXPECT_EQ(fellCount, 1);
    EXPECT_EQ(captured.base, b);
    EXPECT_EQ(captured.material, "Log");
    EXPECT_EQ(captured.trunkHeight, 6);
    EXPECT_GE(captured.totalChop, 24.0f);
    EXPECT_TRUE(cm.isFelled(b));
    EXPECT_NEAR(cm.progressAt(b), 1.0f, 1e-6f);
}

// Chopping a felled tree does not re-fire the callback or over-accumulate.
TEST(ChopManagerTest, NoRefireAfterFell) {
    ChopManager cm;
    int fellCount = 0;
    cm.setOnTreeFelled([&](const TreeFellEvent&) { ++fellCount; });

    const glm::ivec3 b = base(3, 16, -7);
    cm.addChop(b, "Log", 2, 10.0f, 8.0f);  // fells immediately (10 >= 8)
    EXPECT_EQ(fellCount, 1);

    for (int i = 0; i < 5; ++i) {
        auto r = cm.addChop(b, "Log", 2, 10.0f, 8.0f);
        EXPECT_FALSE(r.felled);
        EXPECT_TRUE(r.alreadyFelled);
        EXPECT_NEAR(r.progress, 1.0f, 1e-6f);
    }
    EXPECT_EQ(fellCount, 1) << "felled must fire exactly once";
}

// The first contact fixes a tree's hardness/height/material; later swings on the
// same base accumulate into the same tree regardless of the params they pass.
TEST(ChopManagerTest, FirstContactFixesHardness) {
    ChopManager cm;
    const glm::ivec3 b = base(5, 16, 5);
    cm.addChop(b, "LogBirch", 4, 4.0f, /*hardness=*/12.0f);  // fixes hardness=12
    cm.addChop(b, "LogBirch", 999, 4.0f, /*hardness=*/9999.0f);  // ignored hardness
    auto r = cm.addChop(b, "LogBirch", 4, 4.0f, 12.0f);  // 3rd * 4 = 12 -> fell
    EXPECT_TRUE(r.felled);
    EXPECT_EQ(cm.trackedCount(), 1u);
}

// Hardness is clamped to at least 1 so a zero/negative hardness still fells.
TEST(ChopManagerTest, HardnessClampedToOne) {
    ChopManager cm;
    auto r = cm.addChop(base(1, 1, 1), "Log", 1, 4.0f, /*hardness=*/0.0f);
    EXPECT_TRUE(r.felled);
    EXPECT_NEAR(r.progress, 1.0f, 1e-6f);
}

// forget() clears a tree so the same site can be chopped fresh (post-topple).
TEST(ChopManagerTest, ForgetResetsTree) {
    ChopManager cm;
    int fellCount = 0;
    cm.setOnTreeFelled([&](const TreeFellEvent&) { ++fellCount; });
    const glm::ivec3 b = base(2, 2, 2);
    cm.addChop(b, "Log", 1, 10.0f, 4.0f);  // fell
    EXPECT_TRUE(cm.isFelled(b));
    cm.forget(b);
    EXPECT_FALSE(cm.isFelled(b));
    EXPECT_NEAR(cm.progressAt(b), 0.0f, 1e-6f);
    EXPECT_EQ(cm.trackedCount(), 0u);
    cm.addChop(b, "Log", 1, 10.0f, 4.0f);  // fells again
    EXPECT_EQ(fellCount, 2);
}

// STRESS: many independent trees, each chopped to felling, tracked separately
// with no cross-contamination and exactly one fell event apiece.
TEST(ChopManagerTest, ManyTreesIndependent) {
    ChopManager cm;
    int fellCount = 0;
    cm.setOnTreeFelled([&](const TreeFellEvent&) { ++fellCount; });

    const int N = 200;
    // First pass: one swing each — partial, none felled.
    for (int i = 0; i < N; ++i)
        cm.addChop(base(i, 16, i * 2), "Log", 3, 4.0f, 12.0f);
    EXPECT_EQ(fellCount, 0);
    EXPECT_EQ(cm.trackedCount(), (size_t)N);

    // Two more swings each -> every tree crosses 12 exactly on the 3rd.
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < N; ++i) {
            auto r = cm.addChop(base(i, 16, i * 2), "Log", 3, 4.0f, 12.0f);
            if (pass == 1) EXPECT_TRUE(r.felled) << "tree " << i << " should fell on 3rd swing";
        }
    EXPECT_EQ(fellCount, N);
    for (int i = 0; i < N; ++i)
        EXPECT_TRUE(cm.isFelled(base(i, 16, i * 2)));
}

// Trees at distinct bases never share progress (keying is per-cube).
TEST(ChopManagerTest, DistinctBasesDoNotShare) {
    ChopManager cm;
    cm.addChop(base(0, 16, 0), "Log", 4, 4.0f, 8.0f);
    EXPECT_NEAR(cm.progressAt(base(0, 16, 0)), 0.5f, 1e-4f);
    EXPECT_NEAR(cm.progressAt(base(0, 17, 0)), 0.0f, 1e-6f);  // one voxel up = different tree key
    EXPECT_NEAR(cm.progressAt(base(1, 16, 0)), 0.0f, 1e-6f);
}
