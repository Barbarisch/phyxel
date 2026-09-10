// CombatAISystem — MORALE → ESCAPE (Ravenmere gap G-31).
//
// RED baseline (docs/evidence/ravenmere/rv_probe_stdout_run2.txt + rv_run2_stalled.log,
// 2026-09-08): a `combat_ai.flee_below_hp` wolf fled every turn for 17+ rounds, 21 u from the
// party, and the encounter could never resolve. These tests pin the fix: a combatant that has
// fled kEscapeFleeTurns consecutive turns AND is ≥ kEscapeDistanceFeet from its foe leaves the
// encounter (removed after its turn), which auto-ends the fight when it was the last hostile.
#include <gtest/gtest.h>
#include "core/CombatAISystem.h"
#include "core/CombatDirector.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "scene/Entity.h"

#include <cmath>

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

class TestEntity : public Scene::Entity {
public:
    explicit TestEntity(glm::vec3 pos, float maxHP = 100.0f) : m_health(maxHP) { setPosition(pos); }
    void update(float) override {}
    void render(Graphics::RenderCoordinator*) override {}
    HealthComponent* getHealthComponent() override { return &m_health; }
    const HealthComponent* getHealthComponent() const override { return &m_health; }
    HealthComponent m_health;
};

// Body that actually travels (so a flee move covers ground) and mirrors its position
// back into the entity, the way CharacterTurnBody does for a live character.
class MovingBody : public ITurnActorBody {
public:
    explicit MovingBody(TestEntity& e) : m_e(e) {}
    float speed = 6.0f;
    glm::vec3 position() const override { return m_e.getPosition(); }
    float stepToward(const glm::vec3& target, float dt) override {
        glm::vec3 to = target - m_e.getPosition(); to.y = 0.0f;
        const float d = std::sqrt(to.x * to.x + to.z * to.z);
        if (d < 1e-5f) return 0.0f;
        const float step = std::min(d, speed * dt);
        m_e.setPosition(m_e.getPosition() + (to / d) * step);
        return step;
    }
    void stop() override {}
    void beginAttack(const glm::vec3&) override { attackTicks = 3; }
    bool isAttacking() const override { return attackTicks > 0; }
    void anim() { if (attackTicks > 0) attackTicks--; }
    int attackTicks = 0;
private:
    TestEntity& m_e;
};

#define ASSERT_OR_RETURN(cond, val) do { if (!(cond)) return (val); } while (0)

struct Rig {
    CombatDirector dir;
    EntityRegistry reg;
    TestEntity player{{0, 0, 0}, 100.0f};
    TestEntity wolf;
    MovingBody wolfBody{wolf};
    CombatTactics wolfTactics;
    CombatAISystem ai;

    explicit Rig(glm::vec3 wolfPos, float wolfMaxHp = 20.0f, float wolfHp = 5.0f)
        : wolf(wolfPos, wolfMaxHp) {
        wolf.m_health.setHealth(wolfHp);   // 25 % — under the 50 % morale threshold
        reg.registerEntity(&player, "player", "animated");
        reg.registerEntity(&wolf, "wolf", "npc");
        wolfTactics.fleeBelowHpFrac = 0.5f;

        DiceSystem dice;
        dir.setMode(CombatMode::TurnBased);
        dir.beginEncounter({{"player", true, 0, 30}, {"wolf", false, 0, 30}}, dice);
        dir.initiative().setInitiative("player", 20);
        dir.initiative().setInitiative("wolf", 1);
        dir.initiative().sortOrder();

        ai.setCombatDirector(&dir);
        ai.setEntityRegistry(&reg);
        ai.setPlayerEntityId("player");
        ai.setThinkDelay(0.0f);
        ai.setBodyProvider([this](Scene::Entity* e) -> ITurnActorBody* {
            return e == &wolf ? &wolfBody : nullptr;
        });
        ai.setTacticsProvider([this](const std::string& id) -> const CombatTactics* {
            return id == "wolf" ? &wolfTactics : nullptr;
        });
    }

