#include <gtest/gtest.h>

#include "core/ActionBar.h"
#include "core/PlayerProfile.h"

#include <set>

using Phyxel::Core::ActionBar;
using Phyxel::Core::ActionSlot;
using Phyxel::Core::PlayerProfile;

// ============================================================================
// ACTION BAR MODEL (Ravenmere G-150).
//
// The bar used to be DERIVED every frame from the player's known spells, so it had
// exactly as many rows as they had spells, in the order the list happened to be in,
// with nothing to persist. These tests pin the properties that made rearranging it
// possible: a fixed slot count including empties, refusal rather than clamping at the
// edges, swap-not-overwrite, and a round trip through the save file.
// ============================================================================

TEST(ActionBarTest, TheBarAlwaysHasTwelveSlotsIncludingEmptyOnes) {
    ActionBar bar;
    EXPECT_EQ(ActionBar::SLOT_COUNT, 12) << "keys 1-9, 0, -, =";
    EXPECT_TRUE(bar.isDefault());
    EXPECT_EQ(bar.filledCount(), 0);
    for (int i = 0; i < ActionBar::SLOT_COUNT; ++i)
        EXPECT_TRUE(bar.at(i).empty()) << "slot " << i << " should start empty";

    ASSERT_TRUE(bar.assign(4, ActionSlot::Kind::Spell, "fireball"));
    EXPECT_FALSE(bar.isDefault()) << "one assignment makes the layout the player's";
    EXPECT_EQ(bar.filledCount(), 1) << "assigning must not disturb the other eleven";
    for (int i = 0; i < ActionBar::SLOT_COUNT; ++i)
        if (i != 4) EXPECT_TRUE(bar.at(i).empty()) << "slot " << i << " moved";
}

TEST(ActionBarTest, OutOfRangeSlotsAreRefusedNotClamped) {
    ActionBar bar;
    bar.assign(0, ActionSlot::Kind::Spell, "fire_bolt");
    bar.assign(11, ActionSlot::Kind::Action, "end_turn");

    // A caller that means slot 12 has a bug; clamping would hide it by writing slot 11
    // and letting the caller's own assertion pass against the wrong slot.
    EXPECT_FALSE(bar.assign(12, ActionSlot::Kind::Spell, "fireball"));
    EXPECT_FALSE(bar.assign(-1, ActionSlot::Kind::Spell, "fireball"));
    EXPECT_FALSE(bar.clear(12));
    EXPECT_FALSE(bar.swap(0, 99));
    EXPECT_EQ(bar.at(11).id, "end_turn") << "the edge slot was written by a refused call";
    EXPECT_EQ(bar.at(0).id, "fire_bolt");
    EXPECT_EQ(bar.filledCount(), 2);

    // An empty id is a refusal too - clear() is how you empty a slot, so a typo'd id
    // cannot silently blank one.
    EXPECT_FALSE(bar.assign(0, ActionSlot::Kind::Spell, ""));
    EXPECT_EQ(bar.at(0).id, "fire_bolt");
}

TEST(ActionBarTest, SwapExchangesAndNeverDestroys) {
    ActionBar bar;
    bar.assign(0, ActionSlot::Kind::Spell, "fire_bolt");
    bar.assign(3, ActionSlot::Kind::Item, "potion_healing");

    ASSERT_TRUE(bar.swap(0, 3));
    EXPECT_EQ(bar.at(0).id, "potion_healing");
    EXPECT_EQ(bar.at(0).kind, ActionSlot::Kind::Item) << "kind travels with the id";
    EXPECT_EQ(bar.at(3).id, "fire_bolt");
    EXPECT_EQ(bar.at(3).kind, ActionSlot::Kind::Spell);
    EXPECT_EQ(bar.filledCount(), 2) << "a swap conserves assignments";

    // Swapping onto an EMPTY slot moves rather than duplicates.
    ASSERT_TRUE(bar.swap(3, 7));
    EXPECT_TRUE(bar.at(3).empty());
    EXPECT_EQ(bar.at(7).id, "fire_bolt");
    EXPECT_EQ(bar.filledCount(), 2);
}

TEST(ActionBarTest, TheSameSpellMayOccupySeveralSlots) {
    ActionBar bar;
    EXPECT_TRUE(bar.assign(0, ActionSlot::Kind::Spell, "fire_bolt"));
    EXPECT_TRUE(bar.assign(5, ActionSlot::Kind::Spell, "fire_bolt"));
    EXPECT_TRUE(bar.assign(11, ActionSlot::Kind::Spell, "fire_bolt"));
    EXPECT_EQ(bar.filledCount(), 3) << "duplication is a layout choice, not a broken invariant";
}

