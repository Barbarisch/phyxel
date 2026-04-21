#include <gtest/gtest.h>
#include "core/CharacterAttributes.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// AbilityScore
// ---------------------------------------------------------------------------

TEST(AbilityScoreTest, DefaultScore) {
    AbilityScore a;
    EXPECT_EQ(a.total(), 10);
    EXPECT_EQ(a.modifier(), 0);
}

TEST(AbilityScoreTest, ModifierTable) {
    // PHB modifier table: score → modifier
    struct Case { int score; int expectedMod; };
    Case cases[] = {
        {1, -5}, {2, -4}, {3, -4}, {4, -3}, {5, -3},
        {6, -2}, {7, -2}, {8, -1}, {9, -1},
        {10, 0}, {11, 0},
        {12, 1}, {13, 1},
        {14, 2}, {15, 2},
        {16, 3}, {17, 3},
        {18, 4}, {19, 4},
        {20, 5},
        {24, 7}, {30, 10}
    };
    for (auto& c : cases) {
        AbilityScore a;
        a.base = c.score;
        EXPECT_EQ(a.modifier(), c.expectedMod) << "Score=" << c.score;
    }
}

TEST(AbilityScoreTest, TotalSumsAllLayers) {
    AbilityScore a;
    a.base      = 10;
    a.racial    = 2;
    a.equipment = 1;
    a.temporary = -1;
    EXPECT_EQ(a.total(), 12);
    EXPECT_EQ(a.modifier(), 1);
}

TEST(AbilityScoreTest, TotalClampsAt1) {
    AbilityScore a;
    a.base      = 1;
    a.temporary = -50;
    EXPECT_EQ(a.total(), 1);
}

TEST(AbilityScoreTest, TotalClampsAt30) {
    AbilityScore a;
    a.base      = 20;
    a.equipment = 30;
    EXPECT_EQ(a.total(), 30);
    EXPECT_EQ(a.modifier(), 10);
}

TEST(AbilityScoreTest, JsonRoundTrip) {
    AbilityScore a;
    a.base = 15; a.racial = 2; a.equipment = 1; a.temporary = -1;

    auto j = a.toJson();
    AbilityScore b;
    b.fromJson(j);

    EXPECT_EQ(b.base, 15);
    EXPECT_EQ(b.racial, 2);
    EXPECT_EQ(b.equipment, 1);
    EXPECT_EQ(b.temporary, -1);
    EXPECT_EQ(b.total(), a.total());
}

// ---------------------------------------------------------------------------
// CharacterAttributes
// ---------------------------------------------------------------------------

TEST(CharacterAttributesTest, DefaultScoresAre10) {
    CharacterAttributes attrs;
    EXPECT_EQ(attrs.strength.base, 10);
    EXPECT_EQ(attrs.dexterity.base, 10);
    EXPECT_EQ(attrs.constitution.base, 10);
    EXPECT_EQ(attrs.intelligence.base, 10);
    EXPECT_EQ(attrs.wisdom.base, 10);
    EXPECT_EQ(attrs.charisma.base, 10);
}

TEST(CharacterAttributesTest, GetByType) {
    CharacterAttributes attrs;
    attrs.strength.base = 16;
    attrs.dexterity.base = 14;

    EXPECT_EQ(attrs.get(AbilityType::Strength).total(), 16);
    EXPECT_EQ(attrs.get(AbilityType::Dexterity).total(), 14);
    EXPECT_EQ(attrs.modifier(AbilityType::Strength), 3);
    EXPECT_EQ(attrs.modifier(AbilityType::Dexterity), 2);
}

TEST(CharacterAttributesTest, SetAll) {
    CharacterAttributes attrs;
    attrs.setAll(15, 14, 13, 12, 10, 8);

    EXPECT_EQ(attrs.strength.base,     15);
    EXPECT_EQ(attrs.dexterity.base,    14);
    EXPECT_EQ(attrs.constitution.base, 13);
    EXPECT_EQ(attrs.intelligence.base, 12);
    EXPECT_EQ(attrs.wisdom.base,       10);
    EXPECT_EQ(attrs.charisma.base,     8);
}

TEST(CharacterAttributesTest, DerivedInitiativeBonus) {
    CharacterAttributes attrs;
    attrs.dexterity.base = 16;  // +3 modifier
    EXPECT_EQ(attrs.initiativeBonus(), 3);
}

TEST(CharacterAttributesTest, DerivedUnarmoredAC) {
    CharacterAttributes attrs;
    attrs.dexterity.base = 14;  // +2 modifier
    EXPECT_EQ(attrs.unarmoredAC(), 12);
}

