// ProgressionHudTest.cpp -- the XP readout the player actually sees (G-172).
//
// RED-BEFORE-GREEN. The user reports "there should be an XP bar and killing nPCs should grant
// xp, completing quests should grant xp". The awarding path already exists in the shipped game
// (a death event and ObjectiveTracker::onCompleted both call CharacterProgression::awardXP, and
// game.json sets kill_xp 50 / objective_xp 75) -- but there is NO XP anywhere in the HUD, so a
// player has no way to know any of it happened. These tests pin the arithmetic behind the bar.
//
// The bracket deliberately follows the character's ACTUAL level rather than the level its XP
// would buy; see ProgressionHud.h for why.

#include <gtest/gtest.h>

#include "core/ProgressionHud.h"
#include "core/CharacterSheet.h"
#include "core/CharacterProgression.h"

using namespace Phyxel::Core;

namespace {

// A sheet at a given level with a given XP total. One class level is what makes totalLevel()
// report anything, so the fixture builds that explicitly rather than hoping for a default.
CharacterSheet sheetAt(int level, int xp) {
    CharacterSheet s;
    s.name = "test";
    ClassLevel cl;
    cl.classId = "fighter";
    cl.level   = level;
    s.classes.clear();
    if (level > 0) s.classes.push_back(cl);
    s.experiencePoints = xp;
    return s;
}

}  // namespace

TEST(ProgressionHud, AFreshCharacterShowsAnEmptyBarTowardsLevelTwo) {
    const auto v = progressionHud(sheetAt(1, 0));
    EXPECT_EQ(v.level, 1);
    EXPECT_EQ(v.xpTotal, 0);
    EXPECT_EQ(v.xpIntoLevel, 0);
    EXPECT_EQ(v.xpLevelSpan, 300) << "level 1 -> 2 is 0..300 XP in the PHB table";
    EXPECT_EQ(v.xpToNext, 300);
    EXPECT_FLOAT_EQ(v.percent, 0.0f);
    EXPECT_NE(v.text.find("300"), std::string::npos) << "the readout must name the next threshold";
}

TEST(ProgressionHud, HalfWayToTheNextLevelIsFiftyPercent) {
    const auto v = progressionHud(sheetAt(1, 150));
    EXPECT_EQ(v.xpIntoLevel, 150);
    EXPECT_EQ(v.xpToNext, 150);
    EXPECT_FLOAT_EQ(v.percent, 50.0f);
}

TEST(ProgressionHud, LandingExactlyOnAThresholdStartsTheNextBarEmpty) {
    // 900 XP is exactly level 3. A character that has just levelled must see an EMPTY bar
    // toward level 4, not a full one toward level 3 - otherwise every level-up looks like it
    // failed to register.
    const auto v = progressionHud(sheetAt(3, 900));
    EXPECT_EQ(v.level, 3);
    EXPECT_EQ(v.xpIntoLevel, 0);
    EXPECT_EQ(v.xpLevelSpan, 1800) << "level 3 -> 4 is 900..2700";
    EXPECT_FLOAT_EQ(v.percent, 0.0f);
}

TEST(ProgressionHud, TheBarFillsWhenXpIsBankedButTheLevelHasNotBeenTaken) {
    // A game that does not auto-level leaves the sheet at level 1 with level-3 XP. The bar must
    // read FULL ("you can level up"), not 5% of the way through level 3.
    const auto v = progressionHud(sheetAt(1, 1000));
    EXPECT_EQ(v.level, 1);
    EXPECT_FLOAT_EQ(v.percent, 100.0f);
    EXPECT_EQ(v.xpToNext, 0) << "nothing more is needed - the level is already earned";
}

TEST(ProgressionHud, LevelTwentyIsMaxedRatherThanDividingByZero) {
    const auto v = progressionHud(sheetAt(20, CharacterProgression::xpForLevel(20) + 5000));
    EXPECT_EQ(v.level, 20);
    EXPECT_EQ(v.xpLevelSpan, 0);
    EXPECT_EQ(v.xpToNext, 0);
    EXPECT_FLOAT_EQ(v.percent, 100.0f);
    EXPECT_NE(v.text.find("MAX"), std::string::npos);
}

TEST(ProgressionHud, AnUnauthoredSheetDoesNotCrashOrReportLevelZero) {
    // The shipped shell publishes these every frame, including before a profile loads.
    const auto v = progressionHud(sheetAt(0, 0));
    EXPECT_GE(v.level, 1) << "a level-0 readout would be shown to the player";
    EXPECT_GE(v.percent, 0.0f);
    EXPECT_LE(v.percent, 100.0f);
    EXPECT_FALSE(v.text.empty());
}

TEST(ProgressionHud, ThePercentIsAlwaysAValidBarValue) {
    // Sweep the whole table plus junk either side; the bar must never be fed a value it cannot
    // draw. Negative XP is not reachable through awardXP, but the HUD must not be the thing that
    // discovers that.
    for (int lvl = 0; lvl <= 21; ++lvl) {
        for (int xp : {-5000, -1, 0, 1, 299, 300, 901, 47999, 355000, 999999}) {
            const auto v = progressionHud(sheetAt(lvl, xp));
            EXPECT_GE(v.percent, 0.0f)   << "level " << lvl << " xp " << xp;
            EXPECT_LE(v.percent, 100.0f) << "level " << lvl << " xp " << xp;
            EXPECT_GE(v.xpIntoLevel, 0)  << "level " << lvl << " xp " << xp;
            EXPECT_GE(v.xpToNext, 0)     << "level " << lvl << " xp " << xp;
            EXPECT_FALSE(v.text.empty()) << "level " << lvl << " xp " << xp;
        }
    }
}

// The award path the HUD exists to make visible: this is the engine rule the shipped game calls
// on a kill and on an objective. If this ever stops working, the bar would sit still and look
// like a UI bug.
TEST(ProgressionHud, AwardingXpMovesTheBar) {
    CharacterSheet s = sheetAt(1, 0);
    DiceSystem dice;
    const auto before = progressionHud(s);
    ASSERT_FLOAT_EQ(before.percent, 0.0f);

    CharacterProgression::awardXP(s, 150, dice, /*autoLevel=*/false);
    const auto mid = progressionHud(s);
    EXPECT_FLOAT_EQ(mid.percent, 50.0f) << "50 XP a kill x3 must be visible on the bar";

    CharacterProgression::awardXP(s, 150, dice, /*autoLevel=*/false);
    const auto full = progressionHud(s);
    EXPECT_FLOAT_EQ(full.percent, 100.0f);
    EXPECT_EQ(full.xpTotal, 300);
}
