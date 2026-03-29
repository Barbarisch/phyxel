#include <gtest/gtest.h>
#include "ai/Schedule.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::AI;
using json = nlohmann::json;

// ============================================================================
// ScheduleEntry Tests
// ============================================================================

TEST(ScheduleEntryTest, ContainsHourNormal) {
    ScheduleEntry entry;
    entry.startHour = 8.0f;
    entry.endHour = 17.0f;
    entry.activity = ActivityType::Work;

    EXPECT_TRUE(entry.containsHour(8.0f));
    EXPECT_TRUE(entry.containsHour(12.0f));
    EXPECT_TRUE(entry.containsHour(16.99f));
    EXPECT_FALSE(entry.containsHour(17.0f)); // end is exclusive
    EXPECT_FALSE(entry.containsHour(7.0f));
    EXPECT_FALSE(entry.containsHour(20.0f));
}

TEST(ScheduleEntryTest, ContainsHourMidnightWrap) {
    ScheduleEntry entry;
    entry.startHour = 22.0f;
    entry.endHour = 6.0f; // wraps midnight
    entry.activity = ActivityType::Sleep;

    EXPECT_TRUE(entry.containsHour(22.0f));
    EXPECT_TRUE(entry.containsHour(23.5f));
    EXPECT_TRUE(entry.containsHour(0.0f));
    EXPECT_TRUE(entry.containsHour(3.0f));
    EXPECT_TRUE(entry.containsHour(5.99f));
    EXPECT_FALSE(entry.containsHour(6.0f));
    EXPECT_FALSE(entry.containsHour(12.0f));
    EXPECT_FALSE(entry.containsHour(21.0f));
}

TEST(ScheduleEntryTest, JsonRoundTrip) {
    ScheduleEntry entry;
    entry.startHour = 9.0f;
    entry.endHour = 17.0f;
    entry.activity = ActivityType::Work;
    entry.locationId = "workshop_1";
    entry.priority = 5;

    json j = entry.toJson();
    EXPECT_FLOAT_EQ(j["startHour"].get<float>(), 9.0f);
    EXPECT_FLOAT_EQ(j["endHour"].get<float>(), 17.0f);
    EXPECT_EQ(j["activity"], "Work");
    EXPECT_EQ(j["locationId"], "workshop_1");
    EXPECT_EQ(j["priority"], 5);

    ScheduleEntry entry2 = ScheduleEntry::fromJson(j);
    EXPECT_FLOAT_EQ(entry2.startHour, 9.0f);
    EXPECT_FLOAT_EQ(entry2.endHour, 17.0f);
    EXPECT_EQ(entry2.activity, ActivityType::Work);
    EXPECT_EQ(entry2.locationId, "workshop_1");
    EXPECT_EQ(entry2.priority, 5);
}

// ============================================================================
// Schedule Tests
// ============================================================================

TEST(ScheduleTest, AddAndGetCurrentActivity) {
    Schedule sched;

    ScheduleEntry work;
    work.startHour = 8.0f;
    work.endHour = 17.0f;
    work.activity = ActivityType::Work;
    work.locationId = "shop";
    sched.addEntry(work);

    ScheduleEntry sleep;
    sleep.startHour = 22.0f;
    sleep.endHour = 6.0f;
    sleep.activity = ActivityType::Sleep;
    sleep.locationId = "home";
    sched.addEntry(sleep);

    // During work hours
    auto* current = sched.getCurrentActivity(12.0f);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->activity, ActivityType::Work);
    EXPECT_EQ(current->locationId, "shop");

    // During sleep hours
    current = sched.getCurrentActivity(23.0f);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->activity, ActivityType::Sleep);

    // During sleep (after midnight)
    current = sched.getCurrentActivity(3.0f);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->activity, ActivityType::Sleep);

    // No scheduled activity
    current = sched.getCurrentActivity(19.0f);
    EXPECT_EQ(current, nullptr);
}

TEST(ScheduleTest, PriorityResolution) {
    Schedule sched;

    ScheduleEntry low;
    low.startHour = 10.0f;
    low.endHour = 14.0f;
    low.activity = ActivityType::Wander;
    low.priority = 1;
    sched.addEntry(low);

    ScheduleEntry high;
    high.startHour = 11.0f;
    high.endHour = 13.0f;
    high.activity = ActivityType::Guard;
    high.priority = 10;
    sched.addEntry(high);

    // At 10:30, only low is active
    auto* current = sched.getCurrentActivity(10.5f);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->activity, ActivityType::Wander);

    // At 12:00, both are active, high priority wins
    current = sched.getCurrentActivity(12.0f);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->activity, ActivityType::Guard);
}

