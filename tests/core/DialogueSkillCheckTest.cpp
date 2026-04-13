#include <gtest/gtest.h>
#include "core/DialogueSkillCheck.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// CheckType name helpers
// ---------------------------------------------------------------------------

TEST(DialogueSkillCheckTest, CheckTypeRoundTrip) {
    const DialogueCheckType types[] = {
        DialogueCheckType::SkillCheck,
        DialogueCheckType::AbilityCheck,
        DialogueCheckType::ReputationGate
    };
    for (auto t : types) {
        const char* n = dialogueCheckTypeName(t);
        EXPECT_EQ(dialogueCheckTypeFromString(n), t) << "Failed for: " << n;
    }
}

TEST(DialogueSkillCheckTest, CheckTypeFromStringCaseInsensitive) {
    EXPECT_EQ(dialogueCheckTypeFromString("skillcheck"),    DialogueCheckType::SkillCheck);
    EXPECT_EQ(dialogueCheckTypeFromString("ABILITYCHECK"),  DialogueCheckType::AbilityCheck);
    EXPECT_EQ(dialogueCheckTypeFromString("reputationgate"),DialogueCheckType::ReputationGate);
}

// ---------------------------------------------------------------------------
// resolve — SkillCheck
// ---------------------------------------------------------------------------

TEST(DialogueSkillCheckTest, SkillCheckPassWhenTotalMeetsDC) {
    DiceSystem dice;
    DiceSystem::setSeed(1);

    DialogueSkillCheck check;
    check.type  = DialogueCheckType::SkillCheck;
    check.skill = Skill::Persuasion;
    check.dc    = 1;  // DC 1 — should always pass

    auto result = check.resolve(0, dice);
    EXPECT_TRUE(result.passed);
    EXPECT_GE(result.total, 1);
    DiceSystem::setSeed(0);
}

TEST(DialogueSkillCheckTest, SkillCheckFailWhenTotalBelowDC) {
    DiceSystem dice;
    DiceSystem::setSeed(1);

    DialogueSkillCheck check;
    check.type  = DialogueCheckType::SkillCheck;
    check.skill = Skill::Persuasion;
    check.dc    = 100;  // DC 100 — should always fail

    auto result = check.resolve(0, dice);
    EXPECT_FALSE(result.passed);
    DiceSystem::setSeed(0);
}

TEST(DialogueSkillCheckTest, SkillCheckBonusAdded) {
    DiceSystem dice;
    DiceSystem::setSeed(1);

    DialogueSkillCheck check;
    check.type  = DialogueCheckType::SkillCheck;
    check.dc    = 50;

    // With +50 bonus, should pass
    auto result = check.resolve(50, dice);
    EXPECT_GE(result.total, result.roll + 50);
    DiceSystem::setSeed(0);
}

TEST(DialogueSkillCheckTest, SkillCheckResultFields) {
    DiceSystem dice;
    DiceSystem::setSeed(42);

    DialogueSkillCheck check;
    check.dc = 15;
    auto result = check.resolve(3, dice);

    EXPECT_EQ(result.bonus, 3);
    EXPECT_EQ(result.dc, 15);
    EXPECT_EQ(result.total, result.roll + 3);
    EXPECT_EQ(result.passed, result.total >= 15);
    DiceSystem::setSeed(0);
}

// ---------------------------------------------------------------------------
// resolveReputation — ReputationGate
// ---------------------------------------------------------------------------

TEST(DialogueSkillCheckTest, ReputationGatePassAtOrAboveMinimum) {
    DialogueSkillCheck check;
    check.type        = DialogueCheckType::ReputationGate;
    check.minimumTier = ReputationTier::Friendly;  // score >= 250

    EXPECT_TRUE(check.resolveReputation(250));   // exactly Friendly
    EXPECT_TRUE(check.resolveReputation(500));   // Honored
    EXPECT_TRUE(check.resolveReputation(1000));  // Exalted
}

TEST(DialogueSkillCheckTest, ReputationGateFailBelowMinimum) {
    DialogueSkillCheck check;
    check.type        = DialogueCheckType::ReputationGate;
    check.minimumTier = ReputationTier::Friendly;

    EXPECT_FALSE(check.resolveReputation(0));    // Neutral
    EXPECT_FALSE(check.resolveReputation(-100)); // Unfriendly
    EXPECT_FALSE(check.resolveReputation(-600)); // Hostile
}

