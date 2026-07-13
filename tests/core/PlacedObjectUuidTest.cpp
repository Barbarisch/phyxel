#include <gtest/gtest.h>

#include "core/PlacedObjectManager.h"
#include "core/Uuid.h"

#include <string>
#include <unordered_set>

using namespace Phyxel::Core;

// ============================================================================
// Phase 1 of the stable-UUID work: every PlacedObject carries a persistent
// RFC-4122 v4 uuid, is resolvable by EITHER its legacy base_N id OR its uuid,
// the uuid round-trips through toJson/fromJson, old (uuid-less) saves lazily
// backfill a uuid, and uuids are globally unique. registerStructure /
// registerItemProp do not touch the chunk/template deps, so a null-dep manager
// exercises the registry directly (remove() is tested via an item prop, whose
// category skips clearRegion).
// ============================================================================

// --- Serialization level (no manager) ---

TEST(PlacedObjectUuidTest, UuidRoundTripsThroughJson) {
    PlacedObject obj;
    obj.id = "chair_3";
    obj.uuid = Uuid::generate();
    obj.templateName = "chair";
    const std::string original = obj.uuid;

    const PlacedObject restored = PlacedObject::fromJson(obj.toJson());
    EXPECT_EQ(restored.uuid, original) << "uuid dropped/changed on serialization round-trip";
    EXPECT_TRUE(Uuid::isValid(restored.uuid));
}

TEST(PlacedObjectUuidTest, LazyBackfillMintsUuidForOldSaves) {
    // Simulate a pre-uuid saved object: JSON with an id but NO "uuid" field.
    nlohmann::json legacy = {
        {"id", "cottage_1"}, {"template_name", "cottage"}, {"category", "structure"},
        {"parent_id", ""}, {"rotation", 0},
        {"position", {{"x", 0}, {"y", 0}, {"z", 0}}},
        {"bounding_min", {{"x", 0}, {"y", 0}, {"z", 0}}},
        {"bounding_max", {{"x", 4}, {"y", 4}, {"z", 4}}}
    };
    ASSERT_FALSE(legacy.contains("uuid"));
    const PlacedObject restored = PlacedObject::fromJson(legacy);
    EXPECT_TRUE(Uuid::isValid(restored.uuid))
        << "old uuid-less object was not backfilled with a valid uuid on load";
}

// --- Manager level (null deps; registerStructure/registerItemProp don't deref them) ---

TEST(PlacedObjectUuidTest, ResolveByUuidOrLegacyId) {
    PlacedObjectManager mgr(nullptr, nullptr, nullptr);
    const std::string legacyId =
        mgr.registerStructure("cottage", {0, 0, 0}, 0, {0, 0, 0}, {4, 4, 4});
    ASSERT_FALSE(legacyId.empty());

    const PlacedObject* byLegacy = mgr.get(legacyId);
    ASSERT_NE(byLegacy, nullptr);
    ASSERT_TRUE(Uuid::isValid(byLegacy->uuid)) << "registered structure has no valid uuid";

    const std::string uuid = byLegacy->uuid;
    const PlacedObject* byUuid = mgr.get(uuid);
    ASSERT_NE(byUuid, nullptr) << "get(uuid) failed to resolve";
    EXPECT_EQ(byUuid->id, legacyId) << "uuid resolved to the wrong object";

    // A well-formed but unknown uuid resolves to nothing (not to some arbitrary object).
    EXPECT_EQ(mgr.get(Uuid::generate()), nullptr);
}

TEST(PlacedObjectUuidTest, RemoveByUuidAlsoClearsIndex) {
    PlacedObjectManager mgr(nullptr, nullptr, nullptr);
    // Item props skip clearRegion on remove, so a null chunk-manager is safe here.
    const std::string legacyId =
        mgr.registerItemProp("iron_sword", "sword", {2, 2, 2}, 0, {2, 2, 2}, {3, 3, 3}, "Iron Sword");
    ASSERT_FALSE(legacyId.empty());
    const std::string uuid = mgr.get(legacyId)->uuid;

    EXPECT_TRUE(mgr.remove(uuid)) << "remove by uuid failed";
    EXPECT_EQ(mgr.get(uuid), nullptr) << "object still resolvable by uuid after removal";
    EXPECT_EQ(mgr.get(legacyId), nullptr) << "object still resolvable by legacy id after removal";
    // The uuid index must not leak: re-registering must not collide.
    EXPECT_FALSE(mgr.registerItemProp("iron_sword", "sword", {2, 2, 2}, 0, {2, 2, 2}, {3, 3, 3}, "Iron Sword").empty());
}

TEST(PlacedObjectUuidTest, UuidsAreUniqueAcrossManyObjects) {
    PlacedObjectManager mgr(nullptr, nullptr, nullptr);
    constexpr int kN = 10000;
    for (int i = 0; i < kN; ++i) {
        ASSERT_FALSE(mgr.registerStructure("hut", {i, 0, 0}, 0, {i, 0, 0}, {i + 1, 1, 1}).empty());
    }
    std::unordered_set<std::string> uuids;
    for (const auto& obj : mgr.list()) {
        ASSERT_TRUE(Uuid::isValid(obj.uuid)) << "object " << obj.id << " has an invalid uuid";
        ASSERT_TRUE(uuids.insert(obj.uuid).second) << "duplicate uuid: " << obj.uuid;
    }
    EXPECT_EQ(static_cast<int>(uuids.size()), kN);
}

TEST(PlacedObjectUuidTest, FromJsonRebuildsUuidIndexAndBackfills) {
    // A saved registry blob containing a uuid-less object: after fromJson, it must
    // be resolvable by its (freshly minted) uuid AND its legacy id.
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back({
        {"id", "well_1"}, {"template_name", "well"}, {"category", "structure"},
        {"parent_id", ""}, {"rotation", 0},
        {"position", {{"x", 5}, {"y", 0}, {"z", 5}}},
        {"bounding_min", {{"x", 5}, {"y", 0}, {"z", 5}}},
        {"bounding_max", {{"x", 7}, {"y", 3}, {"z", 7}}}
    });
    PlacedObjectManager mgr(nullptr, nullptr, nullptr);
    mgr.fromJson(arr);

    const PlacedObject* byLegacy = mgr.get("well_1");
    ASSERT_NE(byLegacy, nullptr);
    ASSERT_TRUE(Uuid::isValid(byLegacy->uuid));
    const PlacedObject* byUuid = mgr.get(byLegacy->uuid);
    ASSERT_NE(byUuid, nullptr) << "uuid index not rebuilt on fromJson";
    EXPECT_EQ(byUuid->id, "well_1");
}