TEST(ActionBarTest, AssignmentsSurviveAJsonRoundTrip) {
    ActionBar bar;
    bar.assign(0, ActionSlot::Kind::Action, "attack");
    bar.assign(2, ActionSlot::Kind::Spell, "guiding_bolt");
    bar.assign(9, ActionSlot::Kind::Item, "potion_healing");
    bar.assign(11, ActionSlot::Kind::Action, "end_turn");

    ActionBar back;
    back.fromJson(bar.toJson());
    for (int i = 0; i < ActionBar::SLOT_COUNT; ++i) {
        EXPECT_EQ(back.at(i).id, bar.at(i).id) << "slot " << i;
        EXPECT_EQ(back.at(i).kind, bar.at(i).kind) << "slot " << i << " kind";
    }
    EXPECT_EQ(back.filledCount(), 4);
}

TEST(ActionBarTest, AMalformedOrMissingLayoutLoadsEmptyRatherThanRefusing) {
    ActionBar bar;
    bar.assign(0, ActionSlot::Kind::Spell, "fire_bolt");

    bar.fromJson(nlohmann::json::object());          // not an array at all
    EXPECT_TRUE(bar.isDefault()) << "a save from before this existed must load clean";

    bar.fromJson(nlohmann::json::parse(R"([{"kind":"spell","id":"a"},{},{"id":""},7])"));
    EXPECT_EQ(bar.at(0).id, "a");
    EXPECT_TRUE(bar.at(1).empty());
    EXPECT_TRUE(bar.at(2).empty()) << "a row with no id is an empty slot, not a broken load";
    EXPECT_TRUE(bar.at(3).empty()) << "a row that is not even an object";
    EXPECT_EQ(bar.filledCount(), 1);

    // Longer than the bar: the overflow is dropped, not wrapped onto slot 0.
    nlohmann::json tooMany = nlohmann::json::array();
    for (int i = 0; i < 40; ++i) tooMany.push_back({{"kind", "spell"}, {"id", "s"}});
    bar.fromJson(tooMany);
    EXPECT_EQ(bar.filledCount(), ActionBar::SLOT_COUNT);
}

// ============================================================================
// PERSISTENCE — the bar must survive a relaunch, or arranging it is pointless.
// ============================================================================

TEST(ActionBarTest, TheLayoutSurvivesAPlayerProfileRoundTrip) {
    PlayerProfile profile;
    profile.actionBar.assign(0, ActionSlot::Kind::Action, "attack");
    profile.actionBar.assign(3, ActionSlot::Kind::Spell, "fireball");
    profile.actionBar.assign(11, ActionSlot::Kind::Action, "end_turn");

    PlayerProfile back;
    back.fromJson(profile.toJson());

    EXPECT_EQ(back.actionBar.at(0).id, "attack");
    EXPECT_EQ(back.actionBar.at(3).id, "fireball");
    EXPECT_EQ(back.actionBar.at(3).kind, ActionSlot::Kind::Spell);
    EXPECT_EQ(back.actionBar.at(11).id, "end_turn");
    EXPECT_EQ(back.actionBar.filledCount(), 3);
    // CONTROL: the rest of the profile still round-trips, so adding a field did not
    // disturb what was already saved.
    EXPECT_FLOAT_EQ(back.health, profile.health);
    EXPECT_EQ(back.level, profile.level);
}

TEST(ActionBarTest, AProfileSavedBeforeTheBarExistedLoadsWithAnEmptyBar) {
    // Exactly the shape an existing Ravenmere save has: no "actionBar" key at all.
    const auto old = nlohmann::json::parse(R"({
        "health": 42.0, "maxHealth": 80.0, "xp": 900, "level": 3,
        "camera": {"x": 1.0, "y": 2.0, "z": 3.0, "yaw": 10.0, "pitch": -5.0}
    })");
    PlayerProfile profile;
    profile.fromJson(old);

    EXPECT_TRUE(profile.actionBar.isDefault())
        << "an absent layout must read as 'never arranged', so the host fills the default";
    EXPECT_FLOAT_EQ(profile.health, 42.0f) << "the rest of the old save still loads";
    EXPECT_EQ(profile.level, 3);
}
