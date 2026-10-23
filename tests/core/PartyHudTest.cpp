// PartyHudTest.cpp -- the party panel a player needs to see companions at all (G-161).
//
// RED-BEFORE-GREEN. The user passed the DC 12 Persuasion check with Reeve Aldric, was told they
// could have companions, and could not work out how to get any. The recruit machinery all
// exists and works: a `join_party` dialogue action adds to `Core::Party`, the shell respawns
// absent members near the player on a scene load, and `start_combat` auto-enlists them on the
// player's side. Bram and Wren are both authored, placed in town, and gated on story variables
// the check sets.
//
// What does not exist is any way to SEE a party. Nothing in the HUD says a companion joined,
// who is with you, or that one has fallen -- so a working recruit is indistinguishable from a
// broken one, which is exactly what the user hit.

#include <gtest/gtest.h>

#include "core/PartyHud.h"
#include "core/Party.h"

#include <string>
#include <unordered_map>

using namespace Phyxel::Core;
using Phyxel::UI::HudRecord;

namespace {

// A world containing some of the party. Absence is the interesting case, not an error: members
// persist across scenes, so whoever is not in THIS scene has no health to report.
struct FakeWorld {
    std::unordered_map<std::string, std::pair<float, float>> health;  // id -> {hp, maxHp}
    PartyHealthLookup lookup() const {
        return [this](const std::string& id, float& hp, float& maxHp) {
            auto it = health.find(id);
            if (it == health.end()) return false;
            hp = it->second.first;
            maxHp = it->second.second;
            return true;
        };
    }
};

float f(const HudRecord& r, const char* k) {
    auto it = r.floats.find(k);
    return it == r.floats.end() ? -1.0f : it->second;
}
std::string t(const HudRecord& r, const char* k) {
    auto it = r.texts.find(k);
    return it == r.texts.end() ? std::string("<missing>") : it->second;
}

}  // namespace

TEST(PartyHud, AnEmptyPartyShowsNothing) {
    Party p;
    FakeWorld w;
    EXPECT_TRUE(partyHudRecords(p, w.lookup()).empty())
        << "a solo player must not get an empty panel frame with no rows in it";
}

TEST(PartyHud, ARecruitedCompanionAppearsWithNameLevelAndHealth) {
    Party p;
    p.addMember("npc_Bram", "Bram", 3);
    FakeWorld w;
    w.health["npc_Bram"] = {12.0f, 18.0f};

    const auto recs = partyHudRecords(p, w.lookup());
    ASSERT_EQ(recs.size(), 1u) << "the whole point of the panel is that a recruit shows up";
    EXPECT_EQ(t(recs[0], "name"), "Bram");
    EXPECT_EQ(t(recs[0], "levelText"), "Lv 3");
    EXPECT_EQ(t(recs[0], "hpText"), "12 / 18");
    EXPECT_FLOAT_EQ(f(recs[0], "alive"), 1.0f);
    EXPECT_FLOAT_EQ(f(recs[0], "present"), 1.0f);
    EXPECT_NEAR(f(recs[0], "hpPercent"), 100.0f * 12.0f / 18.0f, 0.01f);
}

TEST(PartyHud, AFallenCompanionIsStillListedAndSaysSo) {
    // Ravenmere keeps the dead in the party until a long rest. A member who vanished from the
    // panel on death would read as "the party system dropped them", not as "they died".
    Party p;
    p.addMember("npc_Bram", "Bram", 3);
    p.setAlive("npc_Bram", false);
    FakeWorld w;
    w.health["npc_Bram"] = {0.0f, 18.0f};

    const auto recs = partyHudRecords(p, w.lookup());
    ASSERT_EQ(recs.size(), 1u) << "a fallen companion must not disappear from the party panel";
    EXPECT_FLOAT_EQ(f(recs[0], "alive"), 0.0f);
    EXPECT_EQ(t(recs[0], "statusText"), "Fallen");
    EXPECT_FLOAT_EQ(f(recs[0], "hpPercent"), 0.0f);
}