TEST(CharacterAttributesTest, DerivedCarryCapacity) {
    CharacterAttributes attrs;
    attrs.strength.base = 16;
    EXPECT_EQ(attrs.carryCapacity(), 16 * 15);  // 240 lbs
}

TEST(CharacterAttributesTest, DerivedPushDragLift) {
    CharacterAttributes attrs;
    attrs.strength.base = 18;
    EXPECT_EQ(attrs.pushDragLift(), 18 * 30);   // 540 lbs
}

TEST(CharacterAttributesTest, JsonRoundTrip) {
    CharacterAttributes a;
    a.strength.base     = 16;
    a.dexterity.base    = 14;
    a.constitution.base = 13;
    a.intelligence.base = 12;
    a.wisdom.base       = 10;
    a.charisma.base     = 8;
    a.strength.racial   = 2;  // Mountain Dwarf bonus

    auto j = a.toJson();
    CharacterAttributes b;
    b.fromJson(j);

    EXPECT_EQ(b.strength.base,     16);
    EXPECT_EQ(b.strength.racial,   2);
    EXPECT_EQ(b.strength.total(),  18);
    EXPECT_EQ(b.strength.modifier(), 4);
    EXPECT_EQ(b.charisma.base,     8);
    EXPECT_EQ(b.charisma.modifier(), -1);
}

// ---------------------------------------------------------------------------
// abilityFromString / abilityShortName / abilityFullName
// ---------------------------------------------------------------------------

TEST(AbilityTypeHelpersTest, ShortNames) {
    EXPECT_STREQ(abilityShortName(AbilityType::Strength),     "STR");
    EXPECT_STREQ(abilityShortName(AbilityType::Dexterity),    "DEX");
    EXPECT_STREQ(abilityShortName(AbilityType::Constitution), "CON");
    EXPECT_STREQ(abilityShortName(AbilityType::Intelligence), "INT");
    EXPECT_STREQ(abilityShortName(AbilityType::Wisdom),       "WIS");
    EXPECT_STREQ(abilityShortName(AbilityType::Charisma),     "CHA");
}

TEST(AbilityTypeHelpersTest, FullNames) {
    EXPECT_STREQ(abilityFullName(AbilityType::Strength),     "Strength");
    EXPECT_STREQ(abilityFullName(AbilityType::Dexterity),    "Dexterity");
    EXPECT_STREQ(abilityFullName(AbilityType::Constitution), "Constitution");
    EXPECT_STREQ(abilityFullName(AbilityType::Intelligence), "Intelligence");
    EXPECT_STREQ(abilityFullName(AbilityType::Wisdom),       "Wisdom");
    EXPECT_STREQ(abilityFullName(AbilityType::Charisma),     "Charisma");
}

TEST(AbilityTypeHelpersTest, ParseShortNames) {
    EXPECT_EQ(abilityFromString("STR"), AbilityType::Strength);
    EXPECT_EQ(abilityFromString("DEX"), AbilityType::Dexterity);
    EXPECT_EQ(abilityFromString("CON"), AbilityType::Constitution);
    EXPECT_EQ(abilityFromString("INT"), AbilityType::Intelligence);
    EXPECT_EQ(abilityFromString("WIS"), AbilityType::Wisdom);
    EXPECT_EQ(abilityFromString("CHA"), AbilityType::Charisma);
}

TEST(AbilityTypeHelpersTest, ParseFullNames) {
    EXPECT_EQ(abilityFromString("Strength"),     AbilityType::Strength);
    EXPECT_EQ(abilityFromString("Dexterity"),    AbilityType::Dexterity);
    EXPECT_EQ(abilityFromString("Constitution"), AbilityType::Constitution);
    EXPECT_EQ(abilityFromString("Intelligence"), AbilityType::Intelligence);
    EXPECT_EQ(abilityFromString("Wisdom"),       AbilityType::Wisdom);
    EXPECT_EQ(abilityFromString("Charisma"),     AbilityType::Charisma);
}

TEST(AbilityTypeHelpersTest, ParseCaseInsensitive) {
    EXPECT_EQ(abilityFromString("str"), AbilityType::Strength);
    EXPECT_EQ(abilityFromString("dex"), AbilityType::Dexterity);
    EXPECT_EQ(abilityFromString("strength"), AbilityType::Strength);
}

TEST(AbilityTypeHelpersTest, ParseUnknownThrows) {
    EXPECT_THROW(abilityFromString("XYZ"), std::invalid_argument);
    EXPECT_THROW(abilityFromString(""), std::invalid_argument);
}
