#include <gtest/gtest.h>

#include <fstream>

#include "core/DimensionCanon.h"

using namespace Phyxel::Core;

TEST(DimensionCanonTest, LoadsFlatArchetypeMap) {
    auto j = nlohmann::json::parse(R"({
        "fence_picket": { "category": "fence", "height": 0.9, "tol": 0.15, "post_spacing": 1.8, "rails": 2 }
    })");
    DimensionCanonRegistry reg;
    ASSERT_TRUE(reg.loadFromJson(j));
    ASSERT_TRUE(reg.contains("fence_picket"));
    const ArchetypeDims* a = reg.get("fence_picket");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->category, "fence");
    EXPECT_DOUBLE_EQ(a->value("height"), 0.9);
    EXPECT_DOUBLE_EQ(a->value("post_spacing"), 1.8);
    EXPECT_DOUBLE_EQ(a->tolerance, 0.15);
}

TEST(DimensionCanonTest, ParsesValuesFlagsAnchorsAndTolerance) {
    auto j = nlohmann::json::parse(R"({
        "archetypes": {
            "fence_privacy": { "height": 1.8, "tol": 0.15, "solid": true },
            "chair_dining":  { "seat_top": 0.45, "back_top": 0.9, "anchors": ["seat_0"] }
        }
    })");
    DimensionCanonRegistry reg;
    ASSERT_TRUE(reg.loadFromJson(j));

    const ArchetypeDims* fence = reg.get("fence_privacy");
    ASSERT_NE(fence, nullptr);
    EXPECT_TRUE(fence->flag("solid"));
    EXPECT_FALSE(fence->flag("nonexistent"));
    EXPECT_DOUBLE_EQ(fence->tolerance, 0.15);

    const ArchetypeDims* chair = reg.get("chair_dining");
    ASSERT_NE(chair, nullptr);
    EXPECT_TRUE(chair->has("seat_top"));
    EXPECT_FALSE(chair->has("height"));
    EXPECT_DOUBLE_EQ(chair->value("missing", -1.0), -1.0);   // fallback
    EXPECT_TRUE(chair->requiresAnchor("seat_0"));
    EXPECT_FALSE(chair->requiresAnchor("lie_0"));
}

TEST(DimensionCanonTest, ParsesSourceAndFlagsUnsourced) {
    auto j = nlohmann::json::parse(R"({
        "archetypes": {
            "cited":     { "height": 0.9, "source": "IRC R304" },
            "unsourced": { "height": 0.9 }
        }
    })");
    DimensionCanonRegistry reg;
    ASSERT_TRUE(reg.loadFromJson(j));
    EXPECT_EQ(reg.get("cited")->source, "IRC R304");
    EXPECT_TRUE(reg.get("unsourced")->source.empty());   // flagged by the loader as UNSOURCED
}

TEST(DimensionCanonTest, ParsesPerValueSources) {
    auto j = nlohmann::json::parse(R"({
        "archetypes": {
            "chair_dining": { "seat_top": 0.45, "back_top": 0.9,
                "source": "ergonomics",
                "sources": { "seat_top": "dining chair 17-20 in", "back_top": "INFERRED" } }
        }
    })");
    DimensionCanonRegistry reg;
    ASSERT_TRUE(reg.loadFromJson(j));
    const ArchetypeDims* c = reg.get("chair_dining");
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->hasValueSource("seat_top"));
    EXPECT_TRUE(c->hasValueSource("back_top"));
    EXPECT_FALSE(c->hasValueSource("width"));
    EXPECT_EQ(c->valueSources.at("seat_top"), "dining chair 17-20 in");
}

// The shipped object canon must now be GROUNDED: every archetype cites a source,
// the body-derived furniture heights match the ergonomic values, and door height
// was corrected to the real 2.03 m.
TEST(DimensionCanonTest, ShippedCanonIsGrounded) {
    const char* candidates[] = {
        "resources/object_dimensions.json", "../resources/object_dimensions.json",
        "../../resources/object_dimensions.json", "../../../resources/object_dimensions.json",
    };
    DimensionCanonRegistry reg;
    bool found = false;
    for (const char* p : candidates) { std::ifstream f(p); if (f.good()) { found = reg.loadFromFile(p); break; } }
    if (!found) GTEST_SKIP() << "resources/object_dimensions.json not reachable";

    // every archetype now carries a primary source (no more unsourced debt)
    for (const auto& name : reg.archetypes())
        EXPECT_FALSE(reg.get(name)->source.empty()) << name << " is still unsourced";

    // grounded values + per-value citations
    ASSERT_NE(reg.get("chair_dining"), nullptr);
    EXPECT_NEAR(reg.get("chair_dining")->value("seat_top"), 0.45, 1e-9);
    EXPECT_TRUE(reg.get("chair_dining")->hasValueSource("seat_top"));
    EXPECT_NEAR(reg.get("table_dining")->value("top"), 0.75, 1e-9);
    EXPECT_NEAR(reg.get("door_interior")->value("clear_h"), 2.03, 1e-9);   // corrected from 2.0
    ASSERT_NE(reg.get("chest"), nullptr);                                  // medieval chest added
    EXPECT_TRUE(reg.get("chest")->hasValueSource("width"));
}

TEST(DimensionCanonTest, SkipsCommentKeys) {
    auto j = nlohmann::json::parse(R"({
        "_comment": "ignore me",
        "_units": "cubes",
        "well": { "category": "prop", "height": 0.9 }
    })");
    DimensionCanonRegistry reg;
    ASSERT_TRUE(reg.loadFromJson(j));
    EXPECT_EQ(reg.size(), 1u);
    EXPECT_TRUE(reg.contains("well"));
    EXPECT_FALSE(reg.contains("_comment"));
}

TEST(DimensionCanonTest, MissingFileKeepsExistingAndReturnsFalse) {
    DimensionCanonRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({ "well": { "height": 0.9 } })"));
    EXPECT_FALSE(reg.loadFromFile("does/not/exist.json"));
    EXPECT_TRUE(reg.contains("well"));    // existing entries preserved
}

// The shipped seed canon must be valid and carry the user's reference dimensions
// (picket ~0.9, privacy ~1.8). Runs when the test exe's CWD can see resources/
// (i.e. launched from the repo root); skips otherwise so path quirks don't fail CI.
TEST(DimensionCanonTest, ShippedSeedCanonParses) {
    const char* candidates[] = {
        "resources/object_dimensions.json",
        "../resources/object_dimensions.json",
        "../../resources/object_dimensions.json",
        "../../../resources/object_dimensions.json",
    };
    DimensionCanonRegistry reg;
    bool found = false;
    for (const char* p : candidates) {
        std::ifstream f(p);
        if (f.good()) { found = reg.loadFromFile(p); break; }
    }
    if (!found) GTEST_SKIP() << "resources/object_dimensions.json not reachable from CWD";

    ASSERT_TRUE(reg.contains("fence_picket"));
    ASSERT_TRUE(reg.contains("fence_privacy"));
    ASSERT_TRUE(reg.contains("door_interior"));
    EXPECT_NEAR(reg.get("fence_picket")->value("height"), 0.9, 1e-9);
    EXPECT_NEAR(reg.get("fence_privacy")->value("height"), 1.8, 1e-9);
    EXPECT_TRUE(reg.get("fence_privacy")->flag("solid"));
    EXPECT_TRUE(reg.get("chair_dining")->requiresAnchor("seat_0"));
}
