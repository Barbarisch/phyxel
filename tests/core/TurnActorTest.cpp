#include <gtest/gtest.h>
#include "core/TurnActor.h"
#include "core/ActionEconomy.h"

#include <cmath>

using namespace Phyxel::Core;

namespace {

/// Mock body: moves a fixed world-units/second toward the requested target and
/// reports the distance travelled; an attack stays "active" for a set number of
/// ticks then finishes.
class MockBody : public ITurnActorBody {
public:
    glm::vec3 pos{0, 0, 0};
    float speedUnitsPerSec = 3.0f;   // ~10 ft/s at 0.3048 u/ft
    int   attackTicksLeft  = 0;
    int   attackTicksOnBegin = 3;
    bool  stopped = false;
    glm::vec3 lastAttackTarget{0};

    glm::vec3 position() const override { return pos; }

    float stepToward(const glm::vec3& target, float dt) override {
        stopped = false;
        glm::vec3 to = target - pos; to.y = 0.0f;
        float dist = std::sqrt(to.x * to.x + to.z * to.z);
        if (dist < 1e-5f) return 0.0f;
        float step = std::min(dist, speedUnitsPerSec * dt);
        pos += (to / dist) * step;
        return step;
    }

    void stop() override { stopped = true; }

    void beginAttack(const glm::vec3& targetPos) override {
        lastAttackTarget = targetPos;
        attackTicksLeft  = attackTicksOnBegin;
    }

    bool isAttacking() const override { return attackTicksLeft > 0; }

    // Test helper: advance the attack animation clock.
    void advanceAttackAnim() { if (attackTicksLeft > 0) attackTicksLeft--; }
};

ActionBudget freshBudget(int speedFeet = 30) {
    ActionBudget b; b.reset(speedFeet); return b;
}

} // namespace

// --- Binding / lifecycle ----------------------------------------------------

TEST(TurnActorTest, UnboundIsInert) {
    TurnActor a;
    EXPECT_FALSE(a.isBound());
    EXPECT_FALSE(a.isBusy());
    EXPECT_FALSE(a.requestMove({1, 0, 0}));
    EXPECT_FALSE(a.requestAttack({1, 0, 0}, 5.0f));
    a.tick(0.1f);  // must not crash
}

TEST(TurnActorTest, BeginBindsBodyAndBudget) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget();
    a.begin(&body, &budget);
    EXPECT_TRUE(a.isBound());
    EXPECT_TRUE(a.canMove());
    EXPECT_TRUE(a.canAct());
}

// --- Feet <-> world units ---------------------------------------------------

TEST(TurnActorTest, FeetUnitConversionDefault) {
    TurnActor a;
    EXPECT_NEAR(a.worldUnitsPerFoot(), 0.3048f, 1e-6f);
    EXPECT_NEAR(a.feetToUnits(30.0f), 9.144f, 1e-3f);
    EXPECT_NEAR(a.unitsToFeet(1.524f), 5.0f, 1e-3f);
}

TEST(TurnActorTest, MovementRemainingUnitsTracksBudget) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget(30);
    a.begin(&body, &budget);
    EXPECT_EQ(a.movementRemainingFeet(), 30);
    EXPECT_NEAR(a.movementRemainingUnits(), 9.144f, 1e-3f);
}

// --- Movement debits the budget ---------------------------------------------

TEST(TurnActorTest, MoveReachesTargetWithinBudget) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget(30);  // ~9.14 u
    a.begin(&body, &budget);

    ASSERT_TRUE(a.requestMove({3.0f, 0, 0}));   // ~9.8 ft away — within budget
    EXPECT_TRUE(a.isBusy());

    // Run until idle (or a generous tick cap).
    for (int i = 0; i < 200 && a.isBusy(); ++i) a.tick(0.1f);

    EXPECT_FALSE(a.isBusy());
    EXPECT_NEAR(body.pos.x, 3.0f, TurnActor{}.feetToUnits(1.0f));  // arrived ~target
    EXPECT_TRUE(body.stopped);
    EXPECT_LT(budget.movementRemaining, 30);    // movement was spent
    EXPECT_GE(budget.movementRemaining, 0);
}

