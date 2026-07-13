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

TEST(EntityRegistryUuidTest, UnregisterByUuidClearsIndexNoLeak) {
    Core::EntityRegistry reg;
    TestEntity e1, e2;
    reg.registerEntity(&e1, "slot", "");
    const std::string uuid1 = reg.getUuid("slot");
    ASSERT_TRUE(Core::Uuid::isValid(uuid1));

    EXPECT_TRUE(reg.unregisterEntity(uuid1)) << "unregister by uuid failed";
    EXPECT_EQ(reg.getEntity(uuid1), nullptr) << "entity still resolvable by uuid after unregister";
    EXPECT_EQ(reg.getEntity("slot"), nullptr) << "entity still resolvable by legacy id after unregister";

    // The REAL index-leak measurement (a bare m_entities-erase would mask a leaked
    // m_uuidToId entry): re-register a DIFFERENT entity under the SAME legacy id. If
    // unregister failed to drop uuid1 from m_uuidToId, uuid1 would still map to "slot"
    // and now resolve to the NEW entity. It must resolve to nothing.
    reg.registerEntity(&e2, "slot", "");
    const std::string uuid2 = reg.getUuid("slot");
    EXPECT_NE(uuid1, uuid2) << "re-registration reused the old uuid";
    EXPECT_EQ(reg.getEntity(uuid1), nullptr)
        << "stale m_uuidToId entry leaked across unregister/re-register";
    EXPECT_EQ(reg.getEntity(uuid2), &e2) << "new entity not resolvable by its own uuid";
}

TEST(EntityRegistryUuidTest, DuplicateExplicitUuidRejected) {
    // The restore path supplies an explicit uuid; a clash must not silently clobber the
    // existing mapping (which would orphan the first entity's uuid).
    Core::EntityRegistry reg;
    TestEntity e1, e2;
    const std::string u = Core::Uuid::generate();
    ASSERT_TRUE(reg.registerEntity(&e1, "a", "", u));
    EXPECT_FALSE(reg.registerEntity(&e2, "b", "", u)) << "duplicate explicit uuid should be rejected";
    EXPECT_EQ(reg.getEntity(u), &e1) << "original entity's uuid mapping was clobbered by the duplicate";
}

TEST(EntityRegistryUuidTest, SamePointerReRegisterDropsOldRegistration) {
    // The respawn mechanism: createAnimatedCharacter self-registers the entity as "animated_N",
    // then the explicit registerEntity(...,uuid) re-registers the SAME pointer under the real id.
    // The throwaway registration (and its auto-minted uuid) must be fully gone.
    Core::EntityRegistry reg;
    TestEntity e;
    ASSERT_TRUE(reg.registerEntity(&e, "animated_7", "animated"));
    const std::string throwawayUuid = reg.getUuid("animated_7");
    const std::string want = Core::Uuid::generate();
    ASSERT_TRUE(reg.registerEntity(&e, "persist_probe", "animated", want));

    EXPECT_EQ(reg.getEntity("animated_7"), nullptr) << "throwaway registration not dropped";
    EXPECT_EQ(reg.size(), 1u) << "duplicate registration for the same pointer";
    EXPECT_EQ(reg.getEntity("persist_probe"), &e);
    EXPECT_EQ(reg.getEntity(want), &e);
    EXPECT_EQ(reg.getUuid("persist_probe"), want);
    EXPECT_EQ(reg.getEntity(throwawayUuid), nullptr) << "throwaway uuid leaked in the index";
}

TEST(EntityRegistryUuidTest, AutoIdReseededPastRestoredEntities) {
    // The persistence churn bug: a fresh process restarts m_nextAutoId at 1, so after restoring
    // an auto-id ("entity_1") from a save, the NEXT auto-id spawn must not regenerate the same id
    // (which would collide, silently fail to register the new entity, and misattribute the
    // restored entity's uuid to it). Two registry instances model two process lifetimes.
    Core::EntityRegistry reg1;
    TestEntity a;
    const std::string id1 = reg1.registerEntity(&a);      // auto id, e.g. "entity_1"
    const std::string uuid1 = reg1.getUuid(id1);
    ASSERT_FALSE(id1.empty());

    Core::EntityRegistry reg2;                             // fresh m_nextAutoId{1}
    TestEntity restored, fresh;
    ASSERT_TRUE(reg2.registerEntity(&restored, id1, "animated", uuid1));  // respawn on reload
    const std::string id2 = reg2.registerEntity(&fresh);                  // new auto-id spawn
    ASSERT_FALSE(id2.empty()) << "new auto-id spawn silently failed (id collision with restored entity)";
    EXPECT_NE(id2, id1) << "auto-id counter not reseeded past the restored entity";
    EXPECT_EQ(reg2.getEntity(id1), &restored) << "restored entity clobbered by the new spawn";
    EXPECT_EQ(reg2.getEntity(id2), &fresh);
    EXPECT_NE(reg2.getUuid(id2), uuid1) << "new spawn misattributed the restored entity's uuid";
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
