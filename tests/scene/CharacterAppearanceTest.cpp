#include <gtest/gtest.h>
#include "scene/CharacterAppearance.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::Scene;

// ============================================================================
// Default / Factory
// ============================================================================

TEST(CharacterAppearanceTest, DefaultAppearanceMatchesOriginalColors) {
    auto app = CharacterAppearance::defaultAppearance();
    EXPECT_FLOAT_EQ(app.skinColor.r, 1.0f);
    EXPECT_FLOAT_EQ(app.skinColor.g, 0.8f);
    EXPECT_FLOAT_EQ(app.skinColor.b, 0.6f);
    EXPECT_FLOAT_EQ(app.torsoColor.r, 0.8f);
    EXPECT_FLOAT_EQ(app.torsoColor.g, 0.2f);
    EXPECT_FLOAT_EQ(app.legColor.b, 0.8f);
    EXPECT_FLOAT_EQ(app.heightScale, 1.0f);
    EXPECT_FLOAT_EQ(app.bulkScale, 1.0f);
    EXPECT_FLOAT_EQ(app.headScale, 1.0f);
}

// ============================================================================
// Bone → Color mapping
// ============================================================================

TEST(CharacterAppearanceTest, GetColorForBoneHead) {
    CharacterAppearance app;
    app.skinColor = glm::vec4(0.9f, 0.7f, 0.5f, 1.0f);
    EXPECT_EQ(app.getColorForBone("Head"), app.skinColor);
    EXPECT_EQ(app.getColorForBone("LeftHand"), app.skinColor);
    EXPECT_EQ(app.getColorForBone("RightHand"), app.skinColor);
}

TEST(CharacterAppearanceTest, GetColorForBoneArm) {
    CharacterAppearance app;
    app.armColor = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
    EXPECT_EQ(app.getColorForBone("LeftArm"), app.armColor);
    EXPECT_EQ(app.getColorForBone("RightShoulder"), app.armColor);
}

TEST(CharacterAppearanceTest, GetColorForBoneLeg) {
    CharacterAppearance app;
    app.legColor = glm::vec4(0.4f, 0.5f, 0.6f, 1.0f);
    EXPECT_EQ(app.getColorForBone("LeftLeg"), app.legColor);
    EXPECT_EQ(app.getColorForBone("RightFoot"), app.legColor);
    EXPECT_EQ(app.getColorForBone("LeftThigh"), app.legColor);
}

TEST(CharacterAppearanceTest, GetColorForBoneTorso) {
    CharacterAppearance app;
    app.torsoColor = glm::vec4(0.7f, 0.8f, 0.9f, 1.0f);
    EXPECT_EQ(app.getColorForBone("Spine"), app.torsoColor);
    EXPECT_EQ(app.getColorForBone("Torso"), app.torsoColor);
    EXPECT_EQ(app.getColorForBone("Hips"), app.torsoColor);
    EXPECT_EQ(app.getColorForBone("Chest"), app.torsoColor);
    EXPECT_EQ(app.getColorForBone("Neck"), app.torsoColor);
}

TEST(CharacterAppearanceTest, GetColorForBoneCaseInsensitive) {
    CharacterAppearance app;
    auto headColor = app.getColorForBone("head");
    EXPECT_EQ(headColor, app.skinColor);
    auto armColor = app.getColorForBone("LEFTARM");
    EXPECT_EQ(armColor, app.armColor);
}

TEST(CharacterAppearanceTest, GetColorForBoneUnknownReturnsDefault) {
    CharacterAppearance app;
    app.defaultColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    EXPECT_EQ(app.getColorForBone("SomeBone"), app.defaultColor);
    EXPECT_EQ(app.getColorForBone("Root"), app.defaultColor);
}

// ============================================================================
// Seed-based generation
// ============================================================================

