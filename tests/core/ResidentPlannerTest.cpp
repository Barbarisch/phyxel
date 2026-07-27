#include <gtest/gtest.h>

#include "core/ResidentPlanner.h"

using namespace Phyxel::Core;
namespace AI = Phyxel::AI;

// ============================================================================
// RESIDENT PLANNER (playable-town increment 4) — one NPC resident per building
// location, schedule-driven day loop. RED (live, pre-change): a settlement
// build spawns ZERO NPCs (no spawn code existed on the settlement path at all).
// These tests pin the plan-derivation contract the spawn unit consumes.
// ============================================================================

namespace {
Location mk(const std::string& id, const std::string& name, LocationType t,
            glm::vec3 pos = {0, 17, 0}) {
    Location l;
    l.id = id; l.name = name; l.type = t; l.position = pos; l.radius = 3.0f;
    return l;
}

const AI::ScheduleEntry* at(const ResidentPlan& p, float hour) {
    return p.schedule.getCurrentActivity(hour);
}

const ResidentPlan* byId(const std::vector<ResidentPlan>& v, const std::string& home) {
    for (const auto& p : v) if (p.homeLocationId == home) return &p;
    return nullptr;
}
} // namespace

// One resident per Home/Work/Tavern location; other types get none; names unique.
TEST(ResidentPlannerTest, OneResidentPerBuildingLocation) {
    std::vector<Location> locs = {
        mk("croft_1_1", "croft", LocationType::Home),
        mk("tavern_2_2", "tavern", LocationType::Tavern),
        mk("blacksmith_3_3", "blacksmith", LocationType::Work),
        mk("shrine_4_4", "shrine", LocationType::Custom),      // no resident
        mk("wild_5_5", "wild", LocationType::Wilderness),      // no resident
    };
    auto plans = ResidentPlanner::planResidents(locs);
    ASSERT_EQ(plans.size(), 3u);
    EXPECT_NE(byId(plans, "croft_1_1"), nullptr);
    EXPECT_NE(byId(plans, "tavern_2_2"), nullptr);
    EXPECT_NE(byId(plans, "blacksmith_3_3"), nullptr);
    // unique, deterministic names
    EXPECT_NE(plans[0].name, plans[1].name);
    EXPECT_EQ(byId(plans, "croft_1_1")->name, "res_croft_1_1");
}

// Everyone sleeps AT HOME at night (the wrap-past-midnight block).
TEST(ResidentPlannerTest, EveryoneSleepsAtHomeAtNight) {
    auto plans = ResidentPlanner::planResidents({
        mk("croft_1_1", "croft", LocationType::Home),
        mk("tavern_2_2", "tavern", LocationType::Tavern),
        mk("bakery_3_3", "bakery", LocationType::Work),
    });
    for (const auto& p : plans) {
        const auto* e2am = at(p, 2.0f);
        ASSERT_NE(e2am, nullptr) << p.name << " has no 2am block";
        EXPECT_EQ(e2am->activity, AI::ActivityType::Sleep) << p.name;
        EXPECT_EQ(e2am->locationId, p.homeLocationId) << p.name << " sleeps away from home";
        const auto* e5am = at(p, 5.5f);   // wrap side of the 20-6 block
        ASSERT_NE(e5am, nullptr);
        EXPECT_EQ(e5am->activity, AI::ActivityType::Sleep);
    }
}

// Evening: villagers + tradesfolk head to the tavern; the innkeeper works their own house.
TEST(ResidentPlannerTest, EveningTavernCrowdExceptInnkeeper) {
    auto plans = ResidentPlanner::planResidents({
        mk("croft_1_1", "croft", LocationType::Home),
        mk("tavern_2_2", "tavern", LocationType::Tavern),
        mk("blacksmith_3_3", "blacksmith", LocationType::Work),
    });
    const auto* villager = byId(plans, "croft_1_1");
    const auto* smith = byId(plans, "blacksmith_3_3");
    const auto* keeper = byId(plans, "tavern_2_2");
    ASSERT_TRUE(villager && smith && keeper);

    EXPECT_EQ(at(*villager, 18.0f)->activity, AI::ActivityType::Socialize);
    EXPECT_EQ(at(*villager, 18.0f)->locationId, "tavern_2_2");
    EXPECT_EQ(at(*smith, 18.0f)->locationId, "tavern_2_2");
    EXPECT_EQ(at(*keeper, 18.0f)->activity, AI::ActivityType::Work);
    EXPECT_EQ(at(*keeper, 18.0f)->locationId, "tavern_2_2");   // their own house

    // Day: the smith is at the smithy working, not at the tavern.
    EXPECT_EQ(at(*smith, 10.0f)->activity, AI::ActivityType::Work);
    EXPECT_EQ(at(*smith, 10.0f)->locationId, "blacksmith_3_3");
}

