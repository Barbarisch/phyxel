#include <gtest/gtest.h>
#include "core/CharacterSheet.h"
#include "core/ClassDefinition.h"
#include "core/RaceDefinition.h"
#include "core/ProficiencySystem.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Helpers: register a minimal fighter class and human race for factory tests
// ---------------------------------------------------------------------------

static void registerTestClass() {
    if (ClassRegistry::instance().hasClass("fighter")) return;
    ClassDefinition c;
    c.id          = "fighter";
    c.name        = "Fighter";
    c.hitDieFaces = 10;
    c.primaryAbility = AbilityType::Strength;
    c.savingThrowProficiencies = {AbilityType::Strength, AbilityType::Constitution};
    c.armorProficiencies  = {"light","medium","heavy","shields"};
    c.weaponProficiencies = {"simple","martial"};
    c.skillChoices = 2;
    c.skillPool = {Skill::Athletics, Skill::Perception};
    c.asiLevels = {4,8,12,16,19};
    c.features[1] = {{"second_wind","Second Wind","Regain HP as bonus action.",1},
                     {"fighting_style","Fighting Style","Choose a style.",1}};
    c.features[4] = {{"asi","Ability Score Improvement","Increase scores.",4}};
    ClassRegistry::instance().registerClass(c);
}

static void registerTestRace() {
    if (RaceRegistry::instance().hasRace("human")) return;
    RaceDefinition r;
    r.id    = "human";
    r.name  = "Human";
    r.speed = 30;
    r.abilityBonuses = {
        {AbilityType::Strength,1}, {AbilityType::Dexterity,1}, {AbilityType::Constitution,1},
        {AbilityType::Intelligence,1}, {AbilityType::Wisdom,1}, {AbilityType::Charisma,1}
    };
    r.languages = {"Common"};
    RaceRegistry::instance().registerRace(r);
}

// ---------------------------------------------------------------------------
// HitDicePool
// ---------------------------------------------------------------------------

TEST(HitDicePoolTest, JsonRoundTrip) {
    HitDicePool p;
    p.classId   = "fighter";
    p.faces     = 10;
    p.total     = 5;
    p.remaining = 3;

    auto j = p.toJson();
    HitDicePool p2;
    p2.fromJson(j);

    EXPECT_EQ(p2.classId, "fighter");
    EXPECT_EQ(p2.faces, 10);
    EXPECT_EQ(p2.total, 5);
    EXPECT_EQ(p2.remaining, 3);
}

// ---------------------------------------------------------------------------
// DeathSaveState
// ---------------------------------------------------------------------------

TEST(DeathSaveStateTest, ThreeSuccessesIsStable) {
    DeathSaveState ds;
    ds.successes = 3;
    EXPECT_TRUE(ds.isStable());
    EXPECT_FALSE(ds.isDead());
}

TEST(DeathSaveStateTest, ThreeFailuresIsDead) {
    DeathSaveState ds;
    ds.failures = 3;
    EXPECT_TRUE(ds.isDead());
    EXPECT_FALSE(ds.isStable());
}

TEST(DeathSaveStateTest, StableFlag) {
    DeathSaveState ds;
    ds.stable = true;
    EXPECT_TRUE(ds.isStable());
}

TEST(DeathSaveStateTest, Reset) {
    DeathSaveState ds;
    ds.successes = 2;
    ds.failures  = 2;
    ds.stable    = true;
    ds.reset();
    EXPECT_EQ(ds.successes, 0);
    EXPECT_EQ(ds.failures, 0);
    EXPECT_FALSE(ds.stable);
}

TEST(DeathSaveStateTest, JsonRoundTrip) {
    DeathSaveState ds;
    ds.successes = 1;
    ds.failures  = 2;
    ds.stable    = false;

    auto j = ds.toJson();
    DeathSaveState ds2;
    ds2.fromJson(j);

    EXPECT_EQ(ds2.successes, 1);
    EXPECT_EQ(ds2.failures, 2);
    EXPECT_FALSE(ds2.stable);
}

// ---------------------------------------------------------------------------
// CharacterSheet — construction defaults
// ---------------------------------------------------------------------------

TEST(CharacterSheetTest, DefaultValues) {
    CharacterSheet s;
    EXPECT_EQ(s.totalLevel(), 1);  // max(1, 0)
    EXPECT_EQ(s.maxHP, 0);
    EXPECT_EQ(s.armorClass, 10);
    EXPECT_EQ(s.speed, 30);
    EXPECT_TRUE(s.classes.empty());
}

// ---------------------------------------------------------------------------
// CharacterSheet — totalLevel / proficiencyBonus
// ---------------------------------------------------------------------------

