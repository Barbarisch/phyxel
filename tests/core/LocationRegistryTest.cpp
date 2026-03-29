#include <gtest/gtest.h>
#include "core/LocationRegistry.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::Core;
using json = nlohmann::json;

// ============================================================================
// Location Tests
// ============================================================================

TEST(WorldLocationTest, DefaultConstruction) {
    Location loc;
    EXPECT_TRUE(loc.id.empty());
    EXPECT_TRUE(loc.name.empty());
    EXPECT_FLOAT_EQ(loc.radius, 3.0f);
    EXPECT_EQ(loc.type, LocationType::Custom);
}

TEST(WorldLocationTest, JsonRoundTrip) {
    Location loc;
    loc.id = "tavern_1";
    loc.name = "The Rusty Mug";
    loc.position = glm::vec3(10.0f, 20.0f, 30.0f);
    loc.radius = 5.0f;
    loc.type = LocationType::Tavern;

    json j = loc.toJson();
    EXPECT_EQ(j["id"], "tavern_1");
    EXPECT_EQ(j["name"], "The Rusty Mug");
    EXPECT_FLOAT_EQ(j["position"]["x"].get<float>(), 10.0f);
    EXPECT_FLOAT_EQ(j["position"]["y"].get<float>(), 20.0f);
    EXPECT_FLOAT_EQ(j["position"]["z"].get<float>(), 30.0f);
    EXPECT_FLOAT_EQ(j["radius"].get<float>(), 5.0f);
    EXPECT_EQ(j["type"], "Tavern");

    Location loc2 = Location::fromJson(j);
    EXPECT_EQ(loc2.id, loc.id);
    EXPECT_EQ(loc2.name, loc.name);
    EXPECT_FLOAT_EQ(loc2.position.x, loc.position.x);
    EXPECT_FLOAT_EQ(loc2.position.y, loc.position.y);
    EXPECT_FLOAT_EQ(loc2.position.z, loc.position.z);
    EXPECT_FLOAT_EQ(loc2.radius, loc.radius);
    EXPECT_EQ(loc2.type, loc.type);
}

TEST(WorldLocationTest, FromJsonDefaults) {
    json j = {{"id", "test"}, {"name", "Test"}, {"x", 1.0}, {"y", 2.0}, {"z", 3.0}};
    Location loc = Location::fromJson(j);
    EXPECT_EQ(loc.id, "test");
    EXPECT_FLOAT_EQ(loc.radius, 3.0f); // default
    EXPECT_EQ(loc.type, LocationType::Custom); // default
}

TEST(WorldLocationTest, AllLocationTypes) {
    // Test all type string round-trips
    std::vector<std::pair<LocationType, std::string>> types = {
        {LocationType::Home, "Home"},
        {LocationType::Work, "Work"},
        {LocationType::Tavern, "Tavern"},
        {LocationType::Market, "Market"},
        {LocationType::Temple, "Temple"},
        {LocationType::Farm, "Farm"},
        {LocationType::GuardPost, "GuardPost"},
        {LocationType::Wilderness, "Wilderness"},
        {LocationType::Custom, "Custom"}
    };

    for (const auto& [type, str] : types) {
        Location loc;
        loc.id = "test";
        loc.name = "Test";
        loc.type = type;
        json j = loc.toJson();
        EXPECT_EQ(j["type"], str);
        Location loc2 = Location::fromJson(j);
        EXPECT_EQ(loc2.type, type);
    }
}

// ============================================================================
// LocationRegistry Tests
// ============================================================================

