#include <gtest/gtest.h>

#include "core/EntityRegistry.h"
#include "core/Uuid.h"
#include "scene/Entity.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Phyxel;

namespace {

// Minimal concrete Entity (the base is abstract: update/render are pure virtual).
struct TestEntity : public Scene::Entity {
    void update(float) override {}
    void render(Graphics::RenderCoordinator*) override {}
};

TEST(EntityRegistryUuidTest, MintsAndResolvesByUuidOrId) {
    Core::EntityRegistry reg;
    TestEntity e;
    const std::string id = reg.registerEntity(&e);  // auto id + minted uuid
    ASSERT_FALSE(id.empty());

    const std::string uuid = reg.getUuid(id);
    ASSERT_TRUE(Core::Uuid::isValid(uuid)) << "no valid uuid minted at registration";
    EXPECT_EQ(reg.getEntity(uuid), &e) << "getEntity(uuid) failed to resolve";
    EXPECT_EQ(reg.getEntity(id), &e) << "legacy id resolution broke";
    // A well-formed but unknown uuid resolves to nothing, not to an arbitrary entity.
    EXPECT_EQ(reg.getEntity(Core::Uuid::generate()), nullptr);
}

TEST(EntityRegistryUuidTest, ExplicitUuidPreservedOnRestore) {
    // The respawn / authored-NPC path: registering with an explicit uuid keeps it,
    // so an entity's identity survives reload.
    Core::EntityRegistry reg;
    TestEntity e;
    const std::string want = Core::Uuid::generate();
    ASSERT_TRUE(reg.registerEntity(&e, "npc_bob", "npc", want));
    EXPECT_EQ(reg.getUuid("npc_bob"), want) << "explicit uuid was not preserved";
    EXPECT_EQ(reg.getEntity(want), &e);
}

TEST(EntityRegistryUuidTest, UnregisterByUuidClearsIndex) {
    Core::EntityRegistry reg;
    TestEntity e;
    reg.registerEntity(&e, "e1", "");
    const std::string uuid = reg.getUuid("e1");
    ASSERT_TRUE(Core::Uuid::isValid(uuid));

    EXPECT_TRUE(reg.unregisterEntity(uuid)) << "unregister by uuid failed";
    EXPECT_EQ(reg.getEntity(uuid), nullptr) << "entity still resolvable by uuid after unregister";
    EXPECT_EQ(reg.getEntity("e1"), nullptr) << "entity still resolvable by legacy id after unregister";
}

TEST(EntityRegistryUuidTest, UuidsUniqueAcrossManyEntities) {
    Core::EntityRegistry reg;
    std::vector<std::unique_ptr<TestEntity>> ents;
    std::unordered_set<std::string> uuids;
    constexpr int kN = 2000;
    for (int i = 0; i < kN; ++i) {
        ents.push_back(std::make_unique<TestEntity>());
        const std::string id = reg.registerEntity(ents.back().get());
        const std::string u = reg.getUuid(id);
        ASSERT_TRUE(Core::Uuid::isValid(u));
        ASSERT_TRUE(uuids.insert(u).second) << "duplicate entity uuid: " << u;
    }
    EXPECT_EQ(static_cast<int>(uuids.size()), kN);
}

}  // namespace