TEST(CharacterSheetTest, TotalLevelSingleClass) {
    CharacterSheet s;
    s.classes = {{"fighter", 5, ""}};
    EXPECT_EQ(s.totalLevel(), 5);
    EXPECT_EQ(s.proficiencyBonus(), 3);
}

TEST(CharacterSheetTest, TotalLevelMulticlass) {
    CharacterSheet s;
    s.classes = {{"fighter", 5, ""}, {"wizard", 3, ""}};
    EXPECT_EQ(s.totalLevel(), 8);
    EXPECT_EQ(s.proficiencyBonus(), 3);
}

// ---------------------------------------------------------------------------
// CharacterSheet — skill bonuses
// ---------------------------------------------------------------------------

TEST(CharacterSheetTest, SkillBonusNoProficiency) {
    CharacterSheet s;
    s.classes = {{"fighter", 1, ""}};
    s.attributes.dexterity.base = 14;  // +2 modifier
    EXPECT_EQ(s.skillBonus(Skill::Stealth), 2);
}

TEST(CharacterSheetTest, SkillBonusProficient) {
    CharacterSheet s;
    s.classes = {{"fighter", 1, ""}};
    s.attributes.dexterity.base = 14;  // +2 modifier
    s.skills.push_back({Skill::Stealth, ProficiencyLevel::Proficient});
    // Level 1: profBonus=2, total=4
    EXPECT_EQ(s.skillBonus(Skill::Stealth), 4);
}

TEST(CharacterSheetTest, SkillBonusExpert) {
    CharacterSheet s;
    s.classes = {{"rogue", 1, ""}};
    s.attributes.dexterity.base = 16;  // +3 modifier
    s.skills.push_back({Skill::Stealth, ProficiencyLevel::Expert});
    // Level 1: profBonus=2, expert×2=4, total=7
    EXPECT_EQ(s.skillBonus(Skill::Stealth), 7);
}

// ---------------------------------------------------------------------------
// CharacterSheet — saving throws
// ---------------------------------------------------------------------------

TEST(CharacterSheetTest, SavingThrowNoProficiency) {
    CharacterSheet s;
    s.classes = {{"fighter", 1, ""}};
    s.attributes.dexterity.base = 14;  // +2
    EXPECT_EQ(s.savingThrowBonus(AbilityType::Dexterity), 2);
}

TEST(CharacterSheetTest, SavingThrowProficient) {
    CharacterSheet s;
    s.classes = {{"fighter", 1, ""}};
    s.attributes.strength.base = 16;  // +3
    s.savingThrowProficiencies.insert(AbilityType::Strength);
    // prof=2, total=5
    EXPECT_EQ(s.savingThrowBonus(AbilityType::Strength), 5);
}

// ---------------------------------------------------------------------------
// CharacterSheet — passive perception
// ---------------------------------------------------------------------------

TEST(CharacterSheetTest, PassivePerception) {
    CharacterSheet s;
    s.classes = {{"fighter", 1, ""}};
    s.attributes.wisdom.base = 14;  // +2 modifier
    // Not proficient: 10+2=12
    EXPECT_EQ(s.passivePerception(), 12);

    s.skills.push_back({Skill::Perception, ProficiencyLevel::Proficient});
    // Proficient: 10+2+2=14
    EXPECT_EQ(s.passivePerception(), 14);
}

// ---------------------------------------------------------------------------
// CharacterSheet — HP helpers
// ---------------------------------------------------------------------------

TEST(CharacterSheetTest, TakeDamageNoTemp) {
    CharacterSheet s;
    s.maxHP     = 50;
    s.currentHP = 50;
    int dealt = s.takeDamage(20);
    EXPECT_EQ(dealt, 20);
    EXPECT_EQ(s.currentHP, 30);
    EXPECT_EQ(s.temporaryHP, 0);
}

TEST(CharacterSheetTest, TakeDamageBurnsTempFirst) {
    CharacterSheet s;
    s.maxHP      = 50;
    s.currentHP  = 50;
    s.temporaryHP = 10;
    // Deal 15: 10 from temp, 5 from current
    int dealt = s.takeDamage(15);
    EXPECT_EQ(dealt, 5);
    EXPECT_EQ(s.temporaryHP, 0);
    EXPECT_EQ(s.currentHP, 45);
}

TEST(CharacterSheetTest, TakeDamageAbsorbedByTemp) {
    CharacterSheet s;
    s.maxHP      = 50;
    s.currentHP  = 50;
    s.temporaryHP = 20;
    // Deal 10: fully absorbed by temp
    int dealt = s.takeDamage(10);
    EXPECT_EQ(dealt, 0);
    EXPECT_EQ(s.temporaryHP, 10);
    EXPECT_EQ(s.currentHP, 50);
}

