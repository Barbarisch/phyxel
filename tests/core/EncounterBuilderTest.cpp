#include <gtest/gtest.h>
#include "core/EncounterBuilder.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

TEST(EncounterBuilderTest, DifficultyRoundTrip) {
    const EncounterDifficulty difficulties[] = {
        EncounterDifficulty::Easy, EncounterDifficulty::Medium,
        EncounterDifficulty::Hard, EncounterDifficulty::Deadly
    };
    for (auto d : difficulties) {
        const char* n = encounterDifficultyName(d);
        EXPECT_EQ(encounterDifficultyFromString(n), d) << "Failed for: " << n;
    }
}

TEST(EncounterBuilderTest, DifficultyFromStringCaseInsensitive) {
    EXPECT_EQ(encounterDifficultyFromString("easy"),   EncounterDifficulty::Easy);
    EXPECT_EQ(encounterDifficultyFromString("DEADLY"), EncounterDifficulty::Deadly);
    EXPECT_EQ(encounterDifficultyFromString("unknown"),EncounterDifficulty::Medium);
}

// ---------------------------------------------------------------------------
// XP threshold table
// ---------------------------------------------------------------------------

TEST(EncounterBuilderTest, XpThresholdLevel1) {
    EXPECT_EQ(EncounterBuilder::xpThreshold(1, EncounterDifficulty::Easy),   25);
    EXPECT_EQ(EncounterBuilder::xpThreshold(1, EncounterDifficulty::Medium), 50);
    EXPECT_EQ(EncounterBuilder::xpThreshold(1, EncounterDifficulty::Hard),   75);
    EXPECT_EQ(EncounterBuilder::xpThreshold(1, EncounterDifficulty::Deadly), 100);
}

TEST(EncounterBuilderTest, XpThresholdLevel5) {
    EXPECT_EQ(EncounterBuilder::xpThreshold(5, EncounterDifficulty::Easy),   250);
    EXPECT_EQ(EncounterBuilder::xpThreshold(5, EncounterDifficulty::Deadly), 1100);
}

TEST(EncounterBuilderTest, XpThresholdLevel20) {
    EXPECT_EQ(EncounterBuilder::xpThreshold(20, EncounterDifficulty::Easy),   2800);
    EXPECT_EQ(EncounterBuilder::xpThreshold(20, EncounterDifficulty::Deadly), 12700);
}

TEST(EncounterBuilderTest, XpThresholdClampsBelowOne) {
    EXPECT_EQ(EncounterBuilder::xpThreshold(0,  EncounterDifficulty::Easy),
              EncounterBuilder::xpThreshold(1,  EncounterDifficulty::Easy));
}

TEST(EncounterBuilderTest, XpThresholdClampsAbove20) {
    EXPECT_EQ(EncounterBuilder::xpThreshold(21, EncounterDifficulty::Easy),
              EncounterBuilder::xpThreshold(20, EncounterDifficulty::Easy));
}

TEST(EncounterBuilderTest, ThresholdsIncreaseWithDifficulty) {
    for (int lvl = 1; lvl <= 20; ++lvl) {
        EXPECT_LT(EncounterBuilder::xpThreshold(lvl, EncounterDifficulty::Easy),
                  EncounterBuilder::xpThreshold(lvl, EncounterDifficulty::Medium));
        EXPECT_LT(EncounterBuilder::xpThreshold(lvl, EncounterDifficulty::Medium),
                  EncounterBuilder::xpThreshold(lvl, EncounterDifficulty::Hard));
        EXPECT_LT(EncounterBuilder::xpThreshold(lvl, EncounterDifficulty::Hard),
                  EncounterBuilder::xpThreshold(lvl, EncounterDifficulty::Deadly));
    }
}

// ---------------------------------------------------------------------------
// Multiplier table
// ---------------------------------------------------------------------------

TEST(EncounterBuilderTest, MultiplierByCount) {
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(0),  0.0f);
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(1),  1.0f);
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(2),  1.5f);
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(3),  2.0f);
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(6),  2.0f);
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(7),  2.5f);
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(10), 2.5f);
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(11), 3.0f);
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(14), 3.0f);
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(15), 4.0f);
    EXPECT_FLOAT_EQ(EncounterBuilder::encounterMultiplier(50), 4.0f);
}

// ---------------------------------------------------------------------------
// Encounter — computed properties
// ---------------------------------------------------------------------------

TEST(EncounterBuilderTest, TotalMonsterCount) {
    Encounter e;
    e.monsters.push_back({"goblin", "Goblin", 50, 3, 25});
    e.monsters.push_back({"orc",    "Orc",   100, 2, 50});
    EXPECT_EQ(e.totalMonsterCount(), 5);
}

TEST(EncounterBuilderTest, TotalMonsterXp) {
    Encounter e;
    e.monsters.push_back({"goblin", "Goblin", 50,  3, 25});  // 150
    e.monsters.push_back({"orc",    "Orc",   100,  2, 50});  // 200
    EXPECT_EQ(e.totalMonsterXp(), 350);
}

TEST(EncounterBuilderTest, AdjustedXpAppliesMultiplier) {
    Encounter e;
    e.monsters.push_back({"goblin", "Goblin", 100, 3, 25});  // 300 × 2.0 = 600
    EXPECT_EQ(e.totalMonsterXp(), 300);
    EXPECT_FLOAT_EQ(e.getMultiplier(), 2.0f);
    EXPECT_EQ(e.adjustedXp(), 600);
}