    // The human ends their turn; the AI then runs the wolf's whole turn. Returns the
    // number of frames it took (bounded), or -1 if the wolf's turn never handed back.
    int playWolfTurn() {
        ASSERT_OR_RETURN(dir.currentEntityId() == "player", -1);
        dir.advanceTurn();                    // -> wolf
        for (int i = 0; i < 2000; ++i) {
            ai.tick(0.05f);
            wolfBody.anim();
            if (!dir.inCombat() || dir.currentEntityId() == "player") return i;
        }
        return -1;
    }
};

float gapXZ(const TestEntity& a, const TestEntity& b) {
    const glm::vec3 d = a.getPosition() - b.getPosition();
    return std::sqrt(d.x * d.x + d.z * d.z);
}

} // namespace

// A wolf that starts far away still does NOT escape on its FIRST flee turn — the
// streak requirement is what separates "broke and ran" from "gone".
TEST(CombatAISystemEscapeTest, FirstFleeTurnDoesNotEscapeEvenWhenFar) {
    Rig r({30.0f, 0.0f, 0.0f});           // 30 u ≈ 98 ft: already beyond the escape distance
    ASSERT_GE(r.playWolfTurn(), 0);
    EXPECT_TRUE(r.dir.inCombat());
    EXPECT_EQ(r.dir.enemySideCount(), 1);
    EXPECT_GT(gapXZ(r.wolf, r.player), 30.0f) << "it fled (moved away) rather than fighting";
}

// The RED scenario: a broken wolf that keeps fleeing. After kEscapeFleeTurns flee turns it is
// far from every foe → it escapes on the next turn, the encounter ends, and it is still alive
// (it ran, it was not killed — no kill XP is owed).
TEST(CombatAISystemEscapeTest, RepeatedFleeingFarAwayEndsTheEncounter) {
    Rig r({12.0f, 0.0f, 0.0f});           // 12 u ≈ 39 ft: within reach-ish at the start
    const float hp0 = r.wolf.m_health.getHealth();
    int turns = 0;
    for (; turns < 6 && r.dir.inCombat(); ++turns) {
        ASSERT_GE(r.playWolfTurn(), 0) << "wolf turn " << turns << " never handed back";
    }
    EXPECT_FALSE(r.dir.inCombat()) << "the encounter must resolve once the last hostile has escaped";
    EXPECT_LE(turns, 4) << "2 flee turns + the escape turn";
    EXPECT_GE(turns, 3) << "escape needs the flee streak first";
    EXPECT_EQ(r.wolf.m_health.getHealth(), hp0) << "escaping is not dying";
    EXPECT_GE(gapXZ(r.wolf, r.player), 45.0f * 0.3048f);
}

// CONTROL: the same wolf with the morale threshold below its HP never flees and never escapes.
// (It walks in and swings; the encounter stays live because nobody dies in one turn here.)
TEST(CombatAISystemEscapeTest, NoMoraleBreakNoEscape) {
    Rig r({12.0f, 0.0f, 0.0f}, 20.0f, 15.0f);   // 75 % hp > 50 % threshold
    for (int t = 0; t < 3 && r.dir.inCombat(); ++t) ASSERT_GE(r.playWolfTurn(), 0);
    EXPECT_TRUE(r.dir.inCombat());
    EXPECT_EQ(r.dir.enemySideCount(), 1);
}

// STALL GUARD (G-42): a body that cannot make progress (blocked, or standing inside an ally)
// must not freeze the encounter on its turn. Ravenmere run 6 froze for minutes on Wren's turn.
TEST(CombatAISystemEscapeTest, StalledBodyStillEndsTurn) {
    Rig r({6.0f, 0.0f, 0.0f}, 20.0f, 20.0f);   // full hp -> no morale; 6 u away (> 5 ft reach)
    r.wolfBody.speed = 0.0f;                      // cannot move at all
    const int frames = r.playWolfTurn();
    EXPECT_GE(frames, 0) << "the wolf's turn never handed back to the player";
    EXPECT_TRUE(r.dir.inCombat());                // nobody died, nobody escaped
    EXPECT_EQ(r.dir.currentEntityId(), "player");
}