TEST(CharacterSheetTest, HealCapsAtMaxHP) {
    CharacterSheet s;
    s.maxHP     = 50;
    s.currentHP = 30;
    int healed = s.heal(40);
    EXPECT_EQ(healed, 20);
    EXPECT_EQ(s.currentHP, 50);
}

TEST(CharacterSheetTest, HealDoesNothingAtFull) {
    CharacterSheet s;
    s.maxHP     = 50;
    s.currentHP = 50;
    int healed = s.heal(10);
    EXPECT_EQ(healed, 0);
}

TEST(CharacterSheetTest, GrantTemporaryHPTakesHigher) {
    CharacterSheet s;
    s.temporaryHP = 5;
    s.grantTemporaryHP(8);
    EXPECT_EQ(s.temporaryHP, 8);

    s.grantTemporaryHP(3);  // lower, no change
    EXPECT_EQ(s.temporaryHP, 8);
}

// ---------------------------------------------------------------------------
// CharacterSheet — recalculate
// ---------------------------------------------------------------------------

TEST(CharacterSheetTest, RecalculateInitiative) {
    CharacterSheet s;
    s.attributes.dexterity.base = 16;  // +3
    s.recalculate();
    EXPECT_EQ(s.initiative, 3);
}

// ---------------------------------------------------------------------------
// CharacterSheet — factory
// ---------------------------------------------------------------------------

TEST(CharacterSheetTest, CreateFromRegistries) {
    registerTestClass();
    registerTestRace();

    CharacterSheet s = CharacterSheet::create("Aldric", "human", "fighter");

    EXPECT_EQ(s.name, "Aldric");
    EXPECT_EQ(s.raceId, "human");
    EXPECT_EQ(s.classes.size(), 1u);
    EXPECT_EQ(s.classes[0].classId, "fighter");
    EXPECT_EQ(s.classes[0].level, 1);
    EXPECT_EQ(s.speed, 30);
    EXPECT_GT(s.maxHP, 0);
    EXPECT_EQ(s.currentHP, s.maxHP);

    // Human gives +1 to all attributes
    EXPECT_EQ(s.attributes.strength.racial, 1);
    EXPECT_EQ(s.attributes.dexterity.racial, 1);

    // Fighter saves: STR and CON
    EXPECT_TRUE(s.hasSavingThrowProficiency(AbilityType::Strength));
    EXPECT_TRUE(s.hasSavingThrowProficiency(AbilityType::Constitution));
    EXPECT_FALSE(s.hasSavingThrowProficiency(AbilityType::Intelligence));

    // Level-1 features
    EXPECT_NE(std::find(s.earnedFeatureIds.begin(), s.earnedFeatureIds.end(), "second_wind"),
              s.earnedFeatureIds.end());

    // Hit dice pool
    ASSERT_EQ(s.hitDicePools.size(), 1u);
    EXPECT_EQ(s.hitDicePools[0].classId, "fighter");
    EXPECT_EQ(s.hitDicePools[0].faces, 10);
    EXPECT_EQ(s.hitDicePools[0].total, 1);
    EXPECT_EQ(s.hitDicePools[0].remaining, 1);

    // Languages
    EXPECT_FALSE(s.languages.empty());
}

// ---------------------------------------------------------------------------
// CharacterSheet — JSON round-trip
// ---------------------------------------------------------------------------

TEST(CharacterSheetTest, JsonRoundTrip) {
    registerTestClass();
    registerTestRace();

    CharacterSheet s = CharacterSheet::create("Torinn", "human", "fighter");
    s.attributes.strength.base = 16;
    s.skills.push_back({Skill::Athletics, ProficiencyLevel::Proficient});
    s.savingThrowProficiencies.insert(AbilityType::Dexterity);
    s.experiencePoints = 450;
    s.recalculate();

    auto j = s.toJson();
    CharacterSheet s2;
    s2.fromJson(j);

    EXPECT_EQ(s2.name, s.name);
    EXPECT_EQ(s2.raceId, s.raceId);
    EXPECT_EQ(s2.classes[0].classId, "fighter");
    EXPECT_EQ(s2.classes[0].level, 1);
    EXPECT_EQ(s2.attributes.strength.base, 16);
    EXPECT_EQ(s2.experiencePoints, 450);
    EXPECT_EQ(s2.maxHP, s.maxHP);
    EXPECT_EQ(s2.currentHP, s.currentHP);
    EXPECT_EQ(s2.speed, 30);
    EXPECT_TRUE(s2.hasSavingThrowProficiency(AbilityType::Strength));
    EXPECT_TRUE(s2.hasSavingThrowProficiency(AbilityType::Dexterity));
    EXPECT_TRUE(s2.isProficientWith(Skill::Athletics));
    EXPECT_FALSE(s2.isProficientWith(Skill::Stealth));
}
