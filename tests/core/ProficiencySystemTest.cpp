#include <gtest/gtest.h>
#include "core/ProficiencySystem.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Proficiency bonus scaling
// ---------------------------------------------------------------------------

TEST(ProficiencySystemTest, BonusAtLevel1Is2) {
    EXPECT_EQ(ProficiencySystem::proficiencyBonus(1), 2);
}

TEST(ProficiencySystemTest, BonusScalesByTier) {
    // PHB table
    struct Case { int level; int bonus; };
    Case cases[] = {
        {1,2},{2,2},{3,2},{4,2},
        {5,3},{6,3},{7,3},{8,3},
        {9,4},{10,4},{11,4},{12,4},
        {13,5},{14,5},{15,5},{16,5},
        {17,6},{18,6},{19,6},{20,6}
    };
    for (auto& c : cases) {
        EXPECT_EQ(ProficiencySystem::proficiencyBonus(c.level), c.bonus)
            << "Level=" << c.level;
    }
}

// ---------------------------------------------------------------------------
// Skill bonuses
// ---------------------------------------------------------------------------

TEST(ProficiencySystemTest, SkillBonusNoProficiency) {
    CharacterAttributes attrs;
    attrs.dexterity.base = 14;  // +2 modifier
    int bonus = ProficiencySystem::skillBonus(attrs, Skill::Stealth, ProficiencyLevel::None, 1);
    EXPECT_EQ(bonus, 2);  // just DEX mod
}

TEST(ProficiencySystemTest, SkillBonusProficient) {
    CharacterAttributes attrs;
    attrs.dexterity.base = 14;  // +2 modifier
    // Level 1: profBonus=2, total=4
    int bonus = ProficiencySystem::skillBonus(attrs, Skill::Stealth, ProficiencyLevel::Proficient, 1);
    EXPECT_EQ(bonus, 4);
}

TEST(ProficiencySystemTest, SkillBonusExpert) {
    CharacterAttributes attrs;
    attrs.dexterity.base = 14;  // +2 modifier
    // Level 1: profBonus=2, expert=×2, total=6
    int bonus = ProficiencySystem::skillBonus(attrs, Skill::Stealth, ProficiencyLevel::Expert, 1);
    EXPECT_EQ(bonus, 6);
}

TEST(ProficiencySystemTest, SkillBonusHalfProf) {
    CharacterAttributes attrs;
    attrs.wisdom.base = 12;  // +1 modifier
    // Level 1: profBonus=2, half=1, total=2
    int bonus = ProficiencySystem::skillBonus(attrs, Skill::Perception, ProficiencyLevel::HalfProf, 1);
    EXPECT_EQ(bonus, 2);
}

TEST(ProficiencySystemTest, SkillBonusScalesWithLevel) {
    CharacterAttributes attrs;
    attrs.charisma.base = 16;  // +3 modifier
    // Level 5: profBonus=3, total=6
    int bonus = ProficiencySystem::skillBonus(attrs, Skill::Persuasion, ProficiencyLevel::Proficient, 5);
    EXPECT_EQ(bonus, 6);
    // Level 17: profBonus=6, total=9
    bonus = ProficiencySystem::skillBonus(attrs, Skill::Persuasion, ProficiencyLevel::Proficient, 17);
    EXPECT_EQ(bonus, 9);
}

// ---------------------------------------------------------------------------
// Passive check
// ---------------------------------------------------------------------------

TEST(ProficiencySystemTest, PassiveCheck) {
    EXPECT_EQ(ProficiencySystem::passiveCheck(5), 15);
    EXPECT_EQ(ProficiencySystem::passiveCheck(-1), 9);
    EXPECT_EQ(ProficiencySystem::passiveCheck(0), 10);
}

// ---------------------------------------------------------------------------
// Saving throws
// ---------------------------------------------------------------------------

TEST(ProficiencySystemTest, SavingThrowNoProficiency) {
    CharacterAttributes attrs;
    attrs.constitution.base = 14;  // +2
    int bonus = ProficiencySystem::savingThrowBonus(attrs, AbilityType::Constitution, false, 1);
    EXPECT_EQ(bonus, 2);
}

TEST(ProficiencySystemTest, SavingThrowProficient) {
    CharacterAttributes attrs;
    attrs.constitution.base = 14;  // +2, profBonus=2 at level 1
    int bonus = ProficiencySystem::savingThrowBonus(attrs, AbilityType::Constitution, true, 1);
    EXPECT_EQ(bonus, 4);
}

