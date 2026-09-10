#include <gtest/gtest.h>

#include "core/TransitionMarkers.h"

using namespace Phyxel::Core;

// ============================================================================
// Transition sites must be VISIBLE (WalkabilityGateAndPlaytestLoop increment 7). The
// loader plans one prop per scene-transition region: a trapdoor/ladder ON the site, a
// waystone BESIDE a road exit (perpendicular to the region's long axis, one cube clear),
// an explicit offset when authored, nothing for "none" or for triggers that do not
// transition. RED on the old code: no planner existed - every transition was invisible.
// ============================================================================

static nlohmann::json triggersJson() {
    return nlohmann::json::parse(R"([
      {"id":"to_cellar","when":{"event":"entity_reached_region","entity":"player","interact":true,"marker":"trapdoor",
         "region":{"from":{"x":-27,"y":16,"z":9},"to":{"x":-26,"y":21,"z":10}}},
       "then":[{"type":"transition_scene","target":"cellar"}]},
      {"id":"to_farm","when":{"event":"entity_reached_region","entity":"player",
         "region":{"from":{"x":58,"y":16,"z":12},"to":{"x":62,"y":22,"z":18}}},
       "then":[{"type":"transition_scene","target":"farm"}]},
      {"id":"back_to_town","when":{"event":"entity_reached_region","entity":"player",
         "region":{"from":{"x":1,"y":16,"z":12},"to":{"x":3,"y":22,"z":20}}},
       "then":[{"type":"transition_scene","target":"town"}]},
      {"id":"secret","when":{"event":"entity_reached_region","entity":"player","marker":"none",
         "region":{"from":{"x":0,"y":16,"z":0},"to":{"x":1,"y":22,"z":1}}},
       "then":[{"type":"transition_scene","target":"x"}]},
      {"id":"offset","when":{"event":"entity_reached_region","entity":"player","marker":"waystone","marker_offset":{"x":-2,"z":0},
         "region":{"from":{"x":10,"y":16,"z":10},"to":{"x":12,"y":22,"z":12}}},
       "then":[{"type":"transition_scene","target":"y"}]},
      {"id":"rats_dead","when":{"event":"combat_victory"},"then":[{"type":"set_story_variable","name":"v","value":true}]},
      {"id":"plate","when":{"event":"entity_reached_region","entity":"player",
         "region":{"from":{"x":5,"y":16,"z":5},"to":{"x":6,"y":22,"z":6}}},
       "then":[{"type":"fire_trigger","id":"other"}]}
    ])");
}

TEST(TransitionMarkersTest, EveryTransitionRegionGetsAVisibleSite) {
    const auto plan = planTransitionMarkers(triggersJson());
    ASSERT_EQ(plan.size(), 4u) << "to_cellar, to_farm, back_to_town, offset; 'none', non-transition and non-region triggers excluded";
    EXPECT_EQ(plan[0].triggerId, "to_cellar"); EXPECT_EQ(plan[0].templateName, "trapdoor"); EXPECT_TRUE(plan[0].interact);
    EXPECT_EQ(plan[0].position, glm::ivec3(-27, 16, 9)) << "a trapdoor sits ON its cell";
    EXPECT_EQ(plan[1].templateName, "waystone") << "default marker";
    // to_farm: region x 58..62 (long in x, 4) vs z 12..18 (6): long axis is z -> beside it in x: x1 + 1 = 63
    EXPECT_EQ(plan[1].position, glm::ivec3(63, 16, 15));
    // back_to_town: x 1..3 (2) vs z 12..20 (8): long axis z -> x = 4
    EXPECT_EQ(plan[2].position, glm::ivec3(4, 16, 16));
    EXPECT_EQ(plan[3].position, glm::ivec3(9, 16, 11)) << "explicit offset from the centre (11,11)";
}

TEST(TransitionMarkersTest, RoadExitWaystoneStandsAtTheVergeNotOnTheRoad) {
    // A road running along x: the region is long in x; the waystone goes to +z, one cube clear.
    auto j = nlohmann::json::parse(R"([{"id":"e","when":{"event":"entity_reached_region",
        "region":{"from":{"x":20,"y":16,"z":14},"to":{"x":30,"y":22,"z":16}}},"then":[{"type":"transition_scene","target":"t"}]}])");
    const auto plan = planTransitionMarkers(j);
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0].position, glm::ivec3(25, 16, 17));
    EXPECT_TRUE(planTransitionMarkers(nlohmann::json::object()).empty());
}
