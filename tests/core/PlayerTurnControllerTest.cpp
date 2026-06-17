#include <gtest/gtest.h>
#include "core/PlayerTurnController.h"
#include "core/CombatDirector.h"
#include "core/CombatSystem.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "core/SpellDefinition.h"
#include "scene/Entity.h"

#include <cmath>

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

// Minimal scene entity with health + position (mirrors CombatSystemTest).
class TestEntity : public Scene::Entity {
public:
    explicit TestEntity(glm::vec3 pos, float maxHP = 100.0f) : m_health(maxHP) { position = pos; }
    void update(float) override {}
    void render(Graphics::RenderCoordinator*) override {}
    HealthComponent* getHealthComponent() override { return &m_health; }
    const HealthComponent* getHealthComponent() const override { return &m_health; }
    HealthComponent m_health;
};

// Mock TurnActor body: moves a fixed speed toward the target, reports travel;
// an attack stays active a few ticks then finishes.
class MockBody : public ITurnActorBody {
public:
    glm::vec3 pos{0, 0, 0};
    float speed = 3.0f;
    int   attackTicks = 0;
    glm::vec3 position() const override { return pos; }
    float stepToward(const glm::vec3& target, float dt) override {
        glm::vec3 to = target - pos; to.y = 0.0f;
        float d = std::sqrt(to.x * to.x + to.z * to.z);
        if (d < 1e-5f) return 0.0f;
        float step = std::min(d, speed * dt);
        pos += (to / d) * step;
        return step;
    }
    void stop() override {}
    void beginAttack(const glm::vec3&) override { attackTicks = 3; }
    bool isAttacking() const override { return attackTicks > 0; }
    void anim() { if (attackTicks > 0) attackTicks--; }
};

// Build a turn-based director with the player acting first.
void startPlayerTurn(CombatDirector& dir) {
    DiceSystem dice;
    dir.setMode(CombatMode::TurnBased);
    dir.beginEncounter({{"player", true, 0, 30}, {"enemy", false, 0, 30}}, dice);
    dir.initiative().setInitiative("player", 20);
    dir.initiative().setInitiative("enemy", 1);
    dir.initiative().sortOrder();
}

} // namespace

TEST(PlayerTurnControllerTest, InactiveWhenNotPlayerTurn) {
    CombatDirector dir;  // RealTime, no combat
    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setPlayerEntityId("player");
    pc.tick(0.1f);
    EXPECT_FALSE(pc.isPlayerTurnActive());
    EXPECT_FALSE(pc.requestMove({1, 0, 0}));
}

TEST(PlayerTurnControllerTest, BindsOnPlayerTurnAndMoveDebitsBudget) {
    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    TestEntity player({0, 0, 0});
    reg.registerEntity(&player, "player", "animated");

    MockBody body;
    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    pc.setBodyProvider([&](Scene::Entity*) -> ITurnActorBody* { return &body; });
    pc.setPlayerEntityId("player");

    pc.tick(0.05f);                       // binds on the player's turn
    EXPECT_TRUE(pc.isPlayerTurnActive());
    ASSERT_NE(pc.budget(), nullptr);
    EXPECT_EQ(pc.budget()->movementRemaining, 30);

    ASSERT_TRUE(pc.requestMove({3.0f, 0, 0}));
    for (int i = 0; i < 200 && pc.isBusy(); ++i) pc.tick(0.1f);
    EXPECT_FALSE(pc.isBusy());
    EXPECT_LT(pc.budget()->movementRemaining, 30);   // movement was spent
}

TEST(PlayerTurnControllerTest, MoveRejectedWhileBusy) {
    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    TestEntity player({0, 0, 0});
    reg.registerEntity(&player, "player", "animated");
    MockBody body;

    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    pc.setBodyProvider([&](Scene::Entity*) -> ITurnActorBody* { return &body; });
    pc.setPlayerEntityId("player");
    pc.tick(0.05f);

    ASSERT_TRUE(pc.requestMove({10, 0, 0}));
    EXPECT_FALSE(pc.requestMove({2, 0, 0}));   // busy
}

TEST(PlayerTurnControllerTest, AttackResolvesDamageOnCompletion) {
    DiceSystem::setSeed(7);   // deterministic; attackBonus 20 hits any non-fumble
    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    CombatSystem combat;
    TestEntity player({0, 0, 0});
    TestEntity target({1.0f, 0, 0}, 50.0f);   // ~1 u away, within 5 ft reach
    reg.registerEntity(&player, "player", "animated");
    reg.registerEntity(&target, "enemy", "animated");
    MockBody body;

    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    pc.setCombatSystem(&combat);
    pc.setBodyProvider([&](Scene::Entity*) -> ITurnActorBody* { return &body; });
    pc.setPlayerEntityId("player");
    pc.setAttackBonus(20);          // guarantee a hit vs the pseudo-AC
    pc.setDamageDice("1d4+4");      // 5-8 damage (d1 is not a valid die)
    pc.tick(0.05f);

    ASSERT_TRUE(pc.requestAttack("enemy"));
    EXPECT_FALSE(pc.budget()->action);   // action spent
    // Run the swing to completion.
    for (int i = 0; i < 10 && pc.isBusy(); ++i) { body.anim(); pc.tick(0.1f); }
    EXPECT_FALSE(pc.isBusy());
    EXPECT_LT(target.m_health.getHealth(), 50.0f);   // damage landed via the funnel
}