TEST(ProficiencySystemTest, SavingThrowProficientHighLevel) {
    CharacterAttributes attrs;
    attrs.constitution.base = 16;  // +3, profBonus=6 at level 17
    int bonus = ProficiencySystem::savingThrowBonus(attrs, AbilityType::Constitution, true, 17);
    EXPECT_EQ(bonus, 9);
}

// ---------------------------------------------------------------------------
// Ability-to-skill mapping
// ---------------------------------------------------------------------------

TEST(ProficiencySystemTest, AbilityForSkillMapping) {
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Athletics),     AbilityType::Strength);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Acrobatics),    AbilityType::Dexterity);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::SleightOfHand), AbilityType::Dexterity);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Stealth),       AbilityType::Dexterity);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Arcana),        AbilityType::Intelligence);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::History),       AbilityType::Intelligence);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Investigation),  AbilityType::Intelligence);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Nature),        AbilityType::Intelligence);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Religion),      AbilityType::Intelligence);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::AnimalHandling),AbilityType::Wisdom);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Insight),       AbilityType::Wisdom);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Medicine),      AbilityType::Wisdom);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Perception),    AbilityType::Wisdom);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Survival),      AbilityType::Wisdom);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Deception),     AbilityType::Charisma);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Intimidation),  AbilityType::Charisma);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Performance),   AbilityType::Charisma);
    EXPECT_EQ(ProficiencySystem::abilityForSkill(Skill::Persuasion),    AbilityType::Charisma);
}

// ---------------------------------------------------------------------------
// Skill name helpers
// ---------------------------------------------------------------------------

TEST(ProficiencySystemTest, SkillNames) {
    EXPECT_STREQ(ProficiencySystem::skillName(Skill::Athletics),     "Athletics");
    EXPECT_STREQ(ProficiencySystem::skillName(Skill::SleightOfHand), "Sleight of Hand");
    EXPECT_STREQ(ProficiencySystem::skillName(Skill::AnimalHandling),"Animal Handling");
    EXPECT_STREQ(ProficiencySystem::skillName(Skill::Persuasion),    "Persuasion");
}

TEST(ProficiencySystemTest, SkillFromString) {
    EXPECT_EQ(ProficiencySystem::skillFromString("Athletics"),     Skill::Athletics);
    EXPECT_EQ(ProficiencySystem::skillFromString("Stealth"),       Skill::Stealth);
    EXPECT_EQ(ProficiencySystem::skillFromString("Sleight of Hand"), Skill::SleightOfHand);
    EXPECT_EQ(ProficiencySystem::skillFromString("Animal Handling"), Skill::AnimalHandling);
    EXPECT_EQ(ProficiencySystem::skillFromString("Perception"),    Skill::Perception);
}

TEST(ProficiencySystemTest, SkillFromStringCaseInsensitive) {
    EXPECT_EQ(ProficiencySystem::skillFromString("athletics"),  Skill::Athletics);
    EXPECT_EQ(ProficiencySystem::skillFromString("STEALTH"),    Skill::Stealth);
    EXPECT_EQ(ProficiencySystem::skillFromString("persuasion"), Skill::Persuasion);
}

TEST(ProficiencySystemTest, SkillFromStringUnknownThrows) {
    EXPECT_THROW(ProficiencySystem::skillFromString("Juggling"), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// ALL_SKILLS sentinel
// ---------------------------------------------------------------------------

TEST(ProficiencySystemTest, AllSkillsHas18Entries) {
    // Verify every entry is unique and maps to a valid ability
    std::set<Skill> seen;
    for (int i = 0; i < 18; ++i) {
        Skill s = ProficiencySystem::ALL_SKILLS[i];
        EXPECT_EQ(seen.count(s), 0u) << "Duplicate skill at index " << i;
        seen.insert(s);
        // Should not throw
        EXPECT_NO_THROW(ProficiencySystem::abilityForSkill(s));
    }
    EXPECT_EQ(seen.size(), 18u);
}

// ---------------------------------------------------------------------------
// Bonus string formatting
// ---------------------------------------------------------------------------

TEST(ProficiencySystemTest, BonusString) {
    EXPECT_EQ(ProficiencySystem::bonusString(5),  "+5");
    EXPECT_EQ(ProficiencySystem::bonusString(0),  "+0");
    EXPECT_EQ(ProficiencySystem::bonusString(-2), "-2");
}
