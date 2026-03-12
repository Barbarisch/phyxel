#include <gtest/gtest.h>
#include "ui/DialogueData.h"
#include "ui/DialogueSystem.h"
#include "ui/SpeechBubbleManager.h"
#include <nlohmann/json.hpp>

using namespace Phyxel;
using namespace Phyxel::UI;

// ============================================================================
// DialogueTree Tests
// ============================================================================

class DialogueTreeTest : public ::testing::Test {
protected:
    DialogueTree makeSimpleTree() {
        DialogueTree tree;
        tree.id = "test_tree";
        tree.startNodeId = "start";

        DialogueNode startNode;
        startNode.id = "start";
        startNode.speaker = "Guard";
        startNode.text = "Halt! Who goes there?";
        startNode.nextNodeId = "response";
        tree.nodes["start"] = startNode;

        DialogueNode responseNode;
        responseNode.id = "response";
        responseNode.speaker = "Guard";
        responseNode.text = "Move along then.";
        // No nextNodeId = end of conversation
        tree.nodes["response"] = responseNode;

        return tree;
    }

    DialogueTree makeChoiceTree() {
        DialogueTree tree;
        tree.id = "choice_tree";
        tree.startNodeId = "start";

        DialogueNode startNode;
        startNode.id = "start";
        startNode.speaker = "Merchant";
        startNode.text = "What do you want to buy?";
        startNode.choices.push_back({"Sword", "buy_sword"});
        startNode.choices.push_back({"Shield", "buy_shield"});
        startNode.choices.push_back({"Nothing", "goodbye"});
        tree.nodes["start"] = startNode;

        DialogueNode swordNode;
        swordNode.id = "buy_sword";
        swordNode.speaker = "Merchant";
        swordNode.text = "A fine choice!";
        tree.nodes["buy_sword"] = swordNode;

        DialogueNode shieldNode;
        shieldNode.id = "buy_shield";
        shieldNode.speaker = "Merchant";
        shieldNode.text = "Solid protection!";
        tree.nodes["buy_shield"] = shieldNode;

        DialogueNode goodbyeNode;
        goodbyeNode.id = "goodbye";
        goodbyeNode.speaker = "Merchant";
        goodbyeNode.text = "Come back anytime!";
        tree.nodes["goodbye"] = goodbyeNode;

        return tree;
    }
};

TEST_F(DialogueTreeTest, GetNode) {
    auto tree = makeSimpleTree();
    auto* node = tree.getNode("start");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->speaker, "Guard");
    EXPECT_EQ(node->text, "Halt! Who goes there?");
}

TEST_F(DialogueTreeTest, GetNodeNotFound) {
    auto tree = makeSimpleTree();
    EXPECT_EQ(tree.getNode("nonexistent"), nullptr);
}

TEST_F(DialogueTreeTest, HasNode) {
    auto tree = makeSimpleTree();
    EXPECT_TRUE(tree.hasNode("start"));
    EXPECT_TRUE(tree.hasNode("response"));
    EXPECT_FALSE(tree.hasNode("missing"));
}

TEST_F(DialogueTreeTest, JsonRoundTrip) {
    auto original = makeChoiceTree();
    auto json = original.toJson();
    auto deserialized = DialogueTree::fromJson(json);

    EXPECT_EQ(deserialized.id, original.id);
    EXPECT_EQ(deserialized.startNodeId, original.startNodeId);
    EXPECT_EQ(deserialized.nodes.size(), original.nodes.size());

    auto* startNode = deserialized.getNode("start");
    ASSERT_NE(startNode, nullptr);
    EXPECT_EQ(startNode->speaker, "Merchant");
    EXPECT_EQ(startNode->choices.size(), 3u);
    EXPECT_EQ(startNode->choices[0].text, "Sword");
    EXPECT_EQ(startNode->choices[0].targetNodeId, "buy_sword");
}