TEST(PlayerTurnControllerTest, AttackRejectedOutOfReach) {
    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    TestEntity player({0, 0, 0});
    TestEntity target({20.0f, 0, 0}, 50.0f);   // far away
    reg.registerEntity(&player, "player", "animated");
    reg.registerEntity(&target, "enemy", "animated");
    MockBody body;

    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    pc.setBodyProvider([&](Scene::Entity*) -> ITurnActorBody* { return &body; });
    pc.setPlayerEntityId("player");
    pc.tick(0.05f);

    EXPECT_FALSE(pc.requestAttack("enemy"));
    EXPECT_TRUE(pc.budget()->action);    // not spent
}

TEST(PlayerTurnControllerTest, TargetingQueries) {
    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    TestEntity player({0, 0, 0});
    TestEntity near({1.0f, 0, 0}, 100.0f);   // full HP -> pseudo-AC 14
    TestEntity far({20.0f, 0, 0}, 100.0f);
    reg.registerEntity(&player, "player", "animated");
    reg.registerEntity(&near, "near", "animated");
    reg.registerEntity(&far, "far", "animated");
    MockBody body;

    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    pc.setBodyProvider([&](Scene::Entity*) -> ITurnActorBody* { return &body; });
    pc.setPlayerEntityId("player");
    pc.setAttackBonus(5);
    pc.tick(0.05f);

    EXPECT_EQ(pc.targetAC("near"), 14);                 // 8 + floor(1.0*6)
    // bonus 5 vs AC 14: need d20>=9 -> faces 9..19 (11) + nat20 = 12/20 = 0.60.
    EXPECT_FLOAT_EQ(pc.hitChanceVs("near"), 0.60f);
    EXPECT_NEAR(pc.distanceTo("near"), 1.0f, 1e-3f);
    EXPECT_NEAR(pc.distanceTo("far"), 20.0f, 1e-3f);
    EXPECT_TRUE(pc.inReachOf("near"));                  // 1 u < 5 ft (1.52 u)
    EXPECT_FALSE(pc.inReachOf("far"));
    EXPECT_FLOAT_EQ(pc.hitChanceVs("missing"), 0.0f);   // unknown target
}

TEST(PlayerTurnControllerTest, CastSpellSpendsActionResolvesAndExecutes) {
    // Register a deterministic auto-hit damage spell.
    SpellDefinition s;
    s.id = "test_zap";
    s.level = 1;
    s.resolutionType = SpellResolutionType::AutoHit;
    s.baseDamage = DiceExpression{0, DieType::D6, 5};   // 0 dice + 5 = always 5
    s.damageType = DamageType::Fire;
    SpellRegistry::instance().registerSpell(s);

    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    CombatSystem combat;
    TestEntity player({0, 0, 0});
    TestEntity target({1.0f, 0, 0}, 50.0f);
    reg.registerEntity(&player, "player", "animated");
    reg.registerEntity(&target, "enemy", "animated");
    MockBody body;

    int releaseCalls = 0;
    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    pc.setCombatSystem(&combat);
    pc.setBodyProvider([&](Scene::Entity*) -> ITurnActorBody* { return &body; });
    pc.setPlayerEntityId("player");
    // Executor fires the release immediately (no animation in tests).
    pc.setCastExecutor([&](const std::string&, const std::string&,
                           const glm::vec3&, std::function<void()> onRelease) {
        releaseCalls++;
        onRelease();
    });
    pc.tick(0.05f);

    ASSERT_TRUE(pc.castSpell("test_zap", "enemy"));
    EXPECT_FALSE(pc.budget()->action);                 // action spent
    EXPECT_EQ(releaseCalls, 1);                         // executor invoked
    EXPECT_FLOAT_EQ(target.m_health.getHealth(), 45.0f);  // 5 auto-hit damage landed
    EXPECT_EQ(pc.selectedTarget(), "enemy");

    EXPECT_FALSE(pc.castSpell("unknown_spell", "enemy"));  // unknown -> rejected
}

TEST(PlayerTurnControllerTest, EndTurnAdvancesAndUnbinds) {
    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    TestEntity player({0, 0, 0});
    reg.registerEntity(&player, "player", "animated");
    MockBody body;

    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    pc.setBodyProvider([&](Scene::Entity*) -> ITurnActorBody* { return &body; });
    pc.setPlayerEntityId("player");
    pc.tick(0.05f);
    ASSERT_TRUE(pc.isPlayerTurnActive());
    ASSERT_EQ(dir.currentEntityId(), "player");

    pc.endTurn();
    EXPECT_FALSE(pc.isPlayerTurnActive());
    EXPECT_EQ(dir.currentEntityId(), "enemy");   // advanced to the next combatant
}
