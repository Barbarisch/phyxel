// CombatSceneExitTest.cpp — Ravenmere G-139: a combat move must not carry the player out of the
// fight through a scene-exit region.
//
// WHAT HAPPENED. Probe L4 2026-09-17 (`rv_combat_move_move2.log`): a ground click near the farm's
// west edge routed the player through `back_to_town`'s trigger region during the wolf fight, the
// region fired on entry, and the scene changed with the encounter still running.
//
// TWO INDEPENDENT GUARDS, because either alone leaves a hole:
//   1. SceneManager::setTransitionGuard — the engine-side chokepoint. EVERY path into a scene
//      change goes through transitionTo (region triggers, menu buttons, the test API), so a veto
//      there closes the hole no matter what walked the player in. This is the one that matters for
//      correctness; without it a wander, a knockback or a scripted move could still do it.
//   2. PlayerTurnController::setDestinationFilter — refuses to spend the player's movement budget
//      walking into a region the host has forbidden. Quality, not correctness: with guard 1 in
//      place the transition is refused anyway, but the player would have wasted their turn walking
//      to the map edge.
// The filter is consulted for every WAYPOINT, not just the destination, because a region trigger
// fires when the player ENTERS it — passing through is enough.

#include <gtest/gtest.h>

#include "core/CombatDirector.h"
#include "core/DiceSystem.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "core/PlayerTurnController.h"
#include "core/SceneManager.h"
#include "scene/Entity.h"

#include <cmath>
#include <string>
#include <vector>

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

class ExitTestEntity : public Scene::Entity {
public:
    explicit ExitTestEntity(glm::vec3 pos) : m_health(100.0f) { position = pos; }
    void update(float) override {}
    void render(Graphics::RenderCoordinator*) override {}
    HealthComponent* getHealthComponent() override { return &m_health; }
    const HealthComponent* getHealthComponent() const override { return &m_health; }
    HealthComponent m_health;
};

/// Same shape as PlayerTurnControllerTest's MockBody: walks toward the target and reports travel.
class ExitTestBody : public ITurnActorBody {
public:
    glm::vec3 pos{0, 0, 0};
    float speed = 3.0f;
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
    void beginAttack(const glm::vec3&) override {}
    bool isAttacking() const override { return false; }
};

SceneManifest twoScenes() {
    SceneManifest m;
    m.startScene = "farm";
    SceneDefinition farm; farm.id = "farm"; farm.worldDatabase = "farm.db";
    SceneDefinition town; town.id = "town"; town.worldDatabase = "town.db";
    m.scenes.push_back(farm);
    m.scenes.push_back(town);
    return m;
}

/// Turn-based encounter with the player acting first (mirrors PlayerTurnControllerTest).
void startPlayerTurn(CombatDirector& dir) {
    DiceSystem dice;
    dir.setMode(CombatMode::TurnBased);
    dir.beginEncounter({{"player", true, 0, 30}, {"wolf", false, 0, 30}}, dice);
    dir.initiative().setInitiative("player", 20);
    dir.initiative().setInitiative("wolf", 1);
    dir.initiative().sortOrder();
}

/// A controller bound to a one-player encounter, ready to take a move.
struct Rig {
    CombatDirector dir;
    EntityRegistry reg;
    ExitTestEntity player{{0, 0, 0}};
    ExitTestBody body;
    PlayerTurnController pc;

    Rig() {
        startPlayerTurn(dir);
        reg.registerEntity(&player, "player", "animated");
        pc.setCombatDirector(&dir);
        pc.setEntityRegistry(&reg);
        pc.setBodyProvider([this](Scene::Entity*) -> ITurnActorBody* { return &body; });
        pc.setPlayerEntityId("player");
        pc.tick(0.05f);
    }
};

}  // namespace

// ---------------------------------------------------------------------------------------------
// GUARD 1 — the engine refuses the transition itself.
// ---------------------------------------------------------------------------------------------