TEST(ScheduleTest, EmptySchedule) {
    Schedule sched;
    EXPECT_EQ(sched.getCurrentActivity(12.0f), nullptr);
    EXPECT_TRUE(sched.getEntries().empty());
}

TEST(ScheduleTest, JsonRoundTrip) {
    Schedule sched;

    ScheduleEntry work;
    work.startHour = 8.0f;
    work.endHour = 17.0f;
    work.activity = ActivityType::Work;
    work.locationId = "shop";
    sched.addEntry(work);

    ScheduleEntry eat;
    eat.startHour = 12.0f;
    eat.endHour = 13.0f;
    eat.activity = ActivityType::Eat;
    eat.locationId = "tavern";
    eat.priority = 5;
    sched.addEntry(eat);

    json j = sched.toJson();
    Schedule sched2 = Schedule::fromJson(j);

    EXPECT_EQ(sched2.getEntries().size(), 2u);

    auto* current = sched2.getCurrentActivity(9.0f);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->activity, ActivityType::Work);

    // At noon, eat has higher priority
    current = sched2.getCurrentActivity(12.5f);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->activity, ActivityType::Eat);
}

TEST(ScheduleTest, GuardDefault) {
    Schedule sched = Schedule::guardSchedule();
    EXPECT_FALSE(sched.getEntries().empty());

    // Guard should have a sleep entry (night) and patrol/guard entries
    auto* night = sched.getCurrentActivity(1.0f);
    ASSERT_NE(night, nullptr);
    EXPECT_EQ(night->activity, ActivityType::Sleep);
}

TEST(ScheduleTest, MerchantDefault) {
    Schedule sched = Schedule::merchantSchedule();
    EXPECT_FALSE(sched.getEntries().empty());

    // Merchant works during the day
    auto* work = sched.getCurrentActivity(10.0f);
    ASSERT_NE(work, nullptr);
    // Should be Shop or Work
    EXPECT_TRUE(work->activity == ActivityType::Shop || work->activity == ActivityType::Work);
}

TEST(ScheduleTest, FarmerDefault) {
    Schedule sched = Schedule::farmerSchedule();
    EXPECT_FALSE(sched.getEntries().empty());
}

TEST(ScheduleTest, InnkeeperDefault) {
    Schedule sched = Schedule::innkeeperSchedule();
    EXPECT_FALSE(sched.getEntries().empty());
}

TEST(ScheduleTest, DefaultSchedule) {
    Schedule sched = Schedule::defaultSchedule();
    EXPECT_FALSE(sched.getEntries().empty());
}

TEST(ScheduleTest, ForRoleCaseInsensitive) {
    Schedule g1 = Schedule::forRole("guard");
    Schedule g2 = Schedule::forRole("Guard");
    Schedule g3 = Schedule::forRole("GUARD");

    // All should produce non-empty guard schedules
    EXPECT_FALSE(g1.getEntries().empty());
    EXPECT_FALSE(g2.getEntries().empty());
    EXPECT_FALSE(g3.getEntries().empty());
}

TEST(ScheduleTest, ForRoleUnknownGetsDefault) {
    Schedule sched = Schedule::forRole("unicorn_tamer");
    EXPECT_FALSE(sched.getEntries().empty());
    // Should be same as default
}

// ============================================================================
// ActivityType String Round-Trip
// ============================================================================

TEST(ScheduleEntryTest, AllActivityTypes) {
    std::vector<ActivityType> types = {
        ActivityType::Sleep, ActivityType::Eat, ActivityType::Work,
        ActivityType::Patrol, ActivityType::Socialize, ActivityType::Worship,
        ActivityType::Train, ActivityType::Shop, ActivityType::Wander,
        ActivityType::Guard, ActivityType::Custom
    };

    for (auto type : types) {
        ScheduleEntry entry;
        entry.startHour = 0.0f;
        entry.endHour = 24.0f;
        entry.activity = type;

        json j = entry.toJson();
        ScheduleEntry entry2 = ScheduleEntry::fromJson(j);
        EXPECT_EQ(entry2.activity, type);
    }
}
