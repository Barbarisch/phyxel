#include <gtest/gtest.h>
#include "core/CharacterProgression.h"
#include "core/CharacterSheet.h"
#include "core/ClassDefinition.h"
#include "core/RaceDefinition.h"
#include "core/DiceSystem.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Shared test fixture: seeded dice + pre-registered class/race
// ---------------------------------------------------------------------------

class ProgressionTest : public ::testing::Test {
protected:
    void SetUp() override {
        DiceSystem::setSeed(1234);

        // Register fighter (idempotent)
        if (!ClassRegistry::instance().hasClass("fighter")) {
            ClassDefinition c;
            c.id          = "fighter";
            c.name        = "Fighter";
            c.hitDieFaces = 10;
            c.primaryAbility = AbilityType::Strength;
            c.savingThrowProficiencies = {AbilityType::Strength, AbilityType::Constitution};
            c.armorProficiencies  = {"light","medium","heavy","shields"};
            c.weaponProficiencies = {"simple","martial"};
            c.skillChoices = 2;
            c.asiLevels = {4, 6, 8, 12, 14, 16, 19};
            c.features[1] = {{"second_wind","Second Wind","...",1}};
            c.features[2] = {{"action_surge","Action Surge","...",2}};
            c.features[4] = {{"asi","ASI","...",4}};
            c.features[5] = {{"extra_attack","Extra Attack","...",5}};
            ClassRegistry::instance().registerClass(c);
        }

        // Register human (idempotent)
        if (!RaceRegistry::instance().hasRace("human")) {
            RaceDefinition r;
            r.id    = "human";
            r.name  = "Human";
            r.speed = 30;
            r.abilityBonuses = {
                {AbilityType::Strength,1},{AbilityType::Dexterity,1},{AbilityType::Constitution,1},
                {AbilityType::Intelligence,1},{AbilityType::Wisdom,1},{AbilityType::Charisma,1}
            };
            r.languages = {"Common"};
            RaceRegistry::instance().registerRace(r);
        }
    }

    void TearDown() override {
        DiceSystem::setSeed(0);
    }

    CharacterSheet makeLevel1Fighter() {
        CharacterSheet s;
        s.name = "TestChar";
        s.raceId = "human";
        s.classes = {{"fighter", 1, ""}};
        s.attributes.constitution.base = 14;  // +2 CON mod
        s.maxHP    = 12;  // d10(10) + CON(2)
        s.currentHP = 12;
        HitDicePool pool; pool.classId="fighter"; pool.faces=10; pool.total=1; pool.remaining=1;
        s.hitDicePools.push_back(pool);
        s.savingThrowProficiencies = {AbilityType::Strength, AbilityType::Constitution};
        return s;
    }
};

// ---------------------------------------------------------------------------
// XP / level table
// ---------------------------------------------------------------------------

TEST_F(ProgressionTest, LevelForXP_Thresholds) {
    EXPECT_EQ(CharacterProgression::levelForXP(0),      1);
    EXPECT_EQ(CharacterProgression::levelForXP(299),    1);
    EXPECT_EQ(CharacterProgression::levelForXP(300),    2);
    EXPECT_EQ(CharacterProgression::levelForXP(899),    2);
    EXPECT_EQ(CharacterProgression::levelForXP(900),    3);
    EXPECT_EQ(CharacterProgression::levelForXP(6499),   4);
    EXPECT_EQ(CharacterProgression::levelForXP(6500),   5);
    EXPECT_EQ(CharacterProgression::levelForXP(355000), 20);
    EXPECT_EQ(CharacterProgression::levelForXP(999999), 20);
}

TEST_F(ProgressionTest, XPForLevel) {
    EXPECT_EQ(CharacterProgression::xpForLevel(1),  0);
    EXPECT_EQ(CharacterProgression::xpForLevel(2),  300);
    EXPECT_EQ(CharacterProgression::xpForLevel(5),  6500);
    EXPECT_EQ(CharacterProgression::xpForLevel(20), 355000);
}

TEST_F(ProgressionTest, XPToNextLevel) {
    EXPECT_EQ(CharacterProgression::xpToNextLevel(0),   300);   // level 1 → 2
    EXPECT_EQ(CharacterProgression::xpToNextLevel(300), 600);   // level 2 → 3 (900-300)
    EXPECT_EQ(CharacterProgression::xpToNextLevel(355000), 0);  // max level
}

// ---------------------------------------------------------------------------
// Level up — basic
// ---------------------------------------------------------------------------

TEST_F(ProgressionTest, LevelUpIncreasesClassLevel) {
    auto sheet = makeLevel1Fighter();
    auto result = CharacterProgression::levelUp(sheet, "fighter", *static_cast<DiceSystem*>(nullptr), true);
    // Note: levelUp with useAverageHP doesn't call dice — pass nullptr safely
    // Actually we need a real dice object for the roll path; use average
    DiceSystem dice;
    sheet = makeLevel1Fighter();
    result = CharacterProgression::levelUp(sheet, "fighter", dice, true);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.classId, "fighter");
    EXPECT_EQ(result.newClassLevel, 2);
    EXPECT_EQ(result.newTotalLevel, 2);
    EXPECT_EQ(sheet.classes[0].level, 2);
}