TEST_F(DialogueTreeTest, JsonFromScratch) {
    nlohmann::json j = {
        {"id", "json_tree"},
        {"startNodeId", "n1"},
        {"nodes", nlohmann::json::array({
            {{"id", "n1"}, {"speaker", "NPC"}, {"text", "Hello"}, {"nextNodeId", "n2"},
             {"emotion", "happy"}, {"choices", nlohmann::json::array()}},
            {{"id", "n2"}, {"speaker", "NPC"}, {"text", "Goodbye"}, {"nextNodeId", ""},
             {"emotion", ""}, {"choices", nlohmann::json::array()}}
        })}
    };

    auto tree = DialogueTree::fromJson(j);
    EXPECT_EQ(tree.id, "json_tree");
    ASSERT_TRUE(tree.hasNode("n1"));
    ASSERT_TRUE(tree.hasNode("n2"));
    EXPECT_EQ(tree.getNode("n1")->emotion, "happy");
}

// ============================================================================
// DialogueSystem State Machine Tests
// ============================================================================

class DialogueSystemTest : public ::testing::Test {
protected:
    DialogueSystem system;

    DialogueTree makeLinearTree() {
        DialogueTree tree;
        tree.id = "linear";
        tree.startNodeId = "a";

        DialogueNode a; a.id = "a"; a.speaker = "NPC"; a.text = "Hello"; a.nextNodeId = "b";
        DialogueNode b; b.id = "b"; b.speaker = "NPC"; b.text = "Goodbye";
        tree.nodes["a"] = a;
        tree.nodes["b"] = b;
        return tree;
    }

    DialogueTree makeChoiceTree() {
        DialogueTree tree;
        tree.id = "choice";
        tree.startNodeId = "q";

        DialogueNode q;
        q.id = "q"; q.speaker = "NPC"; q.text = "Pick one";
        q.choices.push_back({"Option A", "aa"});
        q.choices.push_back({"Option B", "bb"});

        DialogueNode aa; aa.id = "aa"; aa.speaker = "NPC"; aa.text = "You chose A";
        DialogueNode bb; bb.id = "bb"; bb.speaker = "NPC"; bb.text = "You chose B";

        tree.nodes["q"] = q;
        tree.nodes["aa"] = aa;
        tree.nodes["bb"] = bb;
        return tree;
    }
};

TEST_F(DialogueSystemTest, InitiallyInactive) {
    EXPECT_FALSE(system.isActive());
    EXPECT_EQ(system.getState(), DialogueState::Inactive);
}

TEST_F(DialogueSystemTest, StartConversation) {
    auto tree = makeLinearTree();
    bool started = system.startConversation("TestNPC", &tree);
    EXPECT_TRUE(started);
    EXPECT_TRUE(system.isActive());
    EXPECT_EQ(system.getCurrentSpeaker(), "NPC");
    EXPECT_EQ(system.getCurrentText(), "Hello");
}

TEST_F(DialogueSystemTest, CannotStartWhileActive) {
    auto tree = makeLinearTree();
    system.startConversation("NPC1", &tree);
    bool second = system.startConversation("NPC2", &tree);
    EXPECT_FALSE(second);
}

TEST_F(DialogueSystemTest, TypewriterEffect) {
    system.setTypingSpeed(10.0f); // 10 chars/sec
    auto tree = makeLinearTree();
    system.startConversation("NPC", &tree);

    EXPECT_EQ(system.getState(), DialogueState::Typing);
    EXPECT_EQ(system.getRevealedText(), "");

    // Advance typewriter partially
    system.update(0.3f); // Should reveal 3 chars
    EXPECT_EQ(system.getRevealedText().size(), 3u);
    EXPECT_EQ(system.getRevealedText(), "Hel");
}

TEST_F(DialogueSystemTest, SkipTypewriter) {
    system.setTypingSpeed(5.0f);
    auto tree = makeLinearTree();
    system.startConversation("NPC", &tree);

    EXPECT_EQ(system.getState(), DialogueState::Typing);
    system.advanceDialogue(); // Skip
    EXPECT_EQ(system.getRevealedText(), "Hello");
    // After skipping, should be waiting for input (linear node)
    EXPECT_EQ(system.getState(), DialogueState::WaitingForInput);
}

