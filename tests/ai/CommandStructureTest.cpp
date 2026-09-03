#include <gtest/gtest.h>

#include "ai/CommandStructure.h"

using namespace Phyxel;
using Order = AI::CommandStructure::Order;
using Situation = AI::CommandStructure::SquadSituation;

// ============================================================================
// CommandStructure — squads, order propagation, and the degradation that makes
// killing an officer matter. The engine owns the structure; WHICH order is
// issued is a host doctrine callback, so these tests pin the mechanism, not
// any particular tactics.
// ============================================================================

namespace {

/// A squad of `n` men named s0..s(n-1), s0 leading.
AI::CommandStructure makeSquad(int n, const std::string& faction = "red") {
    AI::CommandStructure cs;
    cs.createSquad("alpha", faction);
    for (int i = 0; i < n; ++i)
        cs.addMember("alpha", "s" + std::to_string(i), i == 0);
    return cs;
}

/// Drive the decision cadence past one interval.
void tick(AI::CommandStructure& cs, const Situation& sit, float dt = 5.0f,
          std::vector<std::pair<std::string, Order>>* changes = nullptr) {
    cs.update(dt,
              [&](const AI::CommandStructure::Squad&) { return sit; },
              [&](const AI::CommandStructure::Squad& s, Order o) {
                  if (changes) changes->emplace_back(s.id, o);
              });
}

Situation healthy() {
    Situation s;
    s.alive = 10; s.strength = 10; s.healthFraction = 1.0f;
    s.nearestEnemyDist = 40.0f;
    return s;
}

} // namespace

TEST(CommandStructureTest, MembersInheritTheSquadOrder) {
    auto cs = makeSquad(5);
    cs.setDoctrine([](const Situation&) { return Order::Flank; });
    tick(cs, healthy());

    // Every member is under the order, not just the officer who received it.
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(cs.orderFor("s" + std::to_string(i)), Order::Flank) << "member s" << i;
}

TEST(CommandStructureTest, UnsquaddedEntityAdvancesByDefault) {
    auto cs = makeSquad(3);
    cs.setDoctrine([](const Situation&) { return Order::FallBack; });
    tick(cs, healthy());

    // A combatant nobody commands is not silently swept into someone's orders.
    EXPECT_EQ(cs.orderFor("freelancer"), Order::Advance);
    EXPECT_EQ(cs.squadOf("freelancer"), nullptr);
}

TEST(CommandStructureTest, OfficerDeathFreezesTheLastOrder) {
    auto cs = makeSquad(4);

    // First decision: hold.
    cs.setDoctrine([](const Situation&) { return Order::Hold; });
    tick(cs, healthy());
    ASSERT_EQ(cs.orderFor("s2"), Order::Hold);

    // The officer falls; doctrine would now say fall back.
    cs.notifyDeath("s0");
    cs.setDoctrine([](const Situation&) { return Order::FallBack; });
    tick(cs, healthy());

    // A headless squad fights on under its last order — it does NOT get the new
    // one, and it does NOT reset to Advance.
    EXPECT_EQ(cs.orderFor("s2"), Order::Hold);
    ASSERT_NE(cs.squad("alpha"), nullptr);
    EXPECT_TRUE(cs.squad("alpha")->leaderless);
    EXPECT_TRUE(cs.squad("alpha")->leaderId.empty());
}

TEST(CommandStructureTest, DeathOfARankerDoesNotDecapitate) {
    auto cs = makeSquad(4);
    cs.setDoctrine([](const Situation&) { return Order::Hold; });
    tick(cs, healthy());

    cs.notifyDeath("s3");        // an ordinary soldier

    cs.setDoctrine([](const Situation&) { return Order::Flank; });
    tick(cs, healthy());

    EXPECT_FALSE(cs.squad("alpha")->leaderless);
    EXPECT_EQ(cs.orderFor("s1"), Order::Flank);   // orders still flow
}