TEST_F(ProgressionTest, LevelUpAverageHP) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    int hpBefore = sheet.maxHP;

    auto result = CharacterProgression::levelUp(sheet, "fighter", dice, true);

    // Average for d10 = (10/2)+1 = 6, CON mod = +2, total = 8. At min 1.
    EXPECT_EQ(result.hpGained, 8);
    EXPECT_EQ(sheet.maxHP, hpBefore + 8);
}

TEST_F(ProgressionTest, LevelUpRollHP) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    int hpBefore = sheet.maxHP;

    auto result = CharacterProgression::levelUp(sheet, "fighter", dice, false);

    // Rolled: d10 [1-10] + CON mod (+2). Min 1.
    EXPECT_GE(result.hpGained, 1);
    EXPECT_LE(result.hpGained, 12);
    EXPECT_EQ(sheet.maxHP, hpBefore + result.hpGained);
}

TEST_F(ProgressionTest, LevelUpGrantsFeatures) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    // Level up to 2 — "action_surge" is level-2 feature
    auto result = CharacterProgression::levelUp(sheet, "fighter", dice, true);
    EXPECT_NE(std::find(result.featuresGained.begin(), result.featuresGained.end(), "action_surge"),
              result.featuresGained.end());
    EXPECT_NE(std::find(sheet.earnedFeatureIds.begin(), sheet.earnedFeatureIds.end(), "action_surge"),
              sheet.earnedFeatureIds.end());
}

TEST_F(ProgressionTest, LevelUpGrantsASIAtCorrectLevel) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();

    // Level up 3 times to reach level 4 (ASI level for fighter)
    CharacterProgression::levelUp(sheet, "fighter", dice, true);
    CharacterProgression::levelUp(sheet, "fighter", dice, true);
    auto result = CharacterProgression::levelUp(sheet, "fighter", dice, true);

    EXPECT_TRUE(result.grantsASI);
    EXPECT_EQ(sheet.availableASIs, 1);
}

TEST_F(ProgressionTest, LevelUpIncreasesHitDicePool) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    EXPECT_EQ(sheet.hitDicePools[0].total, 1);

    CharacterProgression::levelUp(sheet, "fighter", dice, true);
    EXPECT_EQ(sheet.hitDicePools[0].total, 2);
    EXPECT_EQ(sheet.hitDicePools[0].remaining, 2);
}

TEST_F(ProgressionTest, LevelUpUnknownClassFails) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    auto result = CharacterProgression::levelUp(sheet, "nonexistent", dice, true);
    EXPECT_FALSE(result.success);
}

// ---------------------------------------------------------------------------
// ASI
// ---------------------------------------------------------------------------

TEST_F(ProgressionTest, ApplyASI_PlusTwo) {
    auto sheet = makeLevel1Fighter();
    sheet.attributes.strength.base = 14;
    sheet.availableASIs = 1;

    bool ok = CharacterProgression::applyASI(sheet, AbilityType::Strength);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sheet.attributes.strength.base, 16);
    EXPECT_EQ(sheet.availableASIs, 0);
}

TEST_F(ProgressionTest, ApplyASI_PlusOnePlusOne) {
    auto sheet = makeLevel1Fighter();
    sheet.attributes.strength.base  = 14;
    sheet.attributes.dexterity.base = 12;
    sheet.availableASIs = 1;

    bool ok = CharacterProgression::applyASI(sheet, AbilityType::Strength, AbilityType::Dexterity);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sheet.attributes.strength.base, 15);
    EXPECT_EQ(sheet.attributes.dexterity.base, 13);
    EXPECT_EQ(sheet.availableASIs, 0);
}

TEST_F(ProgressionTest, ApplyASI_CapsAt20) {
    auto sheet = makeLevel1Fighter();
    sheet.attributes.strength.base = 19;
    sheet.availableASIs = 1;

    bool ok = CharacterProgression::applyASI(sheet, AbilityType::Strength);
    EXPECT_TRUE(ok);
    EXPECT_EQ(sheet.attributes.strength.base, 20);  // capped
}

TEST_F(ProgressionTest, ApplyASI_AlreadyAt20Fails) {
    auto sheet = makeLevel1Fighter();
    sheet.attributes.strength.base = 20;
    sheet.availableASIs = 1;

    bool ok = CharacterProgression::applyASI(sheet, AbilityType::Strength);
    EXPECT_FALSE(ok);
    EXPECT_EQ(sheet.availableASIs, 1);  // not consumed
}

TEST_F(ProgressionTest, ApplyASI_NoASIsAvailableFails) {
    auto sheet = makeLevel1Fighter();
    sheet.availableASIs = 0;
    bool ok = CharacterProgression::applyASI(sheet, AbilityType::Strength);
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// Award XP
// ---------------------------------------------------------------------------

TEST_F(ProgressionTest, AwardXP_NoAutoLevel) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    bool leveled = CharacterProgression::awardXP(sheet, 500, dice, false);
    EXPECT_FALSE(leveled);
    EXPECT_EQ(sheet.experiencePoints, 500);
    EXPECT_EQ(sheet.totalLevel(), 1);
}