TEST_F(DialogueSystemTest, AdvanceToNextNode) {
    auto tree = makeLinearTree();
    system.startConversation("NPC", &tree);

    // Skip typewriter on first node
    system.advanceDialogue();
    EXPECT_EQ(system.getState(), DialogueState::WaitingForInput);

    // Advance to next node
    system.advanceDialogue();
    EXPECT_EQ(system.getCurrentText(), "Goodbye");
    EXPECT_TRUE(system.isActive());
}

TEST_F(DialogueSystemTest, EndOfTree) {
    auto tree = makeLinearTree();
    system.startConversation("NPC", &tree);

    system.advanceDialogue(); // skip typewriter on "Hello"
    system.advanceDialogue(); // go to "Goodbye"
    system.advanceDialogue(); // skip typewriter on "Goodbye"
    system.advanceDialogue(); // end (no nextNodeId)

    EXPECT_FALSE(system.isActive());
    EXPECT_EQ(system.getState(), DialogueState::Inactive);
}

TEST_F(DialogueSystemTest, ChoiceSelection) {
    auto tree = makeChoiceTree();
    system.startConversation("NPC", &tree);

    // Skip typewriter
    system.advanceDialogue();
    EXPECT_EQ(system.getState(), DialogueState::ChoiceSelection);
    EXPECT_EQ(system.getAvailableChoices().size(), 2u);

    // Select choice B
    system.selectChoice(1);
    EXPECT_EQ(system.getCurrentText(), "You chose B");
}

TEST_F(DialogueSystemTest, InvalidChoiceIgnored) {
    auto tree = makeChoiceTree();
    system.startConversation("NPC", &tree);
    system.advanceDialogue(); // skip typewriter → ChoiceSelection

    system.selectChoice(5); // out of range
    EXPECT_EQ(system.getState(), DialogueState::ChoiceSelection); // unchanged
    system.selectChoice(-1); // negative
    EXPECT_EQ(system.getState(), DialogueState::ChoiceSelection); // unchanged
}

TEST_F(DialogueSystemTest, EndConversationManually) {
    auto tree = makeLinearTree();
    system.startConversation("NPC", &tree);
    EXPECT_TRUE(system.isActive());

    system.endConversation();
    EXPECT_FALSE(system.isActive());
}

TEST_F(DialogueSystemTest, EndCallback) {
    std::string callbackNPC;
    std::string callbackNode;
    system.setConversationEndCallback([&](const std::string& npc, const std::string& node) {
        callbackNPC = npc;
        callbackNode = node;
    });

    auto tree = makeLinearTree();
    system.startConversation("TestNPC", &tree);
    system.endConversation();

    EXPECT_EQ(callbackNPC, "TestNPC");
}

TEST_F(DialogueSystemTest, NullTreeRejected) {
    bool ok = system.startConversation("NPC", nullptr);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(system.isActive());
}

TEST_F(DialogueSystemTest, TypingSpeed) {
    system.setTypingSpeed(100.0f);
    EXPECT_FLOAT_EQ(system.getTypingSpeed(), 100.0f);
}

TEST_F(DialogueSystemTest, ChoiceConditionFiltering) {
    DialogueTree tree;
    tree.id = "cond";
    tree.startNodeId = "q";

    DialogueNode q;
    q.id = "q"; q.speaker = "NPC"; q.text = "Choose";
    DialogueChoice c1; c1.text = "Always"; c1.targetNodeId = "a";
    DialogueChoice c2; c2.text = "Never"; c2.targetNodeId = "b";
    c2.condition = []() { return false; }; // This choice should be filtered
    DialogueChoice c3; c3.text = "Also always"; c3.targetNodeId = "c";
    q.choices = {c1, c2, c3};
    tree.nodes["q"] = q;

    DialogueNode a; a.id = "a"; a.speaker = "NPC"; a.text = "A";
    DialogueNode b; b.id = "b"; b.speaker = "NPC"; b.text = "B";
    DialogueNode c; c.id = "c"; c.speaker = "NPC"; c.text = "C";
    tree.nodes["a"] = a;
    tree.nodes["b"] = b;
    tree.nodes["c"] = c;

    system.startConversation("NPC", &tree);
    system.advanceDialogue(); // skip typewriter

    // Only 2 choices should be available (the one with false condition filtered)
    EXPECT_EQ(system.getAvailableChoices().size(), 2u);
    EXPECT_EQ(system.getAvailableChoices()[0].text, "Always");
    EXPECT_EQ(system.getAvailableChoices()[1].text, "Also always");
}

