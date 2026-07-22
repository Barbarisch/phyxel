#include <gtest/gtest.h>
#include "scene/AppearancePresetRegistry.h"

using namespace Phyxel::Scene;

// The registry shares resources/appearance_presets.json with
// tools/interaction_pipeline/morphology_presets.py — these tests pin the
// C++ side of that contract (tests run from the repo root).

class AppearancePresetRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& reg = AppearancePresetRegistry::instance();
        reg.clear();
        reg.ensureLoaded();
    }
};

TEST_F(AppearancePresetRegistryTest, LoadsAllBuiltInPresets) {
    auto& reg = AppearancePresetRegistry::instance();
    EXPECT_EQ(reg.count(), 11u);
    for (const char* id : {"standard", "giant", "dwarf", "child", "halfling",
                           "gnome", "elf", "tiefling", "dragonborn", "half_orc",
                           "goliath"}) {
        EXPECT_TRUE(reg.hasPreset(id)) << "missing preset: " << id;
    }
}

TEST_F(AppearancePresetRegistryTest, DwarfProportionsMatchSharedJson) {
    // D&D-tuned values (composite ~0.75x standing height ≈ 4'4") — see the
    // tuning notes in resources/appearance_presets.json.
    const auto* dwarf = AppearancePresetRegistry::instance().getPreset("dwarf");
    ASSERT_NE(dwarf, nullptr);
    EXPECT_FLOAT_EQ(dwarf->heightScale, 0.80f);
    EXPECT_FLOAT_EQ(dwarf->bulkScale, 1.45f);
    EXPECT_FLOAT_EQ(dwarf->legLengthScale, 0.82f);
    EXPECT_FLOAT_EQ(dwarf->torsoLengthScale, 1.02f);
    EXPECT_FLOAT_EQ(dwarf->shoulderWidthScale, 1.15f);
    EXPECT_FLOAT_EQ(dwarf->headScale, 1.10f);
    EXPECT_EQ(dwarf->presetId, "dwarf");
}

TEST_F(AppearancePresetRegistryTest, DndRaceHeightOrdering) {
    // The D&D size ladder must hold: halfling < gnome < dwarf < human(standard)
    // < half_orc < dragonborn < goliath. heightScale alone is a proxy (limb
    // scales compound), but the tuned presets keep the ladder monotone in it.
    auto& reg = AppearancePresetRegistry::instance();
    auto h = [&](const char* id) { return reg.getPreset(id)->heightScale; };
    EXPECT_LT(h("halfling"), h("gnome"));
    EXPECT_LT(h("gnome"), h("dwarf"));
    EXPECT_LT(h("dwarf"), h("standard"));
    EXPECT_LT(h("standard"), h("half_orc"));
    EXPECT_LT(h("half_orc"), h("dragonborn"));
    EXPECT_LT(h("dragonborn"), h("goliath"));
    // And the dwarf must be the bulkiest of the small/medium races.
    EXPECT_GT(reg.getPreset("dwarf")->bulkScale, reg.getPreset("halfling")->bulkScale);
    EXPECT_GT(reg.getPreset("dwarf")->bulkScale, reg.getPreset("standard")->bulkScale);
}

TEST_F(AppearancePresetRegistryTest, UnknownPresetReturnsNull) {
    EXPECT_EQ(AppearancePresetRegistry::instance().getPreset("no_such_preset"), nullptr);
}

TEST_F(AppearancePresetRegistryTest, StandardIsAllOnes) {
    const auto* std_ = AppearancePresetRegistry::instance().getPreset("standard");
    ASSERT_NE(std_, nullptr);
    EXPECT_FLOAT_EQ(std_->heightScale, 1.0f);
    EXPECT_FLOAT_EQ(std_->bulkScale, 1.0f);
}

TEST_F(AppearancePresetRegistryTest, ApplyProportionsFromKeepsColors) {
    const auto* dwarf = AppearancePresetRegistry::instance().getPreset("dwarf");
    ASSERT_NE(dwarf, nullptr);

    CharacterAppearance app;
    app.skinColor = {0.1f, 0.2f, 0.3f, 1.0f};
    app.torsoColor = {0.4f, 0.5f, 0.6f, 1.0f};
    app.morphology = MorphologyType::Humanoid;

    app.applyProportionsFrom(*dwarf);
    EXPECT_FLOAT_EQ(app.heightScale, 0.60f);
    EXPECT_EQ(app.presetId, "dwarf");
    // Colors and morphology untouched — a preset is proportions only.
    EXPECT_FLOAT_EQ(app.skinColor.r, 0.1f);
    EXPECT_FLOAT_EQ(app.torsoColor.g, 0.5f);
    EXPECT_EQ(app.morphology, MorphologyType::Humanoid);
}
