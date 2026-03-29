#include <gtest/gtest.h>
#include "ai/NeedsSystem.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::AI;
using json = nlohmann::json;

// ============================================================================
// NeedType string conversion
// ============================================================================

TEST(NeedTypeTest, ToStringAndBack) {
    EXPECT_EQ(needTypeToString(NeedType::Hunger), "Hunger");
    EXPECT_EQ(needTypeToString(NeedType::Rest), "Rest");
    EXPECT_EQ(needTypeToString(NeedType::Social), "Social");
    EXPECT_EQ(needTypeToString(NeedType::Safety), "Safety");
    EXPECT_EQ(needTypeToString(NeedType::Entertainment), "Entertainment");
    EXPECT_EQ(needTypeToString(NeedType::Comfort), "Comfort");

    EXPECT_EQ(needTypeFromString("Hunger"), NeedType::Hunger);
    EXPECT_EQ(needTypeFromString("Rest"), NeedType::Rest);
    EXPECT_EQ(needTypeFromString("Social"), NeedType::Social);
    EXPECT_EQ(needTypeFromString("Safety"), NeedType::Safety);
    EXPECT_EQ(needTypeFromString("Entertainment"), NeedType::Entertainment);
    EXPECT_EQ(needTypeFromString("Comfort"), NeedType::Comfort);
    EXPECT_EQ(needTypeFromString("bogus"), NeedType::Hunger); // default
}

// ============================================================================
// Need struct
// ============================================================================

TEST(NeedTest, DefaultValues) {
    Need n;
    EXPECT_FLOAT_EQ(n.value, 100.0f);
    EXPECT_FLOAT_EQ(n.decayRate, 1.0f);
    EXPECT_FLOAT_EQ(n.urgencyThreshold, 30.0f);
}

TEST(NeedTest, UrgencyCalculation) {
    Need n;
    n.value = 100.0f;
    EXPECT_FLOAT_EQ(n.getUrgency(), 0.0f);

    n.value = 0.0f;
    EXPECT_FLOAT_EQ(n.getUrgency(), 1.0f);

    n.value = 50.0f;
    EXPECT_FLOAT_EQ(n.getUrgency(), 0.5f);
}

TEST(NeedTest, IsUrgent) {
    Need n;
    n.urgencyThreshold = 30.0f;
    n.value = 29.0f;
    EXPECT_TRUE(n.isUrgent());
    n.value = 31.0f;
    EXPECT_FALSE(n.isUrgent());
    n.value = 30.0f;
    EXPECT_FALSE(n.isUrgent()); // not strictly below
}

TEST(NeedTest, JsonRoundTrip) {
    Need n;
    n.type = NeedType::Social;
    n.value = 42.5f;
    n.decayRate = 0.8f;
    n.urgencyThreshold = 25.0f;

    json j = n.toJson();
    Need n2 = Need::fromJson(j);
    EXPECT_EQ(n2.type, NeedType::Social);
    EXPECT_FLOAT_EQ(n2.value, 42.5f);
    EXPECT_FLOAT_EQ(n2.decayRate, 0.8f);
    EXPECT_FLOAT_EQ(n2.urgencyThreshold, 25.0f);
}

// ============================================================================
// NeedsSystem
// ============================================================================

TEST(NeedsSystemTest, InitDefaults) {
    NeedsSystem sys;
    sys.initDefaults();

    EXPECT_NE(sys.getNeed(NeedType::Hunger), nullptr);
    EXPECT_NE(sys.getNeed(NeedType::Rest), nullptr);
    EXPECT_NE(sys.getNeed(NeedType::Social), nullptr);
    EXPECT_NE(sys.getNeed(NeedType::Safety), nullptr);
    EXPECT_NE(sys.getNeed(NeedType::Entertainment), nullptr);
    EXPECT_NE(sys.getNeed(NeedType::Comfort), nullptr);
}

TEST(NeedsSystemTest, SetAndGetNeed) {
    NeedsSystem sys;
    Need n;
    n.type = NeedType::Hunger;
    n.value = 50.0f;
    n.decayRate = 3.0f;
    sys.setNeed(n);

    const Need* got = sys.getNeed(NeedType::Hunger);
    ASSERT_NE(got, nullptr);
    EXPECT_FLOAT_EQ(got->value, 50.0f);
    EXPECT_FLOAT_EQ(got->decayRate, 3.0f);
}