// ============================================================================
// SpeechBubbleManager Tests
// ============================================================================

class SpeechBubbleManagerTest : public ::testing::Test {
protected:
    SpeechBubbleManager manager;
};

TEST_F(SpeechBubbleManagerTest, InitiallyEmpty) {
    EXPECT_EQ(manager.getBubbleCount(), 0u);
    EXPECT_TRUE(manager.getBubbles().empty());
}

TEST_F(SpeechBubbleManagerTest, CreateBubble) {
    manager.say("entity_1", "Hello world!", 3.0f);
    EXPECT_EQ(manager.getBubbleCount(), 1u);
    EXPECT_EQ(manager.getBubbles()[0].text, "Hello world!");
    EXPECT_EQ(manager.getBubbles()[0].speakerEntityId, "entity_1");
}

TEST_F(SpeechBubbleManagerTest, BubbleExpires) {
    manager.say("e1", "Temp", 1.0f);
    EXPECT_EQ(manager.getBubbleCount(), 1u);

    manager.update(0.5f);
    EXPECT_EQ(manager.getBubbleCount(), 1u); // Still alive

    manager.update(0.6f); // Total elapsed > 1.0
    EXPECT_EQ(manager.getBubbleCount(), 0u); // Gone
}

TEST_F(SpeechBubbleManagerTest, MaxBubblesEviction) {
    for (size_t i = 0; i < SpeechBubbleManager::MAX_BUBBLES + 2; ++i) {
        manager.say("e" + std::to_string(i), "Msg " + std::to_string(i));
    }
    EXPECT_EQ(manager.getBubbleCount(), SpeechBubbleManager::MAX_BUBBLES);
    // Oldest should have been evicted
    EXPECT_EQ(manager.getBubbles()[0].text, "Msg 2"); // 0 and 1 evicted
}

TEST_F(SpeechBubbleManagerTest, OpacityBeforeFade) {
    manager.say("e1", "Test", 10.0f);
    const auto& bubble = manager.getBubbles()[0];
    EXPECT_FLOAT_EQ(manager.getBubbleOpacity(bubble), 1.0f);
}

TEST_F(SpeechBubbleManagerTest, OpacityDuringFade) {
    manager.say("e1", "Test", 10.0f);
    manager.update(8.0f); // Past 70% (7.0s)
    const auto& bubble = manager.getBubbles()[0];
    float opacity = manager.getBubbleOpacity(bubble);
    EXPECT_GT(opacity, 0.0f);
    EXPECT_LT(opacity, 1.0f);
}

TEST_F(SpeechBubbleManagerTest, WorldPositionNoRegistry) {
    manager.say("e1", "Test");
    const auto& bubble = manager.getBubbles()[0];
    glm::vec3 pos = manager.getBubbleWorldPosition(bubble);
    EXPECT_EQ(pos, glm::vec3(0)); // No registry set
}

TEST_F(SpeechBubbleManagerTest, MultipleBubbles) {
    manager.say("e1", "First", 5.0f);
    manager.say("e2", "Second", 5.0f);
    manager.say("e3", "Third", 5.0f);
    EXPECT_EQ(manager.getBubbleCount(), 3u);
}

// ============================================================================
// StaticDialogueProvider Tests
// ============================================================================

TEST(DialogueProviderTest, StaticProvider) {
    DialogueTree tree;
    tree.id = "provider_test";
    tree.startNodeId = "start";

    StaticDialogueProvider provider(std::move(tree));
    auto* t = provider.getDialogueTree();
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->id, "provider_test");
}

TEST(DialogueProviderTest, SetTree) {
    DialogueTree tree1;
    tree1.id = "tree1";
    StaticDialogueProvider provider(std::move(tree1));

    DialogueTree tree2;
    tree2.id = "tree2";
    provider.setTree(std::move(tree2));

    EXPECT_EQ(provider.getDialogueTree()->id, "tree2");
}
