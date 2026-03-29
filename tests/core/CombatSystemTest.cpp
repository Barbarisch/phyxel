#include <gtest/gtest.h>
#include "core/CombatSystem.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "scene/Entity.h"

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

// Simple test entity with health and position
class TestEntity : public Scene::Entity {
public:
    TestEntity(glm::vec3 pos, float maxHP = 100.0f) : m_health(maxHP) {
        position = pos;
    }
    void update(float) override {}
    void render(Graphics::RenderCoordinator*) override {}
    HealthComponent* getHealthComponent() override { return &m_health; }
    const HealthComponent* getHealthComponent() const override { return &m_health; }
    void setMoveVelocity(const glm::vec3& v) override { lastKnockback = v; }

    HealthComponent m_health;
    glm::vec3 lastKnockback{0};
};

} // anonymous namespace

// ============================================================================
// Basic Attack
// ============================================================================

TEST(CombatSystemTest, AttackHitsNearbyEntity) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, -1.0f));
    auto* targetPtr = target.get();

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 25.0f;
    params.reach = 3.0f;
    params.coneAngleDeg = 90.0f;

    auto events = combat.performAttack(params, registry);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].targetId, "target");
    EXPECT_FLOAT_EQ(events[0].amount, 25.0f);
    EXPECT_FLOAT_EQ(events[0].actualDamage, 25.0f);
    EXPECT_FALSE(events[0].killed);
    EXPECT_FLOAT_EQ(targetPtr->m_health.getHealth(), 75.0f);
}

TEST(CombatSystemTest, AttackDoesNotHitSelf) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    registry.registerEntity(attacker.get(), "attacker", "test");

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 50.0f;
    params.reach = 5.0f;

    auto events = combat.performAttack(params, registry);
    EXPECT_EQ(events.size(), 0u);
}

// ============================================================================
// Cone Check
// ============================================================================

TEST(CombatSystemTest, AttackMissesOutsideCone) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    // Target is behind the attacker
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, 2.0f));

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1); // facing -Z
    params.damage = 25.0f;
    params.reach = 5.0f;
    params.coneAngleDeg = 90.0f;

    auto events = combat.performAttack(params, registry);
    EXPECT_EQ(events.size(), 0u);
}

TEST(CombatSystemTest, AttackMissesOutOfRange) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, -10.0f));

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 25.0f;
    params.reach = 3.0f; // Target is 10 away, reach is only 3

    auto events = combat.performAttack(params, registry);
    EXPECT_EQ(events.size(), 0u);
}

// ============================================================================
// Invulnerability
// ============================================================================

TEST(CombatSystemTest, InvulnerabilityPreventsSecondHit) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, -1.0f));

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 20.0f;
    params.reach = 3.0f;

    auto events1 = combat.performAttack(params, registry);
    EXPECT_EQ(events1.size(), 1u);

    auto events2 = combat.performAttack(params, registry);
    EXPECT_EQ(events2.size(), 0u); // Invulnerable
}

TEST(CombatSystemTest, InvulnerabilityExpiresAfterUpdate) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, -1.0f));

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 20.0f;
    params.reach = 3.0f;

    combat.performAttack(params, registry);

    // Expire invulnerability
    combat.update(1.0f);

    auto events = combat.performAttack(params, registry);
    EXPECT_EQ(events.size(), 1u);
}

// ============================================================================
// Kill
// ============================================================================

TEST(CombatSystemTest, KillSetsKilledFlag) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, -1.0f), 20.0f);

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 50.0f;
    params.reach = 3.0f;

    auto events = combat.performAttack(params, registry);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].killed);
}

TEST(CombatSystemTest, DeadEntitiesAreSkipped) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, -1.0f));
    target->m_health.kill();

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 25.0f;
    params.reach = 3.0f;

    auto events = combat.performAttack(params, registry);
    EXPECT_EQ(events.size(), 0u);
}

// ============================================================================
// Knockback
// ============================================================================

TEST(CombatSystemTest, KnockbackApplied) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, -1.0f));
    auto* targetPtr = target.get();

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 10.0f;
    params.reach = 3.0f;
    params.knockbackForce = 5.0f;

    combat.performAttack(params, registry);
    // Knockback should be nonzero
    EXPECT_NE(glm::length(targetPtr->lastKnockback), 0.0f);
}

// ============================================================================
// Callback
// ============================================================================

TEST(CombatSystemTest, OnDamageCallbackFired) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, -1.0f));

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    int callCount = 0;
    combat.setOnDamage([&](const DamageEvent&) { callCount++; });

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 10.0f;
    params.reach = 3.0f;

    combat.performAttack(params, registry);
    EXPECT_EQ(callCount, 1);
}

// ============================================================================
// DamageEvent JSON
// ============================================================================

TEST(CombatSystemTest, DamageEventToJson) {
    DamageEvent e;
    e.attackerId = "a";
    e.targetId = "b";
    e.amount = 10.0f;
    e.actualDamage = 10.0f;
    e.type = DamageType::Physical;
    e.knockback = glm::vec3(1, 2, 3);
    e.killed = false;

    auto j = e.toJson();
    EXPECT_EQ(j["attackerId"], "a");
    EXPECT_EQ(j["targetId"], "b");
    EXPECT_FLOAT_EQ(j["amount"].get<float>(), 10.0f);
    EXPECT_FALSE(j["killed"].get<bool>());
}

// ============================================================================
// Multiple Targets
// ============================================================================

TEST(CombatSystemTest, HitsMultipleTargetsInCone) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto t1 = std::make_unique<TestEntity>(glm::vec3(-0.5f, 0, -1.0f));
    auto t2 = std::make_unique<TestEntity>(glm::vec3(0.5f, 0, -1.0f));

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(t1.get(), "t1", "test");
    registry.registerEntity(t2.get(), "t2", "test");

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 15.0f;
    params.reach = 5.0f;
    params.coneAngleDeg = 120.0f;

    auto events = combat.performAttack(params, registry);
    EXPECT_EQ(events.size(), 2u);
}
