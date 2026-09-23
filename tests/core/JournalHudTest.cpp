// JournalHudTest.cpp -- the journal window (G-168).
//
// User, 2026-09-23: "if there are side quests, it would be nice to have a window to see them
// 'L' key maybe". The engine has ObjectiveTracker with categories, priorities and descriptions;
// the shipped HUD shows only a strip of active titles - no descriptions, no side/main split, no
// history of what was done. These pin the sections a journal window is built from. Pure, no
// Vulkan.

#include <gtest/gtest.h>

#include "core/JournalHud.h"
#include "core/ObjectiveTracker.h"

#include <string>
#include <vector>

using namespace Phyxel::Core;
using Phyxel::UI::HudRecord;

namespace {

float f(const HudRecord& r, const char* k) {
    auto it = r.floats.find(k);
    return it == r.floats.end() ? -1.0f : it->second;
}
std::string t(const HudRecord& r, const char* k) {
    auto it = r.texts.find(k);
    return it == r.texts.end() ? std::string("<missing>") : it->second;
}

// Ravenmere's shape: a main chain with hidden later steps, and side quests.
ObjectiveTracker ravenmereLike() {
    ObjectiveTracker o;
    o.addObjective("main_1", "Speak with Reeve Aldric", "The Reeve holds court at the town hall.", "main", 10);
    o.addObjective("main_2", "Ask Priestess Maera", "Maera keeps the shrine.", "main", 9, /*hidden=*/true);
    o.addObjective("side_rats", "Clear the rats from the cellar", "Hobb will pay.", "side", 0);
    o.addObjective("side_wolves", "Drive the wolves from Hollin Farm", "Dunstan lost two goats.", "side", 5);
    return o;
}

bool mentions(const std::vector<HudRecord>& rows, const char* needle) {
    for (const auto& r : rows) if (t(r, "title").find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST(JournalHud, AnEmptyTrackerProducesNoSectionsAtAll) {
    ObjectiveTracker o;
    const auto s = journalHudSections(o);
    EXPECT_TRUE(s.empty()) << "every section empty, so the panel draws no headings";
}

TEST(JournalHud, MainAndSideQuestsLandInTheirOwnSections) {
    const auto s = journalHudSections(ravenmereLike());
    ASSERT_EQ(s.main.size(), 1u) << "main_2 is hidden, so only main_1 is a main entry";
    EXPECT_EQ(t(s.main[0], "title"), "[ ] Speak with Reeve Aldric");
    ASSERT_EQ(s.side.size(), 2u);
    EXPECT_TRUE(s.completed.empty()) << "nothing is completed yet";
}

TEST(JournalHud, TheDescriptionTheStripNeverShowsIsInTheJournal) {
    const auto s = journalHudSections(ravenmereLike());
    ASSERT_EQ(s.main.size(), 1u);
    EXPECT_EQ(t(s.main[0], "detail"), "The Reeve holds court at the town hall.")
        << "the whole point of a journal over the strip is the description";
}

TEST(JournalHud, HiddenStepsStayHiddenSoTheJournalCannotSpoilTheChain) {
    const auto s = journalHudSections(ravenmereLike());
    EXPECT_FALSE(mentions(s.main, "Maera")) << "main_2 is authored hidden until main_1 completes";
    EXPECT_FALSE(mentions(s.side, "Maera"));
    EXPECT_FALSE(mentions(s.completed, "Maera"));
}

TEST(JournalHud, SideQuestsOrderByPriorityThenTitle) {
    const auto s = journalHudSections(ravenmereLike());
    ASSERT_EQ(s.side.size(), 2u);
    EXPECT_EQ(t(s.side[0], "title"), "[ ] Drive the wolves from Hollin Farm") << "priority 5 first";
    EXPECT_EQ(t(s.side[1], "title"), "[ ] Clear the rats from the cellar");
}

TEST(JournalHud, CompletingAnObjectiveMovesItToCompletedWithATick) {
    ObjectiveTracker o = ravenmereLike();
    o.completeObjective("side_rats");
    const auto s = journalHudSections(o);
    ASSERT_EQ(s.completed.size(), 1u);
    EXPECT_EQ(t(s.completed[0], "title"), "[x] Clear the rats from the cellar");
    EXPECT_FLOAT_EQ(f(s.completed[0], "complete"), 1.0f);
    EXPECT_EQ(s.side.size(), 1u) << "only the wolves remain active on the side";
    EXPECT_FALSE(mentions(s.side, "rats"));
}

TEST(JournalHud, FailedObjectivesAreOmittedLikeTheStripOmitsThem) {
    ObjectiveTracker o = ravenmereLike();
    o.failObjective("side_wolves");
    const auto s = journalHudSections(o);
    EXPECT_FALSE(mentions(s.side, "wolves"));
    EXPECT_FALSE(mentions(s.completed, "wolves"));
}

TEST(JournalHud, ARevealedStepAppearsOnceItIsRevealed) {
    ObjectiveTracker o = ravenmereLike();
    o.completeObjective("main_1");
    o.setHidden("main_2", false);
    const auto s = journalHudSections(o);
    EXPECT_TRUE(mentions(s.main, "Maera"));
    EXPECT_EQ(s.completed.size(), 1u);
}

TEST(JournalHud, EveryRowCarriesEveryFieldTheHudBindsTo) {
    // A missing key leaves a repeater widget showing the PREVIOUS row's value (see PartyHud):
    // an entry would inherit its neighbour's description. So every row carries every field.
    ObjectiveTracker o = ravenmereLike();
    o.completeObjective("side_rats");
    const auto s = journalHudSections(o);
    for (const auto* section : {&s.main, &s.side, &s.completed})
        for (const auto& r : *section) {
            for (const char* k : {"title", "detail", "category"})
                EXPECT_NE(t(r, k), "<missing>") << "text field '" << k << "' missing";
            EXPECT_GE(f(r, "complete"), 0.0f) << "float field 'complete' missing";
        }
}
