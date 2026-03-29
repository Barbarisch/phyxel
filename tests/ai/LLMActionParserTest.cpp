#include <gtest/gtest.h>
#include "ai/LLMActionParser.h"

using namespace Phyxel::AI;

// ============================================================================
// LLMActionParser::parse() Tests
// ============================================================================

TEST(LLMActionParserTest, ParseEmptyString) {
    auto result = LLMActionParser::parse("");
    EXPECT_TRUE(result.dialogueText.empty());
    EXPECT_TRUE(result.actions.empty());
}

TEST(LLMActionParserTest, ParsePlainTextNoTags) {
    auto result = LLMActionParser::parse("Hello traveler, welcome to the village!");
    EXPECT_EQ(result.dialogueText, "Hello traveler, welcome to the village!");
    EXPECT_TRUE(result.actions.empty());
}

TEST(LLMActionParserTest, ParseSingleEmoteTag) {
    auto result = LLMActionParser::parse("[EMOTE:wave] Hello there!");
    EXPECT_EQ(result.dialogueText, "Hello there!");
    ASSERT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].type, LLMActionType::Emote);
    EXPECT_EQ(result.actions[0].param1, "wave");
    EXPECT_TRUE(result.actions[0].param2.empty());
}

TEST(LLMActionParserTest, ParseSingleMoodTag) {
    auto result = LLMActionParser::parse("I'm feeling great today! [MOOD:happy]");
    EXPECT_EQ(result.dialogueText, "I'm feeling great today!");
    ASSERT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].type, LLMActionType::Mood);
    EXPECT_EQ(result.actions[0].param1, "happy");
}

TEST(LLMActionParserTest, ParseGiveItemTag) {
    auto result = LLMActionParser::parse("[GIVE_ITEM:iron_sword] Take this sword!");
    ASSERT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].type, LLMActionType::GiveItem);
    EXPECT_EQ(result.actions[0].param1, "iron_sword");
}

TEST(LLMActionParserTest, ParseQuestUpdateWithTwoParams) {
    auto result = LLMActionParser::parse("[QUEST_UPDATE:main_quest:completed] The quest is done.");
    ASSERT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].type, LLMActionType::QuestUpdate);
    EXPECT_EQ(result.actions[0].param1, "main_quest");
    EXPECT_EQ(result.actions[0].param2, "completed");
}

TEST(LLMActionParserTest, ParseQuestUpdateWithOneParam) {
    auto result = LLMActionParser::parse("[QUEST_UPDATE:side_quest] Check on this.");
    ASSERT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].type, LLMActionType::QuestUpdate);
    EXPECT_EQ(result.actions[0].param1, "side_quest");
    EXPECT_TRUE(result.actions[0].param2.empty());
}

TEST(LLMActionParserTest, ParseMultipleTags) {
    std::string response = "[EMOTE:bow] [MOOD:respectful] Greetings, noble warrior. [GIVE_ITEM:healing_potion]";
    auto result = LLMActionParser::parse(response);
    EXPECT_EQ(result.dialogueText, "Greetings, noble warrior.");
    ASSERT_EQ(result.actions.size(), 3);
    EXPECT_EQ(result.actions[0].type, LLMActionType::Emote);
    EXPECT_EQ(result.actions[0].param1, "bow");
    EXPECT_EQ(result.actions[1].type, LLMActionType::Mood);
    EXPECT_EQ(result.actions[1].param1, "respectful");
    EXPECT_EQ(result.actions[2].type, LLMActionType::GiveItem);
    EXPECT_EQ(result.actions[2].param1, "healing_potion");
}

TEST(LLMActionParserTest, ParseInlineTags) {
    auto result = LLMActionParser::parse("I see you [EMOTE:nod] and I trust you.");
    EXPECT_EQ(result.dialogueText, "I see you  and I trust you.");
    ASSERT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].type, LLMActionType::Emote);
}

TEST(LLMActionParserTest, ParseTagOnSeparateLine) {
    auto result = LLMActionParser::parse("Hello!\n[MOOD:cheerful]\nHow are you?");
    EXPECT_NE(result.dialogueText.find("Hello!"), std::string::npos);
    EXPECT_NE(result.dialogueText.find("How are you?"), std::string::npos);
    ASSERT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].param1, "cheerful");
}

TEST(LLMActionParserTest, ParseOnlyTags) {
    auto result = LLMActionParser::parse("[EMOTE:wave][MOOD:happy]");
    EXPECT_TRUE(result.dialogueText.empty());
    ASSERT_EQ(result.actions.size(), 2);
}

TEST(LLMActionParserTest, UnknownTagsIgnored) {
    auto result = LLMActionParser::parse("[UNKNOWN:foo] Hello [EMOTE:wave]");
    // [UNKNOWN:foo] is not matched by the regex, so it stays in the text
    EXPECT_NE(result.dialogueText.find("[UNKNOWN:foo]"), std::string::npos);
    ASSERT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].type, LLMActionType::Emote);
}