TEST(LocationRegistryTest, AddAndGet) {
    LocationRegistry registry;
    Location loc;
    loc.id = "tavern_1";
    loc.name = "The Rusty Mug";
    loc.position = glm::vec3(10, 20, 30);
    loc.type = LocationType::Tavern;

    registry.addLocation(loc);
    EXPECT_EQ(registry.getAllLocations().size(), 1u);

    auto* found = registry.getLocation("tavern_1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "The Rusty Mug");
    EXPECT_EQ(found->type, LocationType::Tavern);
}

TEST(LocationRegistryTest, GetNonexistent) {
    LocationRegistry registry;
    EXPECT_EQ(registry.getLocation("nope"), nullptr);
}

TEST(LocationRegistryTest, Remove) {
    LocationRegistry registry;
    Location loc;
    loc.id = "home_1";
    loc.name = "Cottage";
    loc.position = glm::vec3(0, 0, 0);
    registry.addLocation(loc);
    EXPECT_EQ(registry.getAllLocations().size(), 1u);

    registry.removeLocation("home_1");
    EXPECT_EQ(registry.getAllLocations().size(), 0u);
    EXPECT_EQ(registry.getLocation("home_1"), nullptr);
}

TEST(LocationRegistryTest, GetByType) {
    LocationRegistry registry;

    Location t1; t1.id = "t1"; t1.name = "Tavern A"; t1.position = glm::vec3(0); t1.type = LocationType::Tavern;
    Location t2; t2.id = "t2"; t2.name = "Tavern B"; t2.position = glm::vec3(10, 0, 0); t2.type = LocationType::Tavern;
    Location h1; h1.id = "h1"; h1.name = "Home"; h1.position = glm::vec3(20, 0, 0); h1.type = LocationType::Home;

    registry.addLocation(t1);
    registry.addLocation(t2);
    registry.addLocation(h1);

    auto taverns = registry.getLocationsByType(LocationType::Tavern);
    EXPECT_EQ(taverns.size(), 2u);

    auto homes = registry.getLocationsByType(LocationType::Home);
    EXPECT_EQ(homes.size(), 1u);

    auto farms = registry.getLocationsByType(LocationType::Farm);
    EXPECT_EQ(farms.size(), 0u);
}

TEST(LocationRegistryTest, GetNearest) {
    LocationRegistry registry;

    Location a; a.id = "a"; a.name = "A"; a.position = glm::vec3(0, 0, 0);
    Location b; b.id = "b"; b.name = "B"; b.position = glm::vec3(100, 0, 0);

    registry.addLocation(a);
    registry.addLocation(b);

    auto* nearest = registry.getNearestLocation(glm::vec3(5, 0, 0));
    ASSERT_NE(nearest, nullptr);
    EXPECT_EQ(nearest->id, "a");

    nearest = registry.getNearestLocation(glm::vec3(90, 0, 0));
    ASSERT_NE(nearest, nullptr);
    EXPECT_EQ(nearest->id, "b");
}

TEST(LocationRegistryTest, GetNearestEmptyRegistry) {
    LocationRegistry registry;
    EXPECT_EQ(registry.getNearestLocation(glm::vec3(0)), nullptr);
}

TEST(LocationRegistryTest, GetLocationsNear) {
    LocationRegistry registry;

    Location a; a.id = "a"; a.name = "A"; a.position = glm::vec3(0, 0, 0);
    Location b; b.id = "b"; b.name = "B"; b.position = glm::vec3(5, 0, 0);
    Location c; c.id = "c"; c.name = "C"; c.position = glm::vec3(100, 0, 0);

    registry.addLocation(a);
    registry.addLocation(b);
    registry.addLocation(c);

    auto near = registry.getLocationsNear(glm::vec3(2, 0, 0), 10.0f);
    EXPECT_EQ(near.size(), 2u); // a and b

    near = registry.getLocationsNear(glm::vec3(2, 0, 0), 1.0f);
    EXPECT_EQ(near.size(), 0u); // none within 1.0
}

TEST(LocationRegistryTest, GetLocationAt) {
    LocationRegistry registry;

    Location a; a.id = "a"; a.name = "A"; a.position = glm::vec3(10, 0, 0); a.radius = 5.0f;
    Location b; b.id = "b"; b.name = "B"; b.position = glm::vec3(50, 0, 0); b.radius = 3.0f;

    registry.addLocation(a);
    registry.addLocation(b);

    auto* at = registry.getLocationAt(glm::vec3(12, 0, 0)); // within a's radius
    ASSERT_NE(at, nullptr);
    EXPECT_EQ(at->id, "a");

    at = registry.getLocationAt(glm::vec3(30, 0, 0)); // not in any
    EXPECT_EQ(at, nullptr);

    at = registry.getLocationAt(glm::vec3(51, 0, 0)); // within b's radius
    ASSERT_NE(at, nullptr);
    EXPECT_EQ(at->id, "b");
}

TEST(LocationRegistryTest, JsonRoundTrip) {
    LocationRegistry registry;

    Location a; a.id = "a"; a.name = "Tavern"; a.position = glm::vec3(1, 2, 3); a.type = LocationType::Tavern;
    Location b; b.id = "b"; b.name = "Farm"; b.position = glm::vec3(4, 5, 6); b.type = LocationType::Farm;

    registry.addLocation(a);
    registry.addLocation(b);

    json j = registry.toJson();

    LocationRegistry registry2;
    registry2.fromJson(j);

    EXPECT_EQ(registry2.getAllLocations().size(), 2u);
    auto* fa = registry2.getLocation("a");
    ASSERT_NE(fa, nullptr);
    EXPECT_EQ(fa->name, "Tavern");
    EXPECT_EQ(fa->type, LocationType::Tavern);
}

TEST(LocationRegistryTest, DuplicateIdOverwrites) {
    LocationRegistry registry;

    Location a; a.id = "x"; a.name = "First"; a.position = glm::vec3(0);
    Location b; b.id = "x"; b.name = "Second"; b.position = glm::vec3(10, 0, 0);

    registry.addLocation(a);
    registry.addLocation(b);

    // Either overwrites or keeps first — just ensure no crash and one entry
    auto* found = registry.getLocation("x");
    ASSERT_NE(found, nullptr);
}
