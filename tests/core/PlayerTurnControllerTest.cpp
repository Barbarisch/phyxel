#include <gtest/gtest.h>
#include "core/PlayerTurnController.h"
#include "core/CombatDirector.h"
#include "core/CombatSystem.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "core/SpellDefinition.h"
#include "scene/Entity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "physics/PhysicsWorld.h"
#include "graphics/Camera.h"

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

// G-102: out of reach AND beyond this turn's movement (30 ft = ~9.1 u): refused, budget intact.
TEST(PlayerTurnControllerTest, AttackRejectedOutOfReach) {
    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    TestEntity player({0, 0, 0});
    TestEntity target({20.0f, 0, 0}, 50.0f);   // far away: 20 u > 9.1 u of movement
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

TEST(PlayerTurnControllerTest, AreaSpellHitsMultipleTargetsInRadius) {
    // Auto-hit area spell so the test is deterministic (no save rolls).
    SpellDefinition s;
    s.id = "test_blast";
    s.level = 1;
    s.resolutionType = SpellResolutionType::AutoHit;
    s.areaShape = AreaShape::Sphere;
    s.areaSizeFeet = 20.0f;             // ~6.1 u radius
    s.baseDamage = DiceExpression{0, DieType::D6, 4};   // always 4
    s.damageType = DamageType::Fire;
    SpellRegistry::instance().registerSpell(s);

    CombatDirector dir;
    {
        DiceSystem dice;
        dir.setMode(CombatMode::TurnBased);
        dir.beginEncounter({{"player", true, 0, 30},
                            {"g1", false, 0, 30},
                            {"g2", false, 0, 30},
                            {"g3", false, 0, 30}}, dice);
        dir.initiative().setInitiative("player", 20);
        dir.initiative().sortOrder();
    }
    EntityRegistry reg;
    CombatSystem combat;
    TestEntity player({0, 0, 0});
    TestEntity g1({10, 0, 0}, 50.0f);   // near the centre (g2)
    TestEntity g2({12, 0, 0}, 50.0f);   // cast centre
    TestEntity g3({40, 0, 0}, 50.0f);   // far away — outside the radius
    reg.registerEntity(&player, "player", "animated");
    reg.registerEntity(&g1, "g1", "npc");
    reg.registerEntity(&g2, "g2", "npc");
    reg.registerEntity(&g3, "g3", "npc");
    MockBody body;

    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    pc.setCombatSystem(&combat);
    pc.setBodyProvider([&](Scene::Entity*) -> ITurnActorBody* { return &body; });
    pc.setPlayerEntityId("player");
    pc.setCastExecutor([](const std::string&, const std::string&,
                          const glm::vec3&, std::function<void()> r) { r(); });
    pc.tick(0.05f);

    // Preview: centred on g2, g1+g2 are within 20 ft (~6.1 u), g3 is not.
    auto preview = pc.aoeTargetsAt("test_blast", glm::vec3(12, 0, 0));
    EXPECT_EQ(preview.size(), 2u);

    ASSERT_TRUE(pc.castSpell("test_blast", "g2"));
    EXPECT_FLOAT_EQ(g1.m_health.getHealth(), 46.0f);   // 4 dmg
    EXPECT_FLOAT_EQ(g2.m_health.getHealth(), 46.0f);   // 4 dmg
    EXPECT_FLOAT_EQ(g3.m_health.getHealth(), 50.0f);   // untouched (out of area)
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


// G-102 (BG3): a foe out of reach but within this turn's movement is APPROACHED, then hit.
// RED before: requestAttack returned false and the action was never spent (the manual
// test of 2026-09-11 pressed Attack on a foe 12 u away and nothing happened).
TEST(PlayerTurnControllerTest, AttackOutOfReachApproachesThenSwings) {
    DiceSystem::setSeed(7);
    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    CombatSystem combat;
    TestEntity player({0, 0, 0});
    TestEntity target({5.0f, 0, 0}, 50.0f);   // 5 u away: out of 5 ft reach, inside 30 ft of movement
    reg.registerEntity(&player, "player", "animated");
    reg.registerEntity(&target, "enemy", "animated");
    MockBody body;
    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    pc.setCombatSystem(&combat);
    pc.setBodyProvider([&](Scene::Entity*) -> ITurnActorBody* { return &body; });
    pc.setPlayerEntityId("player");
    pc.setAttackBonus(20);
    pc.setDamageDice("1d4+4");
    pc.tick(0.05f);
    EXPECT_FALSE(pc.inReachOf("enemy"));
    ASSERT_TRUE(pc.requestAttack("enemy")) << "must approach, not refuse";
    EXPECT_EQ(pc.approachTarget(), "enemy");
    EXPECT_TRUE(pc.budget()->action) << "the action is spent on the swing, not the walk";
    // walk + swing to completion
    for (int i = 0; i < 400 && (pc.isBusy() || !pc.approachTarget().empty()); ++i) { body.anim(); pc.tick(0.05f); }
    EXPECT_TRUE(pc.approachTarget().empty());
    EXPECT_TRUE(pc.inReachOf("enemy"));
    EXPECT_FALSE(pc.budget()->action) << "arrived and swung";
    EXPECT_LT(target.m_health.getHealth(), 50.0f) << "the swing landed";
    EXPECT_LT(pc.budget()->movementRemaining, 30) << "the walk cost movement";
}


// G-121 (manual review 2026-09-16: "the mouse over for attacking characters isn't right -
// I have mouse over a section of the screen that isn't actually over the character").
// The pick used a 32 px circle around the CHEST point: legs and head missed, and empty
// screen near the chest hit. The hot zone is now the character's screen box (feet to
// head, capsule width). RED before: a click on the feet resolved to Move, not Attack.
TEST(PlayerTurnControllerTest, PickHitsTheWholeCharacterNotAChestCircle) {
    auto physics = std::make_unique<Physics::PhysicsWorld>();
    Scene::AnimatedVoxelCharacter enemy(physics.get(), glm::vec3(0.0f, 20.0f, 0.0f));
    ASSERT_TRUE(enemy.loadModel("resources/animated_characters/humanoid.anim"));
    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    TestEntity player({-6.0f, 20.0f, 0.0f});
    reg.registerEntity(&player, "player", "animated");
    reg.registerEntity(&enemy, "enemy", "animated");
    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    // Camera 7 m in front of the enemy at chest height, looking at it (+x toward the enemy).
    Graphics::Camera cam({-7.0f, 21.0f, 0.0f});
    cam.setYaw(0.0f); cam.setPitch(-5.0f);
    const glm::vec2 vp{1280.0f, 720.0f};
    glm::vec2 feetPx, chestPx, headPx;
    ASSERT_TRUE(pc.screenOf(cam, "enemy", vp, feetPx, 0.05f));
    ASSERT_TRUE(pc.screenOf(cam, "enemy", vp, chestPx, 0.9f));
    ASSERT_TRUE(pc.screenOf(cam, "enemy", vp, headPx, enemy.getControllerHalfHeight() * 2.0f - 0.05f));
    EXPECT_GT(std::fabs(feetPx.y - chestPx.y), 40.0f) << "sanity: the feet are well outside the old 32 px chest circle";
    EXPECT_EQ(pc.resolvePick(cam, feetPx, vp, 20.0f).kind, PlayerTurnController::PickResult::Kind::Attack) << "click on the feet";
    EXPECT_EQ(pc.resolvePick(cam, headPx, vp, 20.0f).kind, PlayerTurnController::PickResult::Kind::Attack) << "click on the head";
    EXPECT_EQ(pc.resolvePick(cam, chestPx, vp, 20.0f).kind, PlayerTurnController::PickResult::Kind::Attack);
    // Hovering the NAMEPLATE above the head selects too (the plate reads as the character).
    glm::vec2 platePx;
    ASSERT_TRUE(pc.screenOf(cam, "enemy", vp, platePx, 2.15f));
    EXPECT_EQ(pc.resolvePick(cam, {platePx.x, platePx.y - 40.0f}, vp, 20.0f).kind, PlayerTurnController::PickResult::Kind::Attack) << "on the plate";
    glm::vec2 mn, mx;
    ASSERT_TRUE(PlayerTurnController::screenBoxOf(cam, &enemy, vp, mn, mx));
    const glm::vec2 beside(mx.x + 40.0f, chestPx.y);   // 40 px outside the box, level with the chest
    EXPECT_NE(pc.resolvePick(cam, beside, vp, 20.0f).kind, PlayerTurnController::PickResult::Kind::Attack)
        << "empty screen beside the character is not the character";
}

// --- G-136: a combat move follows the host's NavGraph route, and the ground pick
// lands on the VOXEL SURFACE under the cursor, not on a plane at the player's feet.
TEST(PlayerTurnControllerTest, MoveFollowsThePathProvidersRoute) {
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
    // The "graph" routes every move through a corner at (3,0,0) — a wall between the
    // start and the goal at (3,0,3).
    glm::vec3 askedFrom{-1}, askedTo{-1};
    pc.setPathProvider([&](const glm::vec3& from, const glm::vec3& to) {
        askedFrom = from; askedTo = to;
        return std::vector<glm::vec3>{{3.0f, 0, 0}, to};
    });
    pc.tick(0.05f);
    ASSERT_TRUE(pc.requestMove({3.0f, 0, 3.0f}));
    EXPECT_NEAR(askedTo.z, 3.0f, 1e-4f);
    bool passedCorner = false;
    for (int i = 0; i < 400 && pc.isBusy(); ++i) {
        pc.tick(0.05f);
        // within the actor's arrival radius of the corner (a straight diagonal never comes closer than 2.1 u)
        if (std::abs(body.pos.x - 3.0f) < 0.35f && body.pos.z < 0.35f) passedCorner = true;
    }
    EXPECT_TRUE(passedCorner) << "the move ignored the route and cut the corner";
    EXPECT_NEAR(body.pos.x, 3.0f, 0.35f);
    EXPECT_NEAR(body.pos.z, 3.0f, 0.35f);
}

TEST(PlayerTurnControllerTest, GroundPickLandsOnTheVoxelSurfaceNotThePlayersPlane) {
    CombatDirector dir; startPlayerTurn(dir);
    EntityRegistry reg;
    TestEntity player({0, 0, 0});
    reg.registerEntity(&player, "player", "animated");
    PlayerTurnController pc;
    pc.setCombatDirector(&dir);
    pc.setEntityRegistry(&reg);
    pc.setPlayerEntityId("player");
    // Terrain: ground at y<=0 everywhere, plus a 3-cube-high ledge for x >= 5.
    pc.setSolidProvider([](const glm::ivec3& c) { return c.y <= 0 || (c.x >= 5 && c.y <= 3); });
    // Camera high above (8,30,0) looking straight down: the screen centre projects to x=8, z=0.
    Graphics::Camera cam({8.0f, 30.0f, 0.0f});
    cam.setYaw(0.0f); cam.setPitch(-89.9f);
    const glm::vec2 vp{1280.0f, 720.0f};
    const auto r = pc.resolvePick(cam, {640.0f, 360.0f}, vp, /*groundY=*/0.0f);
    ASSERT_EQ(r.kind, PlayerTurnController::PickResult::Kind::Move);
    EXPECT_NEAR(r.point.x, 8.0f, 0.6f);
    EXPECT_NEAR(r.point.z, 0.0f, 0.6f);
    EXPECT_NEAR(r.point.y, 4.0f, 0.01f) << "the ledge top (cube y=3 -> standing y=4), not the y=0 plane";
}
