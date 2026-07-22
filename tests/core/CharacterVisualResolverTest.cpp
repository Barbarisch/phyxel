#include <gtest/gtest.h>
#include "core/CharacterVisualResolver.h"
#include "core/RaceDefinition.h"
#include "scene/AppearancePresetRegistry.h"

using namespace Phyxel;
using Core::CharacterVisualResolver;

// Pins the race -> visual contract: spawning with a race id yields that
// race's preset proportions, palette skin tone, and model. Tests run from
// the repo root so resources/races + resources/appearance_presets.json load.

class CharacterVisualResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        Core::RaceRegistry::instance().clear();
        Scene::AppearancePresetRegistry::instance().clear();
    }
};

TEST_F(CharacterVisualResolverTest, RaceAppliesPresetProportions) {
    nlohmann::json def = {{"race", "dwarf_mountain"}};
    auto res = CharacterVisualResolver::resolve(def, "Thorin");

    EXPECT_TRUE(res.raceFound);
    EXPECT_EQ(res.raceId, "dwarf_mountain");
    EXPECT_EQ(res.animFile, "resources/animated_characters/humanoid.anim");
    EXPECT_FLOAT_EQ(res.appearance.heightScale, 0.80f);
    EXPECT_FLOAT_EQ(res.appearance.bulkScale, 1.45f);
    EXPECT_FLOAT_EQ(res.appearance.legLengthScale, 0.82f);
    EXPECT_EQ(res.appearance.presetId, "dwarf");
}

TEST_F(CharacterVisualResolverTest, RaceSkinToneComesFromPaletteDeterministically) {
    nlohmann::json def = {{"race", "half_orc"}};
    auto a = CharacterVisualResolver::resolve(def, "Grok");
    auto b = CharacterVisualResolver::resolve(def, "Grok");
    // Deterministic per name.
    EXPECT_FLOAT_EQ(a.appearance.skinColor.r, b.appearance.skinColor.r);
    EXPECT_FLOAT_EQ(a.appearance.skinColor.g, b.appearance.skinColor.g);
    // Half-orc palette is green-dominant: green channel above red for every tone.
    EXPECT_GT(a.appearance.skinColor.g, a.appearance.skinColor.r);
}

TEST_F(CharacterVisualResolverTest, ExplicitAppearanceOverridesRaceFieldByField) {
    nlohmann::json def = {
        {"race", "dwarf_mountain"},
        {"appearance", {{"heightScale", 0.75f}}}
    };
    auto res = CharacterVisualResolver::resolve(def, "Thorin");
    EXPECT_FLOAT_EQ(res.appearance.heightScale, 0.75f);   // explicit wins
    EXPECT_FLOAT_EQ(res.appearance.bulkScale, 1.45f);     // rest keeps race preset
}

TEST_F(CharacterVisualResolverTest, ExplicitAnimFileOverridesRace) {
    nlohmann::json def = {
        {"race", "human"},
        {"animFile", "resources/animated_characters/character_female.anim"}
    };
    auto res = CharacterVisualResolver::resolve(def, "Mara");
    EXPECT_EQ(res.animFile, "resources/animated_characters/character_female.anim");
}

TEST_F(CharacterVisualResolverTest, UnknownRaceFallsBackToDefaults) {
    nlohmann::json def = {{"race", "no_such_race"}};
    auto res = CharacterVisualResolver::resolve(def, "Nobody");
    EXPECT_FALSE(res.raceFound);
    EXPECT_EQ(res.animFile, "resources/animated_characters/humanoid.anim");
}

TEST_F(CharacterVisualResolverTest, PresetInAppearanceWithoutRace) {
    nlohmann::json def = {{"appearance", {{"preset", "halfling"}}}};
    auto res = CharacterVisualResolver::resolve(def, "Pippin");
    EXPECT_FLOAT_EQ(res.appearance.heightScale, 0.57f);
    EXPECT_EQ(res.appearance.presetId, "halfling");
}

TEST_F(CharacterVisualResolverTest, RacelessExplicitAppearanceKeepsLegacySemantics) {
    // No race + explicit appearance = defaults + given fields (NOT seeded
    // colors) — existing game.json content must render identically.
    nlohmann::json def = {{"appearance", {{"heightScale", 1.2f}}}};
    auto res = CharacterVisualResolver::resolve(def, "Legacy");
    EXPECT_FLOAT_EQ(res.appearance.heightScale, 1.2f);
    Scene::CharacterAppearance defaults;
    EXPECT_FLOAT_EQ(res.appearance.skinColor.r, defaults.skinColor.r);
    EXPECT_FLOAT_EQ(res.appearance.torsoColor.g, defaults.torsoColor.g);
}

TEST_F(CharacterVisualResolverTest, AllShippedRacesResolveInsideValidatedBand) {
    // Every race in resources/races must resolve to proportions inside the
    // rig-validated band (~0.55 .. 1.30 per-scalar) — a race JSON edit that
    // drifts outside it should fail here, not clip bones at runtime.
    Core::RaceRegistry::instance().ensureLoaded();
    auto ids = Core::RaceRegistry::instance().getAllRaceIds();
    ASSERT_GE(ids.size(), 9u);
    for (const auto& id : ids) {
        nlohmann::json def = {{"race", id}};
        auto res = CharacterVisualResolver::resolve(def, "BandCheck_" + id);
        ASSERT_TRUE(res.raceFound) << id;
        for (float s : {res.appearance.heightScale, res.appearance.bulkScale,
                        res.appearance.headScale, res.appearance.armLengthScale,
                        res.appearance.legLengthScale, res.appearance.torsoLengthScale,
                        res.appearance.shoulderWidthScale}) {
            EXPECT_GE(s, 0.55f) << id;
            EXPECT_LE(s, 1.50f) << id;   // bulk tops out at dwarf's 1.50
        }
    }
}

TEST_F(CharacterVisualResolverTest, RaceDefinitionVisualRoundTrips) {
    Core::RaceRegistry::instance().ensureLoaded();
    const auto* dwarf = Core::RaceRegistry::instance().getRace("dwarf_mountain");
    ASSERT_NE(dwarf, nullptr);
    ASSERT_TRUE(dwarf->hasVisual());

    auto j = dwarf->toJson();
    auto reparsed = Core::RaceDefinition::fromJson(j);
    EXPECT_TRUE(reparsed.hasVisual());
    EXPECT_EQ(reparsed.visual.value("appearancePreset", ""), "dwarf");
}