TEST(PartyHud, ACompanionInAnotherSceneIsListedAsAwayRatherThanDropped) {
    // The party persists across scene transitions; the entity does not exist until the shell
    // respawns it. Reporting that as 0 HP would look like they died on the way.
    Party p;
    p.addMember("npc_Wren", "Wren", 2);
    FakeWorld w;  // deliberately empty: Wren is not in this scene

    const auto recs = partyHudRecords(p, w.lookup());
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(t(recs[0], "name"), "Wren");
    EXPECT_FLOAT_EQ(f(recs[0], "present"), 0.0f);
    EXPECT_FLOAT_EQ(f(recs[0], "alive"), 1.0f) << "absent is not dead";
    EXPECT_EQ(t(recs[0], "statusText"), "Away");
    EXPECT_EQ(t(recs[0], "hpText"), "--") << "no health is honest; a fake 0/0 is not";
}

TEST(PartyHud, TheOrderFollowsTheParty) {
    Party p;
    p.addMember("npc_Bram", "Bram", 3);
    p.addMember("npc_Wren", "Wren", 2);
    FakeWorld w;
    w.health["npc_Bram"] = {18.0f, 18.0f};
    w.health["npc_Wren"] = {9.0f, 14.0f};

    const auto recs = partyHudRecords(p, w.lookup());
    ASSERT_EQ(recs.size(), 2u);
    EXPECT_EQ(t(recs[0], "name"), "Bram") << "rows must not reorder between frames";
    EXPECT_EQ(t(recs[1], "name"), "Wren");
}

TEST(PartyHud, ZeroMaxHealthDoesNotDivideByZero) {
    Party p;
    p.addMember("npc_Ghost", "Ghost", 1);
    FakeWorld w;
    w.health["npc_Ghost"] = {0.0f, 0.0f};

    const auto recs = partyHudRecords(p, w.lookup());
    ASSERT_EQ(recs.size(), 1u);
    const float pct = f(recs[0], "hpPercent");
    EXPECT_GE(pct, 0.0f);
    EXPECT_LE(pct, 100.0f);
}

TEST(PartyHud, EveryRowCarriesEveryFieldTheHudBindsTo) {
    // A missing key does not crash the HUD - applyRecord simply leaves the widget alone, which
    // shows the PREVIOUS row's value in a repeater. That is worse than blank: party member two
    // would wear member one's health. So every row must carry every field.
    Party p;
    p.addMember("npc_Bram", "Bram", 3);
    p.addMember("npc_Wren", "Wren", 2);
    FakeWorld w;
    w.health["npc_Bram"] = {18.0f, 18.0f};   // present
                                             // Wren absent on purpose

    for (const auto& r : partyHudRecords(p, w.lookup())) {
        for (const char* k : {"name", "levelText", "hpText", "statusText", "rowText"})
            EXPECT_NE(t(r, k), "<missing>") << "text field '" << k << "' missing from a row";
        for (const char* k : {"alive", "present", "hpPercent"})
            EXPECT_GE(f(r, k), 0.0f) << "float field '" << k << "' missing from a row";
    }
}

TEST(PartyHud, TheCompactRowNamesTheMemberAndTheirState) {
    Party p;
    p.addMember("npc_Bram", "Bram", 3);
    p.addMember("npc_Wren", "Wren", 2);
    p.setAlive("npc_Wren", false);
    FakeWorld w;
    w.health["npc_Bram"] = {12.0f, 18.0f};
    w.health["npc_Wren"] = {0.0f, 14.0f};

    const auto recs = partyHudRecords(p, w.lookup());
    ASSERT_EQ(recs.size(), 2u);
    EXPECT_NE(t(recs[0], "rowText").find("Bram"), std::string::npos);
    EXPECT_NE(t(recs[0], "rowText").find("12 / 18"), std::string::npos)
        << "a healthy member's row shows their health";
    EXPECT_NE(t(recs[1], "rowText").find("Fallen"), std::string::npos)
        << "a fallen member's row says so instead of showing 0 / 14";
}