TEST(CommandStructureTest, OfficersDoNotReplanEveryFrame) {
    auto cs = makeSquad(3);
    cs.setDecisionInterval(3.0f);
    int calls = 0;
    cs.setDoctrine([&](const Situation&) { ++calls; return Order::Advance; });

    // Twenty 0.1 s frames = 2.0 s: inside one decision interval.
    for (int i = 0; i < 20; ++i) tick(cs, healthy(), 0.1f);
    EXPECT_EQ(calls, 0) << "doctrine ran before the decision interval elapsed";

    for (int i = 0; i < 20; ++i) tick(cs, healthy(), 0.1f);   // now past 3 s
    EXPECT_EQ(calls, 1);
}

TEST(CommandStructureTest, OrderChangeCallbackFiresOnlyOnChange) {
    auto cs = makeSquad(3);
    std::vector<std::pair<std::string, Order>> changes;

    cs.setDoctrine([](const Situation&) { return Order::Hold; });
    tick(cs, healthy(), 5.0f, &changes);
    tick(cs, healthy(), 5.0f, &changes);   // same order again
    EXPECT_EQ(changes.size(), 1u) << "re-issuing the same order is not a change";

    cs.setDoctrine([](const Situation&) { return Order::FallBack; });
    tick(cs, healthy(), 5.0f, &changes);
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[1].second, Order::FallBack);
}

TEST(CommandStructureTest, ObjectiveTracksTheEnemyBetweenOrderChanges) {
    auto cs = makeSquad(3);
    cs.setDoctrine([](const Situation&) { return Order::Flank; });

    Situation sit = healthy();
    sit.enemyCentre = glm::vec3(10.0f, 0.0f, 5.0f);
    tick(cs, sit);
    EXPECT_EQ(cs.squad("alpha")->objective, glm::vec3(10.0f, 0.0f, 5.0f));

    // The order does not change, but the enemy has moved. A squad still under
    // "flank" must flank where the enemy IS, not where it was when ordered.
    sit.enemyCentre = glm::vec3(30.0f, 0.0f, -5.0f);
    tick(cs, sit);
    EXPECT_EQ(cs.squad("alpha")->order, Order::Flank);
    EXPECT_EQ(cs.squad("alpha")->objective, glm::vec3(30.0f, 0.0f, -5.0f));
}

TEST(CommandStructureTest, WipedSquadIssuesNoOrders) {
    auto cs = makeSquad(3);
    cs.setDoctrine([](const Situation&) { return Order::FallBack; });

    Situation dead = healthy();
    dead.alive = 0;
    std::vector<std::pair<std::string, Order>> changes;
    tick(cs, dead, 5.0f, &changes);

    EXPECT_TRUE(changes.empty());
    EXPECT_EQ(cs.squad("alpha")->order, Order::Advance) << "unchanged from default";
}

TEST(CommandStructureTest, SquadsAreIndependent) {
    AI::CommandStructure cs;
    cs.createSquad("alpha", "red");
    cs.addMember("alpha", "a0", true);
    cs.createSquad("bravo", "red");
    cs.addMember("bravo", "b0", true);

    // Doctrine keyed off the squad each officer commands: alpha is shattered,
    // bravo is fresh. One order must not leak into the other squad.
    cs.update(5.0f,
              [](const AI::CommandStructure::Squad& s) {
                  Situation sit = healthy();
                  if (s.id == "alpha") { sit.alive = 1; sit.strength = 10; }
                  return sit;
              },
              {});
    // The doctrine itself decides; install one that reads the situation.
    cs.setDoctrine([](const Situation& s) {
        const float frac = s.strength > 0 ? float(s.alive) / s.strength : 1.0f;
        return frac <= 0.34f ? Order::FallBack : Order::Advance;
    });
    cs.update(5.0f,
              [](const AI::CommandStructure::Squad& s) {
                  Situation sit = healthy();
                  if (s.id == "alpha") { sit.alive = 1; sit.strength = 10; }
                  return sit;
              },
              {});

    EXPECT_EQ(cs.orderFor("a0"), Order::FallBack);
    EXPECT_EQ(cs.orderFor("b0"), Order::Advance);
}
