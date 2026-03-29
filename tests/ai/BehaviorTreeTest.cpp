#include <gtest/gtest.h>
#include "ai/Blackboard.h"
#include "ai/PerceptionSystem.h"
#include "ai/BehaviorTree.h"
#include "ai/UtilityAI.h"
#include "ai/ActionSystem.h"

using namespace Phyxel;
using namespace Phyxel::AI;

// ============================================================================
// Blackboard Tests
// ============================================================================

TEST(BlackboardTest, SetAndGetBool) {
    Blackboard bb;
    bb.setBool("flag", true);
    EXPECT_TRUE(bb.getBool("flag"));
    EXPECT_FALSE(bb.getBool("missing"));
}

TEST(BlackboardTest, SetAndGetInt) {
    Blackboard bb;
    bb.setInt("count", 42);
    EXPECT_EQ(bb.getInt("count"), 42);
    EXPECT_EQ(bb.getInt("missing", -1), -1);
}

TEST(BlackboardTest, SetAndGetFloat) {
    Blackboard bb;
    bb.setFloat("speed", 3.14f);
    EXPECT_FLOAT_EQ(bb.getFloat("speed"), 3.14f);
    EXPECT_FLOAT_EQ(bb.getFloat("missing", 1.0f), 1.0f);
}

TEST(BlackboardTest, SetAndGetString) {
    Blackboard bb;
    bb.setString("name", "Guard");
    EXPECT_EQ(bb.getString("name"), "Guard");
    EXPECT_EQ(bb.getString("missing", "none"), "none");
}

