#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "core/SettlementLayout.h"

using namespace Phyxel::Core;

// ============================================================================
// Building variation — a settlement must NOT be N identical boxes. pickBuildingVariant assigns each
// plot a typology + style + footprint shape, picked deterministically and INDEPENDENTLY, so a village
// shows a real distribution across all three dimensions (and is reproducible).
// ============================================================================

namespace {
const std::vector<std::string> kTypologies = {"croft", "longhouse", "hall_house"};
const std::vector<std::string> kStyles     = {"timber_cottage", "stone_manor", "stone_keep"};
}  // namespace

// A 24-plot village varies across EVERY dimension (>=2 distinct values each) — not a uniform row.
TEST(BuildingVariantTest, VillageVariesAcrossTypologyStyleAndShape) {
    std::set<std::string> typ, sty, shp;
    for (int i = 0; i < 24; ++i) {
        const BuildingVariant v = pickBuildingVariant(i, kTypologies, kStyles, 7u);
        // every choice is a valid palette member / known shape
        EXPECT_NE(std::find(kTypologies.begin(), kTypologies.end(), v.typology), kTypologies.end());
        EXPECT_NE(std::find(kStyles.begin(), kStyles.end(), v.style), kStyles.end());
        EXPECT_TRUE(v.footprintShape == "rect" || v.footprintShape == "L") << v.footprintShape;
        typ.insert(v.typology); sty.insert(v.style); shp.insert(v.footprintShape);
    }
    EXPECT_GE(typ.size(), 2u) << "typology never varies across the village";
    EXPECT_GE(sty.size(), 2u) << "style never varies — every house looks the same";
    EXPECT_GE(shp.size(), 2u) << "footprint shape never varies — all rectangles (or all L)";
}

// Deterministic: same (plot, seed) -> same variant; and the dimensions vary INDEPENDENTLY (style isn't
// just a function of typology), so neighbours can share a typology yet differ in style/shape.
TEST(BuildingVariantTest, DeterministicAndIndependentDimensions) {
    for (int i = 0; i < 10; ++i) {
        const BuildingVariant a = pickBuildingVariant(i, kTypologies, kStyles, 7u);
        const BuildingVariant b = pickBuildingVariant(i, kTypologies, kStyles, 7u);
        EXPECT_EQ(a.typology, b.typology);
        EXPECT_EQ(a.style, b.style);
        EXPECT_EQ(a.footprintShape, b.footprintShape);
    }
    // Independence: among plots sharing a typology, more than one (style|shape) combo appears.
    std::set<std::string> combosForHallHouse;
    for (int i = 0; i < 60; ++i) {
        const BuildingVariant v = pickBuildingVariant(i, kTypologies, kStyles, 7u);
        if (v.typology == "hall_house") combosForHallHouse.insert(v.style + "/" + v.footprintShape);
    }
    EXPECT_GE(combosForHallHouse.size(), 2u) << "style/shape are slaved to typology, not independent";
}

// Empty palettes -> sane defaults, no crash.
TEST(BuildingVariantTest, EmptyPalettesFallBack) {
    const BuildingVariant v = pickBuildingVariant(3, {}, {}, 1u);
    EXPECT_FALSE(v.typology.empty());
    EXPECT_FALSE(v.style.empty());
    EXPECT_TRUE(v.footprintShape == "rect" || v.footprintShape == "L");
}