TEST(LLMActionParserTest, WhitespaceCleanup) {
    // After stripping a tag from the start, leading whitespace should be trimmed
    auto result = LLMActionParser::parse("   [EMOTE:wave]   Hello   ");
    EXPECT_EQ(result.dialogueText, "Hello");
}

TEST(LLMActionParserTest, MultilineCleanup) {
    auto result = LLMActionParser::parse("[EMOTE:bow]\n\n\nHello!\n\n\nGoodbye!");
    // Should collapse multiple blank lines
    EXPECT_EQ(result.dialogueText, "Hello!\n\nGoodbye!");
}

// ============================================================================
// LLMActionParser::execute() Tests
// ============================================================================

TEST(LLMActionParserTest, ExecuteCallsEmoteHandler) {
    LLMParseResult result;
    result.actions.push_back({LLMActionType::Emote, "wave", ""});

    std::string capturedEmote;
    LLMActionHandler handler;
    handler.onEmote = [&](const std::string& e) { capturedEmote = e; };

    LLMActionParser::execute(result, handler);
    EXPECT_EQ(capturedEmote, "wave");
}

TEST(LLMActionParserTest, ExecuteCallsGiveItemHandler) {
    LLMParseResult result;
    result.actions.push_back({LLMActionType::GiveItem, "sword", ""});

    std::string capturedItem;
    LLMActionHandler handler;
    handler.onGiveItem = [&](const std::string& item) { capturedItem = item; };

    LLMActionParser::execute(result, handler);
    EXPECT_EQ(capturedItem, "sword");
}

TEST(LLMActionParserTest, ExecuteCallsQuestUpdateHandler) {
    LLMParseResult result;
    result.actions.push_back({LLMActionType::QuestUpdate, "main", "completed"});

    std::string capturedQuest, capturedStatus;
    LLMActionHandler handler;
    handler.onQuestUpdate = [&](const std::string& q, const std::string& s) {
        capturedQuest = q;
        capturedStatus = s;
    };

    LLMActionParser::execute(result, handler);
    EXPECT_EQ(capturedQuest, "main");
    EXPECT_EQ(capturedStatus, "completed");
}

TEST(LLMActionParserTest, ExecuteCallsMoodHandler) {
    LLMParseResult result;
    result.actions.push_back({LLMActionType::Mood, "angry", ""});

    std::string capturedMood;
    LLMActionHandler handler;
    handler.onMood = [&](const std::string& m) { capturedMood = m; };

    LLMActionParser::execute(result, handler);
    EXPECT_EQ(capturedMood, "angry");
}

TEST(LLMActionParserTest, ExecuteSkipsNullHandlers) {
    LLMParseResult result;
    result.actions.push_back({LLMActionType::Emote, "wave", ""});
    result.actions.push_back({LLMActionType::GiveItem, "sword", ""});

    LLMActionHandler handler; // all callbacks null
    // Should not crash
    EXPECT_NO_THROW(LLMActionParser::execute(result, handler));
}

TEST(LLMActionParserTest, ExecuteMultipleActionsInOrder) {
    LLMParseResult result;
    result.actions.push_back({LLMActionType::Emote, "bow", ""});
    result.actions.push_back({LLMActionType::Mood, "happy", ""});
    result.actions.push_back({LLMActionType::GiveItem, "potion", ""});

    std::vector<std::string> order;
    LLMActionHandler handler;
    handler.onEmote = [&](const std::string&) { order.push_back("emote"); };
    handler.onMood = [&](const std::string&) { order.push_back("mood"); };
    handler.onGiveItem = [&](const std::string&) { order.push_back("give"); };

    LLMActionParser::execute(result, handler);
    ASSERT_EQ(order.size(), 3);
    EXPECT_EQ(order[0], "emote");
    EXPECT_EQ(order[1], "mood");
    EXPECT_EQ(order[2], "give");
}

// ============================================================================
// LLMActionParser::parseAndExecute() Tests
// ============================================================================

TEST(LLMActionParserTest, ParseAndExecuteReturnsCleanText) {
    std::string capturedEmote;
    LLMActionHandler handler;
    handler.onEmote = [&](const std::string& e) { capturedEmote = e; };

    auto text = LLMActionParser::parseAndExecute("[EMOTE:salute] Good day, sir!", handler);
    EXPECT_EQ(text, "Good day, sir!");
    EXPECT_EQ(capturedEmote, "salute");
}

TEST(LLMActionParserTest, ParseAndExecuteWithNoTags) {
    LLMActionHandler handler;
    auto text = LLMActionParser::parseAndExecute("Just plain text.", handler);
    EXPECT_EQ(text, "Just plain text.");
}

// ============================================================================
// llmActionTypeToString() Tests
// ============================================================================

TEST(LLMActionParserTest, ActionTypeToString) {
    EXPECT_EQ(llmActionTypeToString(LLMActionType::Emote), "EMOTE");
    EXPECT_EQ(llmActionTypeToString(LLMActionType::GiveItem), "GIVE_ITEM");
    EXPECT_EQ(llmActionTypeToString(LLMActionType::QuestUpdate), "QUEST_UPDATE");
    EXPECT_EQ(llmActionTypeToString(LLMActionType::Mood), "MOOD");
}