TEST(DialogueSkillCheckTest, ReputationGateHonored) {
    DialogueSkillCheck check;
    check.type        = DialogueCheckType::ReputationGate;
    check.minimumTier = ReputationTier::Honored;

    EXPECT_FALSE(check.resolveReputation(300));  // Friendly — not enough
    EXPECT_TRUE(check.resolveReputation(500));   // exactly Honored
}

// ---------------------------------------------------------------------------
// label
// ---------------------------------------------------------------------------

TEST(DialogueSkillCheckTest, LabelSkillCheck) {
    DialogueSkillCheck check;
    check.type  = DialogueCheckType::SkillCheck;
    check.skill = Skill::Persuasion;
    check.dc    = 15;

    std::string lbl = check.label();
    EXPECT_NE(lbl.find("Persuasion"), std::string::npos);
    EXPECT_NE(lbl.find("15"),         std::string::npos);
}

TEST(DialogueSkillCheckTest, LabelAbilityCheck) {
    DialogueSkillCheck check;
    check.type    = DialogueCheckType::AbilityCheck;
    check.ability = AbilityType::Intelligence;
    check.dc      = 12;

    std::string lbl = check.label();
    EXPECT_NE(lbl.find("INT"), std::string::npos);
    EXPECT_NE(lbl.find("12"),  std::string::npos);
}

TEST(DialogueSkillCheckTest, LabelReputationGate) {
    DialogueSkillCheck check;
    check.type        = DialogueCheckType::ReputationGate;
    check.factionId   = "merchants_guild";
    check.minimumTier = ReputationTier::Friendly;

    std::string lbl = check.label();
    EXPECT_NE(lbl.find("Friendly"),        std::string::npos);
    EXPECT_NE(lbl.find("merchants_guild"), std::string::npos);
}

TEST(DialogueSkillCheckTest, LabelUsesDescriptionIfSet) {
    DialogueSkillCheck check;
    check.type        = DialogueCheckType::SkillCheck;
    check.description = "[Bribe the Guard]";

    EXPECT_EQ(check.label(), "[Bribe the Guard]");
}

// ---------------------------------------------------------------------------
// JSON round-trip
// ---------------------------------------------------------------------------

TEST(DialogueSkillCheckTest, JsonRoundTripSkillCheck) {
    DialogueSkillCheck original;
    original.type       = DialogueCheckType::SkillCheck;
    original.skill      = Skill::Deception;
    original.dc         = 18;
    original.showOnFail = false;
    original.description = "[Lie convincingly]";

    auto j = original.toJson();
    auto restored = DialogueSkillCheck::fromJson(j);

    EXPECT_EQ(restored.type,       DialogueCheckType::SkillCheck);
    EXPECT_EQ(restored.skill,      Skill::Deception);
    EXPECT_EQ(restored.dc,         18);
    EXPECT_FALSE(restored.showOnFail);
    EXPECT_EQ(restored.description, "[Lie convincingly]");
}

TEST(DialogueSkillCheckTest, JsonRoundTripAbilityCheck) {
    DialogueSkillCheck original;
    original.type    = DialogueCheckType::AbilityCheck;
    original.ability = AbilityType::Strength;
    original.dc      = 14;

    auto j = original.toJson();
    auto restored = DialogueSkillCheck::fromJson(j);

    EXPECT_EQ(restored.type,    DialogueCheckType::AbilityCheck);
    EXPECT_EQ(restored.ability, AbilityType::Strength);
    EXPECT_EQ(restored.dc,      14);
}

TEST(DialogueSkillCheckTest, JsonRoundTripReputationGate) {
    DialogueSkillCheck original;
    original.type        = DialogueCheckType::ReputationGate;
    original.factionId   = "city_guard";
    original.minimumTier = ReputationTier::Honored;
    original.dc          = 0;  // not used for gates

    auto j = original.toJson();
    auto restored = DialogueSkillCheck::fromJson(j);

    EXPECT_EQ(restored.type,        DialogueCheckType::ReputationGate);
    EXPECT_EQ(restored.factionId,   "city_guard");
    EXPECT_EQ(restored.minimumTier, ReputationTier::Honored);
}
