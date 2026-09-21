#include <gtest/gtest.h>

#include "scene/motion/CombatTransitionMap.h"

using namespace Phyxel::Scene::Motion;

TEST(CombatTransitionMap, UsesAuthoredCombatPhases) {
    const CombatClipTiming timing{0.4f, 0.6f};
    EXPECT_EQ(combatPhaseAt(0.2f, timing), CombatPhase::Windup);
    EXPECT_EQ(combatPhaseAt(0.5f, timing), CombatPhase::Active);
    EXPECT_EQ(combatPhaseAt(0.8f, timing), CombatPhase::Recovery);
    EXPECT_EQ(combatPhaseAt(0.2f, timing, true), CombatPhase::Guard);
}

TEST(CombatTransitionMap, MapsAttackToBlockOnlyInRecovery) {
    CombatTransitionMap graph({
        {"sword_swing_1", CombatTransitionInput::Block, CombatPhase::Recovery, "sword_guard"},
        {"", CombatTransitionInput::LightAttack, CombatPhase::Recovery, "sword_swing_1"}
    });
    ASSERT_TRUE(graph.valid()) << graph.error();
    EXPECT_EQ(graph.resolve("sword_swing_1", CombatTransitionInput::Block,
                            CombatPhase::Active), nullptr);
    const auto* block = graph.resolve("sword_swing_1", CombatTransitionInput::Block,
                                      CombatPhase::Recovery);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->toClip, "sword_guard");
    EXPECT_EQ(graph.resolve("unknown", CombatTransitionInput::LightAttack,
                            CombatPhase::Recovery)->toClip, "sword_swing_1");
}

TEST(CombatTransitionMap, RejectsAmbiguousOrEmptyRules) {
    EXPECT_FALSE(CombatTransitionMap({{"a", CombatTransitionInput::Block,
                                      CombatPhase::Recovery, ""}}).valid());
    EXPECT_FALSE(CombatTransitionMap({
        {"a", CombatTransitionInput::Block, CombatPhase::Recovery, "guard1"},
        {"a", CombatTransitionInput::Block, CombatPhase::Recovery, "guard2"}
    }).valid());
}