TEST(NeedsSystemTest, RemoveNeed) {
    NeedsSystem sys;
    sys.initDefaults();
    EXPECT_NE(sys.getNeed(NeedType::Comfort), nullptr);
    sys.removeNeed(NeedType::Comfort);
    EXPECT_EQ(sys.getNeed(NeedType::Comfort), nullptr);
}

TEST(NeedsSystemTest, Decay) {
    NeedsSystem sys;
    Need n;
    n.type = NeedType::Hunger;
    n.value = 80.0f;
    n.decayRate = 10.0f;
    sys.setNeed(n);

    sys.update(1.0f); // 1 game hour
    EXPECT_FLOAT_EQ(sys.getNeed(NeedType::Hunger)->value, 70.0f);

    sys.update(7.0f); // 7 more hours → would go to 0, clamp at 0
    EXPECT_FLOAT_EQ(sys.getNeed(NeedType::Hunger)->value, 0.0f);
}

TEST(NeedsSystemTest, Fulfill) {
    NeedsSystem sys;
    Need n;
    n.type = NeedType::Rest;
    n.value = 30.0f;
    sys.setNeed(n);

    sys.fulfill(NeedType::Rest, 50.0f);
    EXPECT_FLOAT_EQ(sys.getNeed(NeedType::Rest)->value, 80.0f);

    // Clamp at 100
    sys.fulfill(NeedType::Rest, 999.0f);
    EXPECT_FLOAT_EQ(sys.getNeed(NeedType::Rest)->value, 100.0f);
}

TEST(NeedsSystemTest, MostUrgent) {
    NeedsSystem sys;
    Need hunger; hunger.type = NeedType::Hunger; hunger.value = 20.0f;
    Need rest; rest.type = NeedType::Rest; rest.value = 60.0f;
    sys.setNeed(hunger);
    sys.setNeed(rest);

    const Need* urgent = sys.getMostUrgent();
    ASSERT_NE(urgent, nullptr);
    EXPECT_EQ(urgent->type, NeedType::Hunger);
}

TEST(NeedsSystemTest, UrgentNeeds) {
    NeedsSystem sys;
    Need hunger; hunger.type = NeedType::Hunger; hunger.value = 10.0f; hunger.urgencyThreshold = 30.0f;
    Need rest; rest.type = NeedType::Rest; rest.value = 60.0f; rest.urgencyThreshold = 30.0f;
    Need social; social.type = NeedType::Social; social.value = 25.0f; social.urgencyThreshold = 30.0f;
    sys.setNeed(hunger);
    sys.setNeed(rest);
    sys.setNeed(social);

    auto urgent = sys.getUrgentNeeds();
    EXPECT_EQ(urgent.size(), 2u); // Hunger and Social
}

TEST(NeedsSystemTest, ActivityFulfillment) {
    auto eating = NeedsSystem::getFulfillmentForActivity("Eat");
    EXPECT_FALSE(eating.empty());
    // Eat should fulfill Hunger
    bool fillsHunger = false;
    for (auto& [type, amount] : eating) {
        if (type == NeedType::Hunger) fillsHunger = true;
    }
    EXPECT_TRUE(fillsHunger);

    auto sleeping = NeedsSystem::getFulfillmentForActivity("Sleep");
    EXPECT_FALSE(sleeping.empty());

    auto unknown = NeedsSystem::getFulfillmentForActivity("NonexistentActivity");
    EXPECT_TRUE(unknown.empty());
}

TEST(NeedsSystemTest, JsonRoundTrip) {
    NeedsSystem sys;
    sys.initDefaults();
    sys.fulfill(NeedType::Hunger, -30.0f); // reduce hunger

    json j = sys.toJson();
    NeedsSystem sys2;
    sys2.fromJson(j);

    EXPECT_EQ(sys.getAllNeeds().size(), sys2.getAllNeeds().size());
    EXPECT_FLOAT_EQ(sys.getNeed(NeedType::Hunger)->value,
                    sys2.getNeed(NeedType::Hunger)->value);
}
