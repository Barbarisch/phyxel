// Core::EncounterInitiator — how fights start without an authored trigger region
// (Ravenmere G-137). Rules only; the shell owns entities and line of sight.
#include <gtest/gtest.h>
#include "core/EncounterInitiator.h"

using namespace Phyxel::Core;

namespace {
std::vector<AggroCandidate> pack() {
    return {
        {"npc_AlphaWolf", {10.0f, 17.0f, 10.0f}, true,  true},
        {"npc_Wolf",      {14.0f, 17.0f, 10.0f}, true,  true},   // 4 u away: joins
        {"npc_Wolf2",     {10.0f, 17.0f, 30.0f}, true,  true},   // 20 u away: not this fight
        {"npc_DeadWolf",  {11.0f, 17.0f, 11.0f}, true,  false},  // a corpse never joins
        {"npc_Hobb",      {12.0f, 17.0f, 10.0f}, false, true},   // the farmer is not hostile
    };
}
} // namespace

TEST(EncounterInitiatorTest, GroupIsTheLivingHostilesNearTheAnchorAnchorFirst) {
    const auto g = EncounterInitiator::gatherGroup("npc_AlphaWolf", {10.0f, 17.0f, 10.0f},
                                                   EncounterInitiator::kGroupRadius, pack());
    ASSERT_EQ(g.size(), 2u);
    EXPECT_EQ(g[0], "npc_AlphaWolf");
    EXPECT_EQ(g[1], "npc_Wolf");
}

TEST(EncounterInitiatorTest, EngagingANonHostileOrDeadAnchorYieldsNoAnchor) {
    auto g = EncounterInitiator::gatherGroup("npc_Hobb", {12.0f, 17.0f, 10.0f}, 12.0f, pack());
    for (const auto& id : g) EXPECT_NE(id, "npc_Hobb");
    g = EncounterInitiator::gatherGroup("npc_DeadWolf", {11.0f, 17.0f, 11.0f}, 12.0f, pack());
    for (const auto& id : g) EXPECT_NE(id, "npc_DeadWolf");
}

TEST(EncounterInitiatorTest, NoticesWithinRangeAndClearSight) {
    const glm::vec3 wolf{0, 17, 0};
    int losCalls = 0;
    auto open   = [&](const glm::vec3&, const glm::vec3&) { ++losCalls; return false; };
    auto walled = [&](const glm::vec3& a, const glm::vec3& b) {
        // the eye line must be used, not the feet
        EXPECT_NEAR(a.y, 17.0f + EncounterInitiator::kEyeHeight, 1e-4f);
        EXPECT_NEAR(b.y, 17.0f + EncounterInitiator::kEyeHeight, 1e-4f);
        return true;
    };
    EXPECT_TRUE (EncounterInitiator::notices(wolf, {5.0f, 17.0f, 0.0f}, 8.0f, open));
    EXPECT_FALSE(EncounterInitiator::notices(wolf, {9.0f, 17.0f, 0.0f}, 8.0f, open)) << "out of range";
    EXPECT_FALSE(EncounterInitiator::notices(wolf, {5.0f, 17.0f, 0.0f}, 8.0f, walled)) << "a wall between";
    EXPECT_TRUE (EncounterInitiator::notices(wolf, {5.0f, 17.0f, 0.0f}, 8.0f, nullptr)) << "no LOS test = open field";
    EXPECT_GE(losCalls, 1);
}

TEST(EncounterInitiatorTest, AnEscapeeKeepsItsDistanceUntilTheCooldownLapses) {
    EncounterInitiator ei;
    EXPECT_TRUE(ei.mayEngage("npc_AlphaWolf", 100.0));
    ei.noteFled("npc_AlphaWolf", 100.0);
    EXPECT_FALSE(ei.mayEngage("npc_AlphaWolf", 100.0 + EncounterInitiator::kFledCooldownSec - 1.0));
    EXPECT_TRUE (ei.mayEngage("npc_AlphaWolf", 100.0 + EncounterInitiator::kFledCooldownSec));
    EXPECT_TRUE (ei.mayEngage("npc_Wolf", 101.0)) << "only the escapee is on cooldown";
    ei.clearFled();
    EXPECT_TRUE(ei.mayEngage("npc_AlphaWolf", 101.0));
}
