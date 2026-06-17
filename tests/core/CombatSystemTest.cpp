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
// External invulnerability query (dodge i-frames)
// ============================================================================

TEST(CombatSystemTest, InvulnerabilityQuerySkipsTarget) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, -1.0f));
    auto* targetPtr = target.get();

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    // Mid-dodge: the target reports invulnerable → hit is skipped.
    bool invulnerable = true;
    combat.setInvulnerabilityQuery(
        [&](const Scene::Entity* e) { return e == targetPtr && invulnerable; });

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 20.0f;
    params.reach = 3.0f;

    auto blocked = combat.performAttack(params, registry);
    EXPECT_EQ(blocked.size(), 0u);
    EXPECT_FLOAT_EQ(targetPtr->m_health.getHealth(), 100.0f);

    // i-frames end → the same attack now connects.
    invulnerable = false;
    auto hit = combat.performAttack(params, registry);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_FLOAT_EQ(targetPtr->m_health.getHealth(), 80.0f);
}

TEST(CombatSystemTest, InvulnerabilityQueryReceivesCandidateEntity) {
    CombatSystem combat;
    EntityRegistry registry;

    auto attacker = std::make_unique<TestEntity>(glm::vec3(0, 0, 0));
    auto target = std::make_unique<TestEntity>(glm::vec3(0, 0, -1.0f));
    auto* targetPtr = target.get();

    registry.registerEntity(attacker.get(), "attacker", "test");
    registry.registerEntity(target.get(), "target", "test");

    const Scene::Entity* seen = nullptr;
    combat.setInvulnerabilityQuery([&](const Scene::Entity* e) {
        seen = e;
        return false;  // don't block; just observe
    });

    CombatSystem::AttackParams params;
    params.attackerId = "attacker";
    params.attackerPos = glm::vec3(0, 0, 0);
    params.attackerForward = glm::vec3(0, 0, -1);
    params.damage = 5.0f;
    params.reach = 3.0f;

    combat.performAttack(params, registry);
    EXPECT_EQ(seen, targetPtr);  // the query is consulted with the real target
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
// applyDamage — the single damage entry point (S2)
// ============================================================================

TEST(CombatSystemTest, ApplyDamageMutatesHealthAndReturnsEvent) {
    CombatSystem combat;
    TestEntity target(glm::vec3(0, 0, 0), 50.0f);

    auto ev = combat.applyDamage(&target, "target", 20.0f, "src");
    EXPECT_FLOAT_EQ(ev.actualDamage, 20.0f);
    EXPECT_FALSE(ev.killed);
    EXPECT_EQ(ev.attackerId, "src");
    EXPECT_EQ(ev.targetId, "target");
    EXPECT_FLOAT_EQ(target.m_health.getHealth(), 30.0f);
}

TEST(CombatSystemTest, ApplyDamageFiresOnDamageOnce) {
    CombatSystem combat;
    TestEntity target(glm::vec3(0, 0, 0));
    int calls = 0;
    combat.setOnDamage([&](const DamageEvent&) { calls++; });

    combat.applyDamage(&target, "target", 5.0f, "src");
    EXPECT_EQ(calls, 1);
}

TEST(CombatSystemTest, ApplyDamageKillSetsKilledFlag) {
    CombatSystem combat;
    TestEntity target(glm::vec3(0, 0, 0), 10.0f);

    auto ev = combat.applyDamage(&target, "target", 25.0f, "src");
    EXPECT_TRUE(ev.killed);
    EXPECT_FALSE(target.m_health.isAlive());
}

TEST(CombatSystemTest, ApplyDamageOnDeadOrNullIsNoOp) {
    CombatSystem combat;
    int calls = 0;
    combat.setOnDamage([&](const DamageEvent&) { calls++; });

    // Null target: safe no-op, no dispatch.
    auto evNull = combat.applyDamage(nullptr, "none", 5.0f, "src");
    EXPECT_FLOAT_EQ(evNull.actualDamage, 0.0f);

    // Already-dead target: no dispatch, no further damage.
    TestEntity dead(glm::vec3(0, 0, 0), 10.0f);
    dead.m_health.kill();
    auto evDead = combat.applyDamage(&dead, "dead", 5.0f, "src");
    EXPECT_FLOAT_EQ(evDead.actualDamage, 0.0f);
    EXPECT_FALSE(evDead.killed);

    EXPECT_EQ(calls, 0);
}

TEST(CombatSystemTest, ApplyDamageKnockbackOptional) {
    CombatSystem combat;
    TestEntity target(glm::vec3(0, 0, 0));

    // No knockback by default → velocity untouched.
    combat.applyDamage(&target, "target", 5.0f, "src");
    EXPECT_FLOAT_EQ(glm::length(target.lastKnockback), 0.0f);

    // Explicit knockback is applied.
    combat.update(1.0f);  // clear i-frames so the second hit lands
    combat.applyDamage(&target, "target", 5.0f, "src",
                       DamageType::Physical, glm::vec3(0, 0, 3.0f));
    EXPECT_NE(glm::length(target.lastKnockback), 0.0f);
}

// Shared health store: two "entities" pointing at one HealthComponent (the
// player + HUD/respawn unification) take damage once, not twice.
TEST(CombatSystemTest, SharedHealthStoreIsSingleSource) {
    CombatSystem combat;
    HealthComponent shared(100.0f);

    // A second entity whose health is the SAME object (mirrors the player
    // character sharing Application::playerHealth via setHealthComponent).
    class SharedEntity : public Scene::Entity {
    public:
        explicit SharedEntity(HealthComponent* h) : m_h(h) {}
        void update(float) override {}
        void render(Graphics::RenderCoordinator*) override {}
        HealthComponent* getHealthComponent() override { return m_h; }
        const HealthComponent* getHealthComponent() const override { return m_h; }
        HealthComponent* m_h;
    } ent(&shared);

    combat.applyDamage(&ent, "player", 30.0f, "goblin");
    EXPECT_FLOAT_EQ(shared.getHealth(), 70.0f);  // decremented exactly once
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