// ---------------------------------------------------------------------------
// EncounterBuilder — fluent API
// ---------------------------------------------------------------------------

TEST(EncounterBuilderTest, FluentBuildSetsFields) {
    auto enc = EncounterBuilder()
        .setId("enc_01")
        .setName("Goblin Ambush")
        .setDescription("Goblins attack from the trees")
        .setLootTable("goblin_hoard")
        .addMonster("goblin", "Goblin", 50, 4, 25)
        .build();

    EXPECT_EQ(enc.id,          "enc_01");
    EXPECT_EQ(enc.name,        "Goblin Ambush");
    EXPECT_EQ(enc.lootTableId, "goblin_hoard");
    EXPECT_EQ(enc.monsters.size(), 1u);
    EXPECT_EQ(enc.monsters[0].count, 4);
}

// ---------------------------------------------------------------------------
// Budget calculation
// ---------------------------------------------------------------------------

TEST(EncounterBuilderTest, BudgetForSingleLevel5Character) {
    Party p;
    p.addMember("hero", "Hero", 5);
    auto budget = EncounterBuilder::calculateBudget(p);

    EXPECT_EQ(budget.easyXp,   250);
    EXPECT_EQ(budget.mediumXp, 500);
    EXPECT_EQ(budget.hardXp,   750);
    EXPECT_EQ(budget.deadlyXp, 1100);
}

TEST(EncounterBuilderTest, BudgetSumsAllPartyMembers) {
    Party p;
    p.addMember("a", "A", 3);  // easy=75
    p.addMember("b", "B", 3);  // easy=75
    auto budget = EncounterBuilder::calculateBudget(p);
    EXPECT_EQ(budget.easyXp, 150);
}

TEST(EncounterBuilderTest, BudgetExcludesDeadMembers) {
    Party p;
    p.addMember("a", "A", 5);
    p.addMember("b", "B", 5);
    p.setAlive("b", false);

    auto full   = EncounterBuilder::calculateBudget(p);
    // Only alice contributes
    auto solo   = EncounterBuilder::calculateBudget([]{
        Party q; q.addMember("x", "X", 5); return q; }());
    EXPECT_EQ(full.easyXp, solo.easyXp);
}

// ---------------------------------------------------------------------------
// Difficulty evaluation
// ---------------------------------------------------------------------------

TEST(EncounterBuilderTest, EvaluateDifficultyEasy) {
    Party p;
    p.addMember("hero", "Hero", 5);  // easy threshold = 250

    // One goblin (50 XP) × 1.0 mult = 50 → Easy
    auto enc = EncounterBuilder()
        .addMonster("goblin", "Goblin", 50, 1, 25)
        .build();

    EXPECT_EQ(EncounterBuilder::evaluateDifficulty(enc, p), EncounterDifficulty::Easy);
}

TEST(EncounterBuilderTest, EvaluateDifficultyDeadly) {
    Party p;
    p.addMember("hero", "Hero", 1);  // deadly = 100

    // One 200XP monster × 1.0 mult = 200 → Deadly
    auto enc = EncounterBuilder()
        .addMonster("troll", "Troll", 200, 1, 100)
        .build();

    EXPECT_EQ(EncounterBuilder::evaluateDifficulty(enc, p), EncounterDifficulty::Deadly);
}

TEST(EncounterBuilderTest, IsValidForDifficulty) {
    Party p;
    p.addMember("hero", "Hero", 5);  // medium = 500

    // ~500 XP adjusted → Medium
    auto enc = EncounterBuilder()
        .addMonster("orc", "Orc", 500, 1, 100)
        .build();

    EXPECT_TRUE(EncounterBuilder::isValidForDifficulty(enc, p, EncounterDifficulty::Medium) ||
                EncounterBuilder::isValidForDifficulty(enc, p, EncounterDifficulty::Hard));
    EXPECT_FALSE(EncounterBuilder::isValidForDifficulty(enc, p, EncounterDifficulty::Easy));
}

// ---------------------------------------------------------------------------
// Encounter JSON
// ---------------------------------------------------------------------------

TEST(EncounterBuilderTest, EncounterJsonRoundTrip) {
    auto original = EncounterBuilder()
        .setId("enc_goblin")
        .setName("Goblin Scout")
        .setLootTable("goblin_hoard")
        .addMonster("goblin", "Goblin", 50, 5, 25)
        .addMonster("hobgoblin", "Hobgoblin", 200, 1, 100)
        .build();

    auto j = original.toJson();
    auto restored = Encounter::fromJson(j);

    EXPECT_EQ(restored.id,          "enc_goblin");
    EXPECT_EQ(restored.name,        "Goblin Scout");
    EXPECT_EQ(restored.lootTableId, "goblin_hoard");
    ASSERT_EQ(restored.monsters.size(), 2u);
    EXPECT_EQ(restored.monsters[0].count, 5);
    EXPECT_EQ(restored.monsters[1].xpValue, 200);
}

TEST(EncounterBuilderTest, MonsterEntryJsonRoundTrip) {
    MonsterEntry m { "troll", "Troll", 700, 2, 500 };
    auto j = m.toJson();
    auto r = MonsterEntry::fromJson(j);
    EXPECT_EQ(r.monsterId,       "troll");
    EXPECT_EQ(r.xpValue,          700);
    EXPECT_EQ(r.count,              2);
    EXPECT_EQ(r.challengeRating,  500);
    EXPECT_EQ(r.totalXp(),       1400);
}
