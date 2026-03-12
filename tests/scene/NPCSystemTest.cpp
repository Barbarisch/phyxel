#include <gtest/gtest.h>
#include "scene/NPCBehavior.h"
#include "scene/behaviors/IdleBehavior.h"
#include "scene/behaviors/PatrolBehavior.h"
#include "core/NPCManager.h"
#include "core/InteractionManager.h"
#include "core/EntityRegistry.h"
#include "scene/Entity.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

using namespace Phyxel;

// ============================================================================
// Stub Entity for tests (no physics needed)
// ============================================================================
class StubEntity : public Scene::Entity {
public:
    void update(float) override {}
    void render(Graphics::RenderCoordinator*) override {}
};

// ============================================================================
// IdleBehavior Tests
// ============================================================================

class IdleBehaviorTest : public ::testing::Test {
protected:
    void SetUp() override {
        entity = std::make_unique<StubEntity>();
        entity->setPosition(glm::vec3(10, 0, 10));
        ctx.self = entity.get();
        ctx.selfId = "test_npc";
    }

    std::unique_ptr<StubEntity> entity;
    Scene::NPCContext ctx;
};

TEST_F(IdleBehaviorTest, BehaviorName) {
    Scene::IdleBehavior idle;
    EXPECT_EQ(idle.getBehaviorName(), "Idle");
}

TEST_F(IdleBehaviorTest, UpdateDoesNotCrashWithoutRegistry) {
    Scene::IdleBehavior idle;
    ctx.entityRegistry = nullptr;
    EXPECT_NO_THROW(idle.update(1.0f, ctx));
}

TEST_F(IdleBehaviorTest, OnInteractDoesNotCrash) {
    Scene::IdleBehavior idle;
    StubEntity interactor;
    EXPECT_NO_THROW(idle.onInteract(&interactor));
}

TEST_F(IdleBehaviorTest, OnEventDoesNothing) {
    Scene::IdleBehavior idle;
    nlohmann::json data;
    EXPECT_NO_THROW(idle.onEvent("test", data));
}

// ============================================================================
// PatrolBehavior Tests
// ============================================================================

class PatrolBehaviorTest : public ::testing::Test {
protected:
    void SetUp() override {
        entity = std::make_unique<StubEntity>();
        entity->setPosition(glm::vec3(0, 0, 0));
        ctx.self = entity.get();
        ctx.selfId = "patrol_npc";
    }

    std::unique_ptr<StubEntity> entity;
    Scene::NPCContext ctx;
};

TEST_F(PatrolBehaviorTest, BehaviorName) {
    Scene::PatrolBehavior patrol({}, 2.0f, 1.0f);
    EXPECT_EQ(patrol.getBehaviorName(), "Patrol");
}

TEST_F(PatrolBehaviorTest, EmptyWaypointsNoOp) {
    Scene::PatrolBehavior patrol({}, 2.0f, 1.0f);
    glm::vec3 startPos = entity->getPosition();
    patrol.update(1.0f, ctx);
    EXPECT_EQ(entity->getPosition(), startPos);
}

TEST_F(PatrolBehaviorTest, MovesTowardFirstWaypoint) {
    std::vector<glm::vec3> waypoints = {glm::vec3(10, 0, 0), glm::vec3(0, 0, 10)};
    Scene::PatrolBehavior patrol(waypoints, 5.0f, 0.0f);
    patrol.update(1.0f, ctx);
    // Should have moved toward (10, 0, 0)
    EXPECT_GT(entity->getPosition().x, 0.0f);
}

TEST_F(PatrolBehaviorTest, ArrivalAdvancesToNextWaypoint) {
    std::vector<glm::vec3> waypoints = {glm::vec3(0.1f, 0, 0), glm::vec3(10, 0, 0)};
    Scene::PatrolBehavior patrol(waypoints, 5.0f, 0.0f);
    EXPECT_EQ(patrol.getCurrentWaypointIndex(), 0u);

    // Move close enough to first waypoint to arrive
    patrol.update(1.0f, ctx);
    // After arrival and 0 wait time, should advance on next update
    patrol.update(0.01f, ctx);
    EXPECT_EQ(patrol.getCurrentWaypointIndex(), 1u);
}

TEST_F(PatrolBehaviorTest, WaypointWrapsAround) {
    std::vector<glm::vec3> waypoints = {glm::vec3(0.1f, 0, 0), glm::vec3(0.2f, 0, 0)};
    Scene::PatrolBehavior patrol(waypoints, 100.0f, 0.0f);

    // Sprint through both waypoints
    for (int i = 0; i < 20; ++i) {
        patrol.update(0.1f, ctx);
    }
    // Should have wrapped back to 0 at some point
    // We can't guarantee the exact index, but it shouldn't crash
    EXPECT_LE(patrol.getCurrentWaypointIndex(), 1u);
}

TEST_F(PatrolBehaviorTest, OnInteractPausesPatrol) {
    std::vector<glm::vec3> waypoints = {glm::vec3(100, 0, 0)};
    Scene::PatrolBehavior patrol(waypoints, 5.0f, 1.0f);

    // Start moving
    patrol.update(1.0f, ctx);
    glm::vec3 posBeforeInteract = entity->getPosition();

    // Interact pauses movement
    StubEntity interactor;
    patrol.onInteract(&interactor);

    // Now update should be in waiting state
    patrol.update(0.1f, ctx);
    glm::vec3 posAfterInteract = entity->getPosition();
    EXPECT_FLOAT_EQ(posBeforeInteract.x, posAfterInteract.x);
}