// No tavern in the settlement -> evenings are spent at home, never a dangling id.
TEST(ResidentPlannerTest, NoTavernFallsBackToHome) {
    auto plans = ResidentPlanner::planResidents({
        mk("croft_1_1", "croft", LocationType::Home),
        mk("bakery_3_3", "bakery", LocationType::Work),
    });
    for (const auto& p : plans) {
        const auto* evening = at(p, 18.0f);
        ASSERT_NE(evening, nullptr);
        const std::string got = evening->locationId, want = p.homeLocationId;
        EXPECT_EQ(got, want) << p.name;
    }
}

// Roles: typology-flavored occupations seed distinct appearances.
TEST(ResidentPlannerTest, RolesFollowTypology) {
    auto plans = ResidentPlanner::planResidents({
        mk("croft_1_1", "croft", LocationType::Home),
        mk("tavern_2_2", "tavern", LocationType::Tavern),
        mk("blacksmith_3_3", "blacksmith", LocationType::Work),
        mk("bakery_4_4", "bakery", LocationType::Work),
        mk("general_store_5_5", "general_store", LocationType::Work),
    });
    EXPECT_EQ(byId(plans, "croft_1_1")->role, "villager");
    EXPECT_EQ(byId(plans, "tavern_2_2")->role, "innkeeper");
    EXPECT_EQ(byId(plans, "blacksmith_3_3")->role, "blacksmith");
    EXPECT_EQ(byId(plans, "bakery_4_4")->role, "baker");
    EXPECT_EQ(byId(plans, "general_store_5_5")->role, "shopkeeper");
}

// Transitions are STAGGERED per resident (deterministic +/-0.3h jitter): everyone
// switching at the same instant funnels the village through one pinch point at once
// (measured: an 11-body jam leaving the tavern at 20:00 sharp).
TEST(ResidentPlannerTest, TransitionsAreStaggered) {
    std::vector<Location> locs;
    for (int i = 0; i < 6; ++i)
        locs.push_back(mk("croft_" + std::to_string(i) + "_0", "croft", LocationType::Home,
                          {i * 10.0f, 17, 0}));
    locs.push_back(mk("tavern_9_9", "tavern", LocationType::Tavern));
    auto plans = ResidentPlanner::planResidents(locs);

    std::set<int> distinctEveningMillis;
    for (const auto& p : plans) {
        if (p.role != "villager") continue;
        // The Socialize entry's start is the evening boundary.
        for (const auto& e : p.schedule.toJson()) {
            if (e.value("activity", std::string()) != "Socialize") continue;
            const float b = e.value("startHour", 0.0f);
            EXPECT_GE(b, 16.7f); EXPECT_LE(b, 17.3f);
            distinctEveningMillis.insert(static_cast<int>(b * 1000.0f));
        }
    }
    EXPECT_GE(distinctEveningMillis.size(), 4u)
        << "evening transitions not staggered across residents";
}

// Input order must not matter (the spawn unit reads an unordered registry map).
TEST(ResidentPlannerTest, DeterministicAcrossInputOrder) {
    std::vector<Location> a = {
        mk("croft_1_1", "croft", LocationType::Home),
        mk("tavern_2_2", "tavern", LocationType::Tavern),
        mk("blacksmith_3_3", "blacksmith", LocationType::Work),
    };
    std::vector<Location> b = {a[2], a[0], a[1]};
    auto pa = ResidentPlanner::planResidents(a);
    auto pb = ResidentPlanner::planResidents(b);
    ASSERT_EQ(pa.size(), pb.size());
    for (size_t i = 0; i < pa.size(); ++i) {
        EXPECT_EQ(pa[i].name, pb[i].name);
        EXPECT_EQ(pa[i].role, pb[i].role);
    }
}