TEST_F(ProgressionTest, AwardXP_AutoLevel) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    bool leveled = CharacterProgression::awardXP(sheet, 300, dice, true, true);
    EXPECT_TRUE(leveled);
    EXPECT_EQ(sheet.classes[0].level, 2);
}

// ---------------------------------------------------------------------------
// Short rest
// ---------------------------------------------------------------------------

TEST_F(ProgressionTest, ShortRest_RecoverHP) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    sheet.currentHP = 4;  // damaged
    // Pool: 1 d10 remaining, CON +2
    auto result = CharacterProgression::shortRest(sheet, 1, dice);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.hitDiceSpent, 1);
    EXPECT_GT(result.hpRecovered, 0);
    EXPECT_EQ(sheet.hitDicePools[0].remaining, 0);
}

TEST_F(ProgressionTest, ShortRest_CannotExceedMaxHP) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    sheet.currentHP = sheet.maxHP;  // already full
    auto result = CharacterProgression::shortRest(sheet, 1, dice);
    EXPECT_EQ(result.hpRecovered, 0);
    // Still spends the die (spent even when at full health in 5e)
    EXPECT_EQ(result.hitDiceSpent, 1);
    EXPECT_EQ(sheet.currentHP, sheet.maxHP);
}

TEST_F(ProgressionTest, ShortRest_NoDiceRemaining) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    sheet.currentHP = 4;
    sheet.hitDicePools[0].remaining = 0;
    auto result = CharacterProgression::shortRest(sheet, 1, dice);
    EXPECT_EQ(result.hitDiceSpent, 0);
    EXPECT_EQ(result.hpRecovered, 0);
}

// ---------------------------------------------------------------------------
// Long rest
// ---------------------------------------------------------------------------

TEST_F(ProgressionTest, LongRest_RestoresFullHP) {
    DiceSystem dice;
    auto sheet = makeLevel1Fighter();
    sheet.currentHP = 1;
    auto result = CharacterProgression::longRest(sheet);
    EXPECT_EQ(sheet.currentHP, sheet.maxHP);
    EXPECT_GT(result.hpRestored, 0);
}

TEST_F(ProgressionTest, LongRest_RestoresHalfHitDice) {
    DiceSystem dice;
    // Build a level-4 fighter who used all 4 hit dice
    auto sheet = makeLevel1Fighter();
    sheet.classes[0].level = 4;
    sheet.hitDicePools[0].total     = 4;
    sheet.hitDicePools[0].remaining = 0;

    auto result = CharacterProgression::longRest(sheet);
    // Recover ceil(4/2) = 2 hit dice
    EXPECT_EQ(result.hitDiceRestored, 2);
    EXPECT_EQ(sheet.hitDicePools[0].remaining, 2);
}

TEST_F(ProgressionTest, LongRest_MinimumOneHitDice) {
    auto sheet = makeLevel1Fighter();
    sheet.hitDicePools[0].total     = 1;
    sheet.hitDicePools[0].remaining = 0;

    auto result = CharacterProgression::longRest(sheet);
    EXPECT_EQ(result.hitDiceRestored, 1);  // min 1
    EXPECT_EQ(sheet.hitDicePools[0].remaining, 1);
}

TEST_F(ProgressionTest, LongRest_ResetsDeathSaves) {
    auto sheet = makeLevel1Fighter();
    sheet.currentHP = 1;
    sheet.deathSaves.successes = 2;
    sheet.deathSaves.failures  = 1;
    CharacterProgression::longRest(sheet);
    EXPECT_EQ(sheet.deathSaves.successes, 0);
    EXPECT_EQ(sheet.deathSaves.failures,  0);
}

TEST_F(ProgressionTest, LongRest_ClearsTemporaryHP) {
    auto sheet = makeLevel1Fighter();
    sheet.temporaryHP = 10;
    CharacterProgression::longRest(sheet);
    EXPECT_EQ(sheet.temporaryHP, 0);
}

// ---------------------------------------------------------------------------
// Hit dice helpers
// ---------------------------------------------------------------------------

TEST_F(ProgressionTest, TotalHitDiceHelpers) {
    auto sheet = makeLevel1Fighter();
    sheet.hitDicePools[0].total     = 5;
    sheet.hitDicePools[0].remaining = 3;

    EXPECT_EQ(CharacterProgression::totalHitDiceMax(sheet), 5);
    EXPECT_EQ(CharacterProgression::totalHitDiceRemaining(sheet), 3);
}

TEST_F(ProgressionTest, MulticlassHitDiceSummed) {
    auto sheet = makeLevel1Fighter();
    HitDicePool pool2; pool2.classId="wizard"; pool2.faces=6; pool2.total=3; pool2.remaining=2;
    sheet.hitDicePools.push_back(pool2);

    EXPECT_EQ(CharacterProgression::totalHitDiceMax(sheet), 4);       // 1+3
    EXPECT_EQ(CharacterProgression::totalHitDiceRemaining(sheet), 3); // 1+2
}
