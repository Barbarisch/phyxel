#include <gtest/gtest.h>

#include <fstream>

#include "core/StyleProfile.h"

using namespace Phyxel::Core;

TEST(StyleProfileTest, LoadsStyleFields) {
    auto j = nlohmann::json::parse(R"({
        "timber_cottage": {
            "description": "humble cottage",
            "roof_style": "gable",
            "foundation": "crawlspace",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222 },
            "materials": { "structure": "Wood", "foundation": "Stone" },
            "flags": { "exposed_beams": true },
            "roof": { "pitch": 0.8, "overhang": 0.4 },
            "ceiling": { "humble": 2.5 }
        }
    })");
    StyleProfileRegistry reg;
    ASSERT_TRUE(reg.loadFromJson(j));
    const StyleProfile* s = reg.get("timber_cottage");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->roofStyle, "gable");
    EXPECT_EQ(s->foundation, "crawlspace");
    EXPECT_DOUBLE_EQ(s->thicknessOf("exterior_wall"), 0.333);
    EXPECT_DOUBLE_EQ(s->thicknessOf("interior_wall"), 0.222);
    EXPECT_EQ(s->materialOf("structure"), "Wood");
    EXPECT_EQ(s->materialOf("foundation"), "Stone");
    EXPECT_TRUE(s->flag("exposed_beams"));
    EXPECT_DOUBLE_EQ(s->roofOf("overhang"), 0.4);
    EXPECT_DOUBLE_EQ(s->ceilingOf("humble"), 2.5);
}

TEST(StyleProfileTest, AccessorFallbacks) {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({ "x": { "thickness": {} } })"));
    const StyleProfile* s = reg.get("x");
    ASSERT_NE(s, nullptr);
    EXPECT_DOUBLE_EQ(s->thicknessOf("exterior_wall", 0.333), 0.333);   // fallback
    EXPECT_EQ(s->materialOf("structure", "Wood"), "Wood");
    EXPECT_FALSE(s->flag("quoins"));
}

// The whole "configurable per style/material" decision: two styles, different
// wall thickness. A stone manor must read thicker than a timber cottage.
TEST(StyleProfileTest, PerStyleThicknessDiffers) {
    auto j = nlohmann::json::parse(R"({
        "timber_cottage": { "thickness": { "exterior_wall": 0.333 } },
        "stone_manor":    { "thickness": { "exterior_wall": 0.667 } }
    })");
    StyleProfileRegistry reg;
    ASSERT_TRUE(reg.loadFromJson(j));
    EXPECT_LT(reg.get("timber_cottage")->thicknessOf("exterior_wall"),
              reg.get("stone_manor")->thicknessOf("exterior_wall"));
}

TEST(StyleProfileTest, ParsesPerValueSources) {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"json({
        "x": { "thickness": { "exterior_wall": 0.222 },
               "sources": { "exterior_wall": "wattle & daub (buildingconservation.com)" } }
    })json"));
    const StyleProfile* s = reg.get("x");
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->hasSource("exterior_wall"));
    EXPECT_FALSE(s->hasSource("foundation_wall"));
}

// The shipped styles must now be GROUNDED: period-correct wall thicknesses, each cited.
TEST(StyleProfileTest, ShippedStylesAreGrounded) {
    const char* candidates[] = {
        "resources/structure_styles.json", "../resources/structure_styles.json",
        "../../resources/structure_styles.json", "../../../resources/structure_styles.json",
    };
    StyleProfileRegistry reg;
    bool found = false;
    for (const char* p : candidates) { std::ifstream f(p); if (f.good()) { found = reg.loadFromFile(p); break; } }
    if (!found) GTEST_SKIP() << "resources/structure_styles.json not reachable";

    const StyleProfile* cottage = reg.get("timber_cottage");
    const StyleProfile* manor = reg.get("stone_manor");
    ASSERT_NE(cottage, nullptr);
    ASSERT_NE(manor, nullptr);
    // grounded thicknesses (medieval): daub cottage wall 0.222, stone manor wall 0.667
    EXPECT_NEAR(cottage->thicknessOf("exterior_wall"), 0.222, 1e-9);
    EXPECT_NEAR(manor->thicknessOf("exterior_wall"), 0.667, 1e-9);
    // every dimensional value is cited
    EXPECT_TRUE(cottage->hasSource("exterior_wall"));
    EXPECT_TRUE(cottage->hasSource("foundation_wall"));
    EXPECT_TRUE(manor->hasSource("exterior_wall"));
    EXPECT_FALSE(cottage->source.empty());
}

TEST(StyleProfileTest, ShippedStylesParse) {
    const char* candidates[] = {
        "resources/structure_styles.json",
        "../resources/structure_styles.json",
        "../../resources/structure_styles.json",
        "../../../resources/structure_styles.json",
    };
    StyleProfileRegistry reg;
    bool found = false;
    for (const char* p : candidates) {
        std::ifstream f(p);
        if (f.good()) { found = reg.loadFromFile(p); break; }
    }
    if (!found) GTEST_SKIP() << "resources/structure_styles.json not reachable from CWD";

    ASSERT_TRUE(reg.contains("timber_cottage"));
    ASSERT_TRUE(reg.contains("stone_manor"));
    EXPECT_EQ(reg.get("timber_cottage")->foundation, "crawlspace");
    EXPECT_LT(reg.get("timber_cottage")->thicknessOf("exterior_wall"),
              reg.get("stone_manor")->thicknessOf("exterior_wall"));
}