TEST(CombatSceneExit, ATransitionGuardCanRefuseAndTheSceneDoesNotChange) {
    // A bare SceneManager never reaches a LOADED scene: the Loading step calls executeLoad()
    // through host callbacks a unit test has none of (SceneSystemTest records the same limit).
    // So the observable here is the refusal itself and whether the state machine started, which
    // is exactly what the guard controls.
    SceneManager sm;
    sm.loadManifest(twoScenes());
    ASSERT_TRUE(sm.getActiveSceneId().empty());

    bool inCombat = true;
    std::string sawScene;
    sm.setTransitionGuard([&](const std::string& scene, std::string& reason) {
        sawScene = scene;
        if (inCombat) { reason = "an encounter is running"; return false; }
        return true;
    });

    EXPECT_FALSE(sm.transitionTo("town")) << "the guard refused, so the call must report failure";
    EXPECT_EQ(sawScene, "town") << "the guard is told which scene was asked for";
    EXPECT_FALSE(sm.isTransitioning()) << "a refused transition must not start the state machine";
    EXPECT_TRUE(sm.getActiveSceneId().empty()) << "and must not change where we are";

    // The guard GATES, it does not disable: the same call proceeds once the host stops refusing.
    inCombat = false;
    EXPECT_TRUE(sm.transitionTo("town"));
    EXPECT_TRUE(sm.isTransitioning()) << "allowed: the transition is now under way";
}

TEST(CombatSceneExit, AGuardThatAllowsIsIndistinguishableFromNoGuard) {
    SceneManager a, b;
    a.loadManifest(twoScenes());
    b.loadManifest(twoScenes());
    EXPECT_FALSE(a.hasTransitionGuard());
    b.setTransitionGuard([](const std::string&, std::string&) { return true; });
    EXPECT_TRUE(b.hasTransitionGuard());

    EXPECT_EQ(a.transitionTo("town"), b.transitionTo("town"));
    EXPECT_EQ(a.isTransitioning(), b.isTransitioning());
    // An unknown scene is still an error, guard or not: the guard adds a veto, it does not
    // replace the existing checks.
    SceneManager c; c.loadManifest(twoScenes());
    c.setTransitionGuard([](const std::string&, std::string&) { return true; });
    EXPECT_FALSE(c.transitionTo("nowhere"));
}

// ---------------------------------------------------------------------------------------------
// GUARD 2 — the combat move will not spend the turn walking into a forbidden region.
// ---------------------------------------------------------------------------------------------

TEST(CombatSceneExit, ACombatMoveRefusesADestinationInsideAForbiddenRegion) {
    Rig r;
    // The exit region: everything with x <= -8 (the farm's west edge).
    r.pc.setDestinationFilter([](const glm::vec3& p) { return p.x > -8.0f; });
    r.pc.setPathProvider([](const glm::vec3&, const glm::vec3& to) {
        return std::vector<glm::vec3>{to};
    });

    EXPECT_FALSE(r.pc.requestMove({-10.0f, 0, 0})) << "a destination inside the exit region is refused";
    EXPECT_FALSE(r.pc.isBusy()) << "and the player's turn is not spent on it";
    EXPECT_TRUE(r.pc.requestMove({4.0f, 0, 0})) << "a destination outside it still works";
}

TEST(CombatSceneExit, ACombatMoveRefusesARouteThatMerelyPassesThroughOne) {
    Rig r;
    r.pc.setDestinationFilter([](const glm::vec3& p) { return p.x > -8.0f; });
    // A legal destination, but the route rounds a wall by going THROUGH the exit region.
    // A region trigger fires on entry, so this must be refused too.
    r.pc.setPathProvider([](const glm::vec3&, const glm::vec3& to) {
        return std::vector<glm::vec3>{{-12.0f, 0.0f, 0.0f}, to};
    });

    EXPECT_FALSE(r.pc.requestMove({2.0f, 0, 6.0f})) << "the destination is legal but the route is not";
    EXPECT_FALSE(r.pc.isBusy());
}

TEST(CombatSceneExit, WithNoFilterCombatMovesBehaveExactlyAsBefore) {
    Rig r;
    r.pc.setPathProvider([](const glm::vec3&, const glm::vec3& to) {
        return std::vector<glm::vec3>{to};
    });
    EXPECT_TRUE(r.pc.requestMove({-10.0f, 0, 0})) << "no filter set must not change existing behaviour";
}