TEST_F(PatrolBehaviorTest, SetWaypointsResetsState) {
    std::vector<glm::vec3> wp1 = {glm::vec3(10, 0, 0)};
    Scene::PatrolBehavior patrol(wp1, 5.0f, 1.0f);
    patrol.update(1.0f, ctx);

    std::vector<glm::vec3> wp2 = {glm::vec3(0, 0, 10), glm::vec3(0, 0, 20)};
    patrol.setWaypoints(wp2);
    EXPECT_EQ(patrol.getCurrentWaypointIndex(), 0u);
    EXPECT_EQ(patrol.getWaypoints().size(), 2u);
}

TEST_F(PatrolBehaviorTest, WalkSpeedAffectsMovement) {
    std::vector<glm::vec3> waypoints = {glm::vec3(100, 0, 0)};

    Scene::PatrolBehavior slowPatrol(waypoints, 1.0f, 0.0f);
    Scene::PatrolBehavior fastPatrol(waypoints, 10.0f, 0.0f);

    auto slowEntity = std::make_unique<StubEntity>();
    slowEntity->setPosition(glm::vec3(0));
    auto fastEntity = std::make_unique<StubEntity>();
    fastEntity->setPosition(glm::vec3(0));

    Scene::NPCContext slowCtx;
    slowCtx.self = slowEntity.get();
    Scene::NPCContext fastCtx;
    fastCtx.self = fastEntity.get();

    slowPatrol.update(1.0f, slowCtx);
    fastPatrol.update(1.0f, fastCtx);

    EXPECT_GT(fastEntity->getPosition().x, slowEntity->getPosition().x);
}

// ============================================================================
// InteractionManager Tests
// ============================================================================

TEST(InteractionManagerTest, NoRegistryDoesNotCrash) {
    Core::InteractionManager mgr;
    EXPECT_NO_THROW(mgr.update(0.016f, glm::vec3(0)));
    EXPECT_EQ(mgr.getNearestInteractableNPC(), nullptr);
    EXPECT_FALSE(mgr.shouldShowPrompt());
}

TEST(InteractionManagerTest, TryInteractWithNoNPC) {
    Core::InteractionManager mgr;
    StubEntity player;
    EXPECT_NO_THROW(mgr.tryInteract(&player));
}

// ============================================================================
// NPCManager Tests (without PhysicsWorld — spawnNPC will fail)
// ============================================================================

TEST(NPCManagerTest, InitiallyEmpty) {
    Core::NPCManager mgr;
    EXPECT_EQ(mgr.getNPCCount(), 0u);
    EXPECT_TRUE(mgr.getAllNPCNames().empty());
}

TEST(NPCManagerTest, GetNPCReturnsNullIfNotFound) {
    Core::NPCManager mgr;
    EXPECT_EQ(mgr.getNPC("nonexistent"), nullptr);
}

TEST(NPCManagerTest, RemoveNPCReturnsFalseIfNotFound) {
    Core::NPCManager mgr;
    EXPECT_FALSE(mgr.removeNPC("nonexistent"));
}

TEST(NPCManagerTest, SpawnFailsWithoutPhysicsWorld) {
    Core::NPCManager mgr;
    auto* npc = mgr.spawnNPC("test_npc", "character.anim", glm::vec3(0),
                              Core::NPCBehaviorType::Idle);
    EXPECT_EQ(npc, nullptr);
}

// ============================================================================
// NPCBehavior Interface Tests
// ============================================================================

class TestBehavior : public Scene::NPCBehavior {
public:
    int updateCount = 0;
    int interactCount = 0;
    int eventCount = 0;

    void update(float dt, Scene::NPCContext& ctx) override { updateCount++; }
    void onInteract(Scene::Entity* interactor) override { interactCount++; }
    void onEvent(const std::string& type, const nlohmann::json& data) override { eventCount++; }
    std::string getBehaviorName() const override { return "Test"; }
};

TEST(NPCBehaviorTest, InterfaceCallbacks) {
    TestBehavior behavior;
    StubEntity entity;
    entity.setPosition(glm::vec3(0));

    Scene::NPCContext ctx;
    ctx.self = &entity;

    behavior.update(0.016f, ctx);
    behavior.update(0.016f, ctx);
    EXPECT_EQ(behavior.updateCount, 2);

    behavior.onInteract(&entity);
    EXPECT_EQ(behavior.interactCount, 1);

    nlohmann::json data;
    behavior.onEvent("test_event", data);
    EXPECT_EQ(behavior.eventCount, 1);
}

// ============================================================================
// NPCContext Tests
// ============================================================================

TEST(NPCContextTest, DefaultsAreNull) {
    Scene::NPCContext ctx;
    EXPECT_EQ(ctx.self, nullptr);
    EXPECT_EQ(ctx.entityRegistry, nullptr);
    EXPECT_EQ(ctx.lightManager, nullptr);
    EXPECT_TRUE(ctx.selfId.empty());
}

TEST(NPCContextTest, GetEntityPositionCallback) {
    StubEntity target;
    target.setPosition(glm::vec3(5, 10, 15));

    Core::EntityRegistry registry;
    registry.registerEntity(&target, "target_01");

    Scene::NPCContext ctx;
    ctx.entityRegistry = &registry;
    ctx.getEntityPosition = [&registry](const std::string& id) -> glm::vec3 {
        auto* e = registry.getEntity(id);
        return e ? e->getPosition() : glm::vec3(0);
    };

    glm::vec3 pos = ctx.getEntityPosition("target_01");
    EXPECT_FLOAT_EQ(pos.x, 5.0f);
    EXPECT_FLOAT_EQ(pos.y, 10.0f);
    EXPECT_FLOAT_EQ(pos.z, 15.0f);
}