TEST(CharacterAppearanceTest, GenerateFromSeedIsDeterministic) {
    auto a1 = CharacterAppearance::generateFromSeed("Alice");
    auto a2 = CharacterAppearance::generateFromSeed("Alice");
    EXPECT_EQ(a1.skinColor, a2.skinColor);
    EXPECT_EQ(a1.torsoColor, a2.torsoColor);
    EXPECT_EQ(a1.armColor, a2.armColor);
    EXPECT_EQ(a1.legColor, a2.legColor);
    EXPECT_FLOAT_EQ(a1.heightScale, a2.heightScale);
    EXPECT_FLOAT_EQ(a1.bulkScale, a2.bulkScale);
}

TEST(CharacterAppearanceTest, DifferentNamesDifferentAppearances) {
    auto a = CharacterAppearance::generateFromSeed("Alice");
    auto b = CharacterAppearance::generateFromSeed("Bob");
    // At least one color should differ (extremely unlikely to collide)
    bool anyDiff = (a.torsoColor != b.torsoColor) ||
                   (a.armColor != b.armColor) ||
                   (a.legColor != b.legColor) ||
                   (a.skinColor != b.skinColor);
    EXPECT_TRUE(anyDiff);
}

TEST(CharacterAppearanceTest, RoleAffectsAppearance) {
    auto plain = CharacterAppearance::generateFromSeed("Guard1");
    auto guard = CharacterAppearance::generateFromSeed("Guard1", "guard");
    // Guard role should override the palette selection, so at least one color differs
    bool anyDiff = (plain.torsoColor != guard.torsoColor) ||
                   (plain.bulkScale != guard.bulkScale);
    EXPECT_TRUE(anyDiff);
}

TEST(CharacterAppearanceTest, BlacksmithIsBulky) {
    auto smith = CharacterAppearance::generateFromSeed("Olaf", "blacksmith");
    EXPECT_GT(smith.bulkScale, 1.0f);
}

// ============================================================================
// JSON round-trip
// ============================================================================

TEST(CharacterAppearanceTest, JsonRoundTrip) {
    CharacterAppearance original;
    original.skinColor = glm::vec4(0.9f, 0.7f, 0.5f, 1.0f);
    original.torsoColor = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
    original.armColor = glm::vec4(0.4f, 0.5f, 0.6f, 1.0f);
    original.legColor = glm::vec4(0.7f, 0.8f, 0.9f, 1.0f);
    original.heightScale = 1.1f;
    original.bulkScale = 0.9f;
    original.headScale = 1.2f;

    auto j = original.toJson();
    auto restored = CharacterAppearance::fromJson(j);

    EXPECT_EQ(restored.skinColor, original.skinColor);
    EXPECT_EQ(restored.torsoColor, original.torsoColor);
    EXPECT_EQ(restored.armColor, original.armColor);
    EXPECT_EQ(restored.legColor, original.legColor);
    EXPECT_FLOAT_EQ(restored.heightScale, original.heightScale);
    EXPECT_FLOAT_EQ(restored.bulkScale, original.bulkScale);
    EXPECT_FLOAT_EQ(restored.headScale, original.headScale);
}

TEST(CharacterAppearanceTest, FromJsonPartialFields) {
    nlohmann::json j = {
        {"skinColor", {{"r", 0.5f}, {"g", 0.5f}, {"b", 0.5f}, {"a", 1.0f}}},
        {"heightScale", 1.3f}
    };
    auto app = CharacterAppearance::fromJson(j);
    EXPECT_FLOAT_EQ(app.skinColor.r, 0.5f);
    EXPECT_FLOAT_EQ(app.heightScale, 1.3f);
    // Unspecified fields should use defaults
    EXPECT_FLOAT_EQ(app.torsoColor.r, 0.8f); // default
    EXPECT_FLOAT_EQ(app.bulkScale, 1.0f);    // default
}

TEST(CharacterAppearanceTest, FromJsonEmpty) {
    auto app = CharacterAppearance::fromJson(nlohmann::json::object());
    auto def = CharacterAppearance::defaultAppearance();
    EXPECT_EQ(app.skinColor, def.skinColor);
    EXPECT_EQ(app.torsoColor, def.torsoColor);
    EXPECT_FLOAT_EQ(app.heightScale, def.heightScale);
}