TEST(TurnActorTest, MoveStopsWhenBudgetRunsOut) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget(10);  // ~3.05 u only
    a.begin(&body, &budget);

    ASSERT_TRUE(a.requestMove({100.0f, 0, 0}));  // far away — can't reach
    for (int i = 0; i < 500 && a.isBusy(); ++i) a.tick(0.1f);

    EXPECT_FALSE(a.isBusy());
    EXPECT_EQ(budget.movementRemaining, 0);      // spent all movement
    EXPECT_LT(body.pos.x, 4.0f);                 // stopped well short of 100
    EXPECT_TRUE(body.stopped);
}

TEST(TurnActorTest, MoveRejectedWithNoMovementBudget) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget(0);
    a.begin(&body, &budget);
    EXPECT_FALSE(a.canMove());
    EXPECT_FALSE(a.requestMove({5, 0, 0}));
    EXPECT_FALSE(a.isBusy());
}

// --- Attack orchestration ---------------------------------------------------

TEST(TurnActorTest, AttackSpendsActionAndCompletesOnAnimEnd) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget();
    body.pos = {0, 0, 0};
    a.begin(&body, &budget);

    // Target ~1.0 u away, reach 5 ft (~1.52 u) → in reach.
    glm::vec3 tgt{1.0f, 0, 0};
    ASSERT_TRUE(a.requestAttack(tgt, 5.0f));
    EXPECT_FALSE(budget.action);                 // action spent
    EXPECT_EQ(a.activity(), TurnActor::Activity::Attacking);
    EXPECT_EQ(body.lastAttackTarget, tgt);

    // Swing is active for a few ticks, then ends.
    a.tick(0.1f);                                 // sees isAttacking() == true
    EXPECT_TRUE(a.isBusy());
    for (int i = 0; i < body.attackTicksOnBegin; ++i) {
        body.advanceAttackAnim();
        a.tick(0.1f);
    }
    EXPECT_FALSE(a.isBusy());                     // attack finished -> turn can advance
}

TEST(TurnActorTest, AttackRejectedOutOfReach) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget();
    body.pos = {0, 0, 0};
    a.begin(&body, &budget);

    EXPECT_FALSE(a.requestAttack({10.0f, 0, 0}, 5.0f));  // way out of reach
    EXPECT_TRUE(budget.action);                          // action NOT spent
    EXPECT_FALSE(a.isBusy());
}

TEST(TurnActorTest, AttackRejectedWhenActionSpent) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget();
    budget.spendAction();
    a.begin(&body, &budget);
    EXPECT_FALSE(a.requestAttack({1, 0, 0}, 5.0f));
}

TEST(TurnActorTest, AttackTimesOutIfAnimNeverRegisters) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget();
    body.attackTicksOnBegin = 0;   // anim never reports active
    a.begin(&body, &budget);

    ASSERT_TRUE(a.requestAttack({1, 0, 0}, 5.0f));
    // Without the safety timeout this would hang the turn forever.
    for (int i = 0; i < 100 && a.isBusy(); ++i) a.tick(0.1f);
    EXPECT_FALSE(a.isBusy());
}

// --- One intent at a time ---------------------------------------------------

TEST(TurnActorTest, BusyRejectsNewIntent) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget();
    a.begin(&body, &budget);

    ASSERT_TRUE(a.requestMove({5, 0, 0}));
    EXPECT_TRUE(a.isBusy());
    EXPECT_FALSE(a.requestAttack({0.5f, 0, 0}, 5.0f));  // busy moving
    EXPECT_FALSE(a.requestMove({2, 0, 0}));             // busy moving
}

TEST(TurnActorTest, EndStopsAndUnbinds) {
    TurnActor a; MockBody body; ActionBudget budget = freshBudget();
    a.begin(&body, &budget);
    a.requestMove({5, 0, 0});
    a.end();
    EXPECT_FALSE(a.isBound());
    EXPECT_TRUE(body.stopped);
}

// --- Custom scale -----------------------------------------------------------

TEST(TurnActorTest, CustomWorldUnitsPerFoot) {
    TurnActor a;
    a.setWorldUnitsPerFoot(1.0f);   // 1 u == 1 ft
    EXPECT_NEAR(a.feetToUnits(30.0f), 30.0f, 1e-4f);
    a.setWorldUnitsPerFoot(-5.0f);  // invalid ignored
    EXPECT_NEAR(a.worldUnitsPerFoot(), 1.0f, 1e-4f);
}
