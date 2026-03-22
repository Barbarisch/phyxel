#include <gtest/gtest.h>
#include "story/EventBus.h"

using namespace Phyxel::Story;

// ============================================================================
// EventBus Tests
// ============================================================================

class EventBusTest : public ::testing::Test {
protected:
    EventBus bus;

    WorldEvent makeEvent(const std::string& type, const std::string& id = "evt1") {
        WorldEvent e;
        e.id = id;
        e.type = type;
        e.timestamp = 1.0f;
        e.location = glm::vec3(10, 16, 10);
        return e;
    }
};

TEST_F(EventBusTest, SubscribeAndEmit) {
    int callCount = 0;
    bus.subscribe([&](const WorldEvent&) { ++callCount; });
    bus.emit(makeEvent("test"));
    EXPECT_EQ(callCount, 1);
}

TEST_F(EventBusTest, MultipleListenersReceiveEvent) {
    int count1 = 0, count2 = 0;
    bus.subscribe([&](const WorldEvent&) { ++count1; });
    bus.subscribe([&](const WorldEvent&) { ++count2; });
    bus.emit(makeEvent("test"));
    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);
}

TEST_F(EventBusTest, SubscribeToTypeFilters) {
    int combatCount = 0, allCount = 0;
    bus.subscribeToType("combat", [&](const WorldEvent&) { ++combatCount; });
    bus.subscribe([&](const WorldEvent&) { ++allCount; });

    bus.emit(makeEvent("combat"));
    bus.emit(makeEvent("dialogue"));

    EXPECT_EQ(combatCount, 1);
    EXPECT_EQ(allCount, 2);
}

TEST_F(EventBusTest, UnsubscribeRemovesListener) {
    int callCount = 0;
    int id = bus.subscribe([&](const WorldEvent&) { ++callCount; });
    bus.emit(makeEvent("test"));
    EXPECT_EQ(callCount, 1);

    bus.unsubscribe(id);
    bus.emit(makeEvent("test"));
    EXPECT_EQ(callCount, 1); // Not called again
}

TEST_F(EventBusTest, UnsubscribeOnlyRemovesTarget) {
    int count1 = 0, count2 = 0;
    int id1 = bus.subscribe([&](const WorldEvent&) { ++count1; });
    bus.subscribe([&](const WorldEvent&) { ++count2; });

    bus.unsubscribe(id1);
    bus.emit(makeEvent("test"));

    EXPECT_EQ(count1, 0);
    EXPECT_EQ(count2, 1);
}

TEST_F(EventBusTest, SubscriberCountTracksCorrectly) {
    EXPECT_EQ(bus.subscriberCount(), 0u);
    int id1 = bus.subscribe([](const WorldEvent&) {});
    EXPECT_EQ(bus.subscriberCount(), 1u);
    bus.subscribe([](const WorldEvent&) {});
    EXPECT_EQ(bus.subscriberCount(), 2u);
    bus.unsubscribe(id1);
    EXPECT_EQ(bus.subscriberCount(), 1u);
}

TEST_F(EventBusTest, ClearRemovesAll) {
    bus.subscribe([](const WorldEvent&) {});
    bus.subscribe([](const WorldEvent&) {});
    EXPECT_EQ(bus.subscriberCount(), 2u);
    bus.clear();
    EXPECT_EQ(bus.subscriberCount(), 0u);
}

TEST_F(EventBusTest, EmitWithNoSubscribers) {
    // Should not crash
    bus.emit(makeEvent("test"));
}

TEST_F(EventBusTest, ListenerReceivesCorrectEventData) {
    std::string receivedType;
    std::string receivedId;
    bus.subscribe([&](const WorldEvent& e) {
        receivedType = e.type;
        receivedId = e.id;
    });
    bus.emit(makeEvent("combat", "battle_1"));
    EXPECT_EQ(receivedType, "combat");
    EXPECT_EQ(receivedId, "battle_1");
}

TEST_F(EventBusTest, TypeFilterIsCaseSensitive) {
    int count = 0;
    bus.subscribeToType("Combat", [&](const WorldEvent&) { ++count; });
    bus.emit(makeEvent("combat"));
    EXPECT_EQ(count, 0);
}

TEST_F(EventBusTest, MultipleTypeSubscriptions) {
    int combatCount = 0, dialogueCount = 0;
    bus.subscribeToType("combat", [&](const WorldEvent&) { ++combatCount; });
    bus.subscribeToType("dialogue", [&](const WorldEvent&) { ++dialogueCount; });

    bus.emit(makeEvent("combat"));
    bus.emit(makeEvent("dialogue"));
    bus.emit(makeEvent("discovery"));

    EXPECT_EQ(combatCount, 1);
    EXPECT_EQ(dialogueCount, 1);
}

TEST_F(EventBusTest, SubscribeReturnsDifferentIds) {
    int id1 = bus.subscribe([](const WorldEvent&) {});
    int id2 = bus.subscribe([](const WorldEvent&) {});
    int id3 = bus.subscribeToType("x", [](const WorldEvent&) {});
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
}