TEST(BlackboardTest, SetAndGetVec3) {
    Blackboard bb;
    glm::vec3 pos(1.0f, 2.0f, 3.0f);
    bb.setVec3("pos", pos);
    auto result = bb.getVec3("pos");
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

TEST(BlackboardTest, OverloadedSet) {
    Blackboard bb;
    bb.set("b", true);
    bb.set("i", 10);
    bb.set("f", 2.5f);
    bb.set("s", std::string("hello"));
    bb.set("v", glm::vec3(1, 2, 3));
    EXPECT_TRUE(bb.getBool("b"));
    EXPECT_EQ(bb.getInt("i"), 10);
    EXPECT_FLOAT_EQ(bb.getFloat("f"), 2.5f);
    EXPECT_EQ(bb.getString("s"), "hello");
    EXPECT_FLOAT_EQ(bb.getVec3("v").x, 1.0f);
}

TEST(BlackboardTest, HasAndErase) {
    Blackboard bb;
    bb.setInt("x", 5);
    EXPECT_TRUE(bb.has("x"));
    EXPECT_FALSE(bb.has("y"));
    bb.erase("x");
    EXPECT_FALSE(bb.has("x"));
}

TEST(BlackboardTest, ClearAndSize) {
    Blackboard bb;
    bb.setInt("a", 1);
    bb.setInt("b", 2);
    EXPECT_EQ(bb.size(), 2u);
    bb.clear();
    EXPECT_EQ(bb.size(), 0u);
}

TEST(BlackboardTest, WrongTypeReturnsDefault) {
    Blackboard bb;
    bb.setInt("x", 42);
    // Asking for bool when it's an int should return default
    EXPECT_FALSE(bb.getBool("x", false));
    EXPECT_EQ(bb.getString("x", "fallback"), "fallback");
}

TEST(BlackboardTest, OverwriteValue) {
    Blackboard bb;
    bb.setInt("x", 1);
    bb.setInt("x", 2);
    EXPECT_EQ(bb.getInt("x"), 2);
}

TEST(BlackboardTest, ToJson) {
    Blackboard bb;
    bb.setBool("alive", true);
    bb.setInt("hp", 100);
    bb.setString("name", "Bob");
    bb.setVec3("pos", glm::vec3(1, 2, 3));

    auto j = bb.toJson();
    EXPECT_TRUE(j["alive"].get<bool>());
    EXPECT_EQ(j["hp"].get<int>(), 100);
    EXPECT_EQ(j["name"].get<std::string>(), "Bob");
    EXPECT_FLOAT_EQ(j["pos"]["x"].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(j["pos"]["y"].get<float>(), 2.0f);
}

// ============================================================================
// PerceptionSystem Tests
// ============================================================================

TEST(PerceptionComponentTest, DefaultConfiguration) {
    PerceptionComponent pc;
    EXPECT_FLOAT_EQ(pc.visionRange, 15.0f);
    EXPECT_FLOAT_EQ(pc.visionAngle, 120.0f);
    EXPECT_FLOAT_EQ(pc.hearingRange, 20.0f);
    EXPECT_FLOAT_EQ(pc.memoryDuration, 10.0f);
}

TEST(PerceptionComponentTest, CustomConfiguration) {
    PerceptionComponent pc;
    pc.visionRange = 30.0f;
    pc.visionAngle = 90.0f;
    pc.hearingRange = 10.0f;
    pc.memoryDuration = 5.0f;
    EXPECT_FLOAT_EQ(pc.visionRange, 30.0f);
    EXPECT_FLOAT_EQ(pc.visionAngle, 90.0f);
    EXPECT_FLOAT_EQ(pc.hearingRange, 10.0f);
    EXPECT_FLOAT_EQ(pc.memoryDuration, 5.0f);
}

TEST(PerceptionComponentTest, EmptyInitially) {
    PerceptionComponent pc;
    EXPECT_TRUE(pc.getVisibleEntities().empty());
    EXPECT_TRUE(pc.getHeardEntities().empty());
    EXPECT_TRUE(pc.getThreats(0.0f).empty());
    EXPECT_FALSE(pc.isAwareOf("test"));
}

TEST(PerceptionComponentTest, SetThreatLevel) {
    PerceptionComponent pc;
    pc.setThreatLevel("enemy1", 0.8f);
    // Threat should be stored but awareness depends on perception update
    // This just tests the setter doesn't crash
}

TEST(PerceptionComponentTest, ToJsonEmpty) {
    PerceptionComponent pc;
    auto j = pc.toJson();
    EXPECT_TRUE(j.contains("visionRange"));
    EXPECT_TRUE(j.contains("knownEntities"));
    EXPECT_TRUE(j.contains("count"));
    EXPECT_TRUE(j["knownEntities"].is_array());
    EXPECT_EQ(j["count"].get<size_t>(), 0u);
}

TEST(SenseResultTest, ToJson) {
    SenseResult r;
    r.entityId = "npc_guard";
    r.position = glm::vec3(10, 0, 5);
    r.distance = 11.18f;
    r.angle = 30.0f;
    r.isVisible = true;
    r.lastSeen = 0.0f;
    r.threatLevel = 0.5f;
    auto j = r.toJson();
    EXPECT_EQ(j["entityId"], "npc_guard");
    EXPECT_TRUE(j["isVisible"].get<bool>());
    EXPECT_FLOAT_EQ(j["threatLevel"].get<float>(), 0.5f);
}

// ============================================================================
// BehaviorTree Tests
// ============================================================================

// Simple leaf that always succeeds
class AlwaysSucceed : public BTNode {
public:
    BTStatus tick(float, ActionContext&) override { return BTStatus::Success; }
    std::string getTypeName() const override { return "AlwaysSucceed"; }
};

// Simple leaf that always fails
class AlwaysFail : public BTNode {
public:
    BTStatus tick(float, ActionContext&) override { return BTStatus::Failure; }
    std::string getTypeName() const override { return "AlwaysFail"; }
};

// Leaf that runs for N ticks then succeeds
class RunNTicks : public BTNode {
public:
    explicit RunNTicks(int n) : m_n(n) {}
    BTStatus tick(float, ActionContext&) override {
        return (++m_count >= m_n) ? BTStatus::Success : BTStatus::Running;
    }
    void reset() override { m_count = 0; }
    std::string getTypeName() const override { return "RunNTicks"; }
private:
    int m_n;
    int m_count = 0;
};

// Counter leaf — counts how many times it's ticked
class CounterLeaf : public BTNode {
public:
    BTStatus tick(float, ActionContext&) override { ++count; return BTStatus::Success; }
    std::string getTypeName() const override { return "Counter"; }
    int count = 0;
};

class BehaviorTreeTest : public ::testing::Test {
protected:
    ActionContext ctx;
    Blackboard bb;
    void SetUp() override {
        ctx.blackboard = &bb;
    }
};

TEST_F(BehaviorTreeTest, SequenceAllSucceed) {
    auto seq = BT::Sequence();
    seq->child(std::make_shared<AlwaysSucceed>());
    seq->child(std::make_shared<AlwaysSucceed>());
    EXPECT_EQ(seq->tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, SequenceFailsOnFirstFailure) {
    auto seq = BT::Sequence();
    seq->child(std::make_shared<AlwaysSucceed>());
    seq->child(std::make_shared<AlwaysFail>());
    seq->child(std::make_shared<AlwaysSucceed>());
    EXPECT_EQ(seq->tick(0.1f, ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, SequenceRunningPauses) {
    auto runner = std::make_shared<RunNTicks>(3);
    auto seq = BT::Sequence();
    seq->child(std::make_shared<AlwaysSucceed>());
    seq->child(runner);
    
    EXPECT_EQ(seq->tick(0.1f, ctx), BTStatus::Running);
    EXPECT_EQ(seq->tick(0.1f, ctx), BTStatus::Running);
    EXPECT_EQ(seq->tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, SelectorFirstSuccessWins) {
    auto sel = BT::Selector();
    sel->child(std::make_shared<AlwaysFail>());
    sel->child(std::make_shared<AlwaysSucceed>());
    sel->child(std::make_shared<AlwaysFail>());
    EXPECT_EQ(sel->tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, SelectorAllFailMeansFailure) {
    auto sel = BT::Selector();
    sel->child(std::make_shared<AlwaysFail>());
    sel->child(std::make_shared<AlwaysFail>());
    EXPECT_EQ(sel->tick(0.1f, ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, SelectorRunningPauses) {
    auto runner = std::make_shared<RunNTicks>(2);
    auto sel = BT::Selector();
    sel->child(runner);
    
    EXPECT_EQ(sel->tick(0.1f, ctx), BTStatus::Running);
    EXPECT_EQ(sel->tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, InverterFlipsSuccess) {
    auto inv = BT::Inverter(std::make_shared<AlwaysSucceed>());
    EXPECT_EQ(inv->tick(0.1f, ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, InverterFlipsFailure) {
    auto inv = BT::Inverter(std::make_shared<AlwaysFail>());
    EXPECT_EQ(inv->tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, InverterPassesRunning) {
    auto inv = BT::Inverter(std::make_shared<RunNTicks>(2));
    EXPECT_EQ(inv->tick(0.1f, ctx), BTStatus::Running);
}

TEST_F(BehaviorTreeTest, RepeaterFiniteCount) {
    auto counter = std::make_shared<CounterLeaf>();
    auto rep = BT::Repeater(counter, 3);
    // First 2 ticks should keep running
    EXPECT_EQ(rep->tick(0.1f, ctx), BTStatus::Running);
    EXPECT_EQ(rep->tick(0.1f, ctx), BTStatus::Running);
    EXPECT_EQ(rep->tick(0.1f, ctx), BTStatus::Success);
    EXPECT_EQ(counter->count, 3);
}

TEST_F(BehaviorTreeTest, CooldownBlocksAfterCompletion) {
    auto leaf = std::make_shared<AlwaysSucceed>();
    auto cd = BT::Cooldown(leaf, 1.0f);
    
    // First tick succeeds
    EXPECT_EQ(cd->tick(0.1f, ctx), BTStatus::Success);
    // Now on cooldown
    EXPECT_EQ(cd->tick(0.1f, ctx), BTStatus::Failure);
    EXPECT_EQ(cd->tick(0.1f, ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, SucceederAlwaysSucceeds) {
    auto succ = BT::Succeeder(std::make_shared<AlwaysFail>());
    EXPECT_EQ(succ->tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, ConditionNodeTrue) {
    auto cond = BT::Condition([](ActionContext&) { return true; }, "IsTrue");
    EXPECT_EQ(cond->tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, ConditionNodeFalse) {
    auto cond = BT::Condition([](ActionContext&) { return false; }, "IsFalse");
    EXPECT_EQ(cond->tick(0.1f, ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, BBConditionMatchesTrue) {
    bb.setBool("alert", true);
    auto cond = BT::BBCheck("alert", true);
    EXPECT_EQ(cond->tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, BBConditionMismatch) {
    bb.setBool("alert", false);
    auto cond = BT::BBCheck("alert", true);
    EXPECT_EQ(cond->tick(0.1f, ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, BBHasKeyPresent) {
    bb.setInt("count", 5);
    auto cond = BT::BBHasKey("count");
    EXPECT_EQ(cond->tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, BBHasKeyMissing) {
    auto cond = BT::BBHasKey("count");
    EXPECT_EQ(cond->tick(0.1f, ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, ParallelAllSucceed) {
    auto par = BT::Parallel();
    par->child(std::make_shared<AlwaysSucceed>());
    par->child(std::make_shared<AlwaysSucceed>());
    EXPECT_EQ(par->tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, ParallelFailsWhenTooManyFail) {
    auto par = BT::Parallel(2);
    par->child(std::make_shared<AlwaysSucceed>());
    par->child(std::make_shared<AlwaysFail>());
    par->child(std::make_shared<AlwaysFail>());
    EXPECT_EQ(par->tick(0.1f, ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, BehaviorTreeRoot) {
    BehaviorTree tree(std::make_shared<AlwaysSucceed>());
    EXPECT_EQ(tree.tick(0.1f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, BehaviorTreeNullRoot) {
    BehaviorTree tree;
    EXPECT_EQ(tree.tick(0.1f, ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, BehaviorTreeReset) {
    auto runner = std::make_shared<RunNTicks>(3);
    BehaviorTree tree(runner);
    tree.tick(0.1f, ctx); // tick 1
    tree.reset();
    // After reset, should start over
    EXPECT_EQ(tree.tick(0.1f, ctx), BTStatus::Running); // tick 1 again
}

// ============================================================================
// UtilityAI Tests
// ============================================================================

TEST_F(BehaviorTreeTest, ConsiderationClampsScore) {
    // Score function returns > 1.0
    Consideration c("test", [](ActionContext&) { return 2.0f; });
    EXPECT_FLOAT_EQ(c.evaluate(ctx), 1.0f);
}

TEST_F(BehaviorTreeTest, ConsiderationWeight) {
    Consideration c("test", [](ActionContext&) { return 0.8f; }, 0.5f);
    EXPECT_FLOAT_EQ(c.evaluate(ctx), 0.4f); // 0.8 * 0.5
}

TEST_F(BehaviorTreeTest, UtilityActionScoring) {
    auto action = std::make_shared<UtilityAction>("idle", std::make_shared<AlwaysSucceed>());
    action->addConsideration("energy", [](ActionContext&) { return 0.5f; });
    action->addConsideration("safety", [](ActionContext&) { return 0.8f; });
    float score = action->evaluate(ctx);
    EXPECT_FLOAT_EQ(score, 0.4f); // 0.5 * 0.8
}

TEST_F(BehaviorTreeTest, UtilityActionNoConsiderationsZero) {
    auto action = std::make_shared<UtilityAction>("empty", std::make_shared<AlwaysSucceed>());
    EXPECT_FLOAT_EQ(action->evaluate(ctx), 0.0f);
}

TEST_F(BehaviorTreeTest, UtilityBrainSelectsBest) {
    auto brain = std::make_shared<UtilityBrain>();
    brain->setEvalInterval(0.0f); // Re-evaluate every tick
    
    // Use Running actions so they don't immediately complete and clear m_current
    auto idleRunner = std::make_shared<RunNTicks>(100);
    auto fightRunner = std::make_shared<RunNTicks>(100);
    
    auto idle = std::make_shared<UtilityAction>("idle", idleRunner);
    idle->addConsideration("always", [](ActionContext&) { return 0.3f; });
    
    auto fight = std::make_shared<UtilityAction>("fight", fightRunner);
    fight->addConsideration("always", [](ActionContext&) { return 0.9f; });
    
    brain->addAction(idle);
    brain->addAction(fight);
    
    brain->tick(0.1f, ctx);
    EXPECT_EQ(brain->getCurrentActionName(), "fight");
}

TEST_F(BehaviorTreeTest, UtilityBrainToJson) {
    auto brain = std::make_shared<UtilityBrain>();
    auto idle = std::make_shared<UtilityAction>("idle", std::make_shared<AlwaysSucceed>());
    idle->addConsideration("c1", [](ActionContext&) { return 0.5f; });
    brain->addAction(idle);
    
    auto j = brain->toJson(ctx);
    EXPECT_TRUE(j.contains("currentAction"));
    EXPECT_TRUE(j.contains("scores"));
    EXPECT_EQ(j["scores"].size(), 1u);
    EXPECT_EQ(j["scores"][0]["name"], "idle");
}

// ============================================================================
// ActionNode Tests (BT leaf wrapping NPCAction)
// ============================================================================

TEST_F(BehaviorTreeTest, WaitActionCompletesAfterDuration) {
    auto waitAction = std::make_shared<WaitAction>(0.5f);
    auto node = BT::Action(waitAction);
    
    // Should be running for a while
    EXPECT_EQ(node->tick(0.2f, ctx), BTStatus::Running);
    EXPECT_EQ(node->tick(0.2f, ctx), BTStatus::Running);
    // Should complete after 0.5s total
    EXPECT_EQ(node->tick(0.2f, ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, SetBlackboardActionSetsValue) {
    auto action = std::make_shared<SetBlackboardAction>("state", "alert");
    auto node = BT::Action(action);
    
    EXPECT_EQ(node->tick(0.1f, ctx), BTStatus::Success);
    EXPECT_EQ(bb.getString("state"), "alert");
}

// ============================================================================
// Integration: Complex Tree
// ============================================================================

TEST_F(BehaviorTreeTest, ComplexTreeGuardBehavior) {
    // Build a guard-like behavior tree:
    //   Selector
    //     Sequence (combat)
    //       Condition(hasThreat)
    //       AlwaysSucceed (placeholder for attack)
    //     Sequence (idle)
    //       AlwaysSucceed (placeholder for patrol)
    
    auto combat = BT::Sequence();
    combat->child(BT::BBCheck("hasThreat"));
    combat->child(std::make_shared<AlwaysSucceed>());
    
    auto idle = BT::Sequence();
    idle->child(std::make_shared<AlwaysSucceed>());
    
    auto root = BT::Selector();
    root->child(combat);
    root->child(idle);
    
    BehaviorTree tree(root);
    
    // No threat — selector falls through to idle
    bb.setBool("hasThreat", false);
    EXPECT_EQ(tree.tick(0.1f, ctx), BTStatus::Success);
    
    // With threat — combat path runs
    bb.setBool("hasThreat", true);
    tree.reset();
    EXPECT_EQ(tree.tick(0.1f, ctx), BTStatus::Success);
}
