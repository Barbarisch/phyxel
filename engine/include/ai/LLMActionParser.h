#pragma once

#include <string>
#include <vector>
#include <functional>

namespace Phyxel {
namespace AI {

// ============================================================================
// LLMAction — a parsed action tag extracted from an LLM response
// ============================================================================

enum class LLMActionType {
    Emote,       // [EMOTE:wave]
    GiveItem,    // [GIVE_ITEM:sword]
    QuestUpdate, // [QUEST_UPDATE:main_quest:completed]
    Mood         // [MOOD:happy]
};

std::string llmActionTypeToString(LLMActionType type);

struct LLMAction {
    LLMActionType type;
    std::string param1;  // Primary param (emote name, item name, quest ID, mood)
    std::string param2;  // Secondary param (quest status: started/completed/failed)
};

// ============================================================================
// LLMParseResult — separated dialogue text + extracted actions
// ============================================================================

struct LLMParseResult {
    std::string dialogueText;          // Response with action tags stripped
    std::vector<LLMAction> actions;    // Extracted actions in order
};

// ============================================================================
// LLMActionHandler — callback interface for executing parsed actions
// ============================================================================

struct LLMActionHandler {
    std::function<void(const std::string& emote)> onEmote;
    std::function<void(const std::string& itemName)> onGiveItem;
    std::function<void(const std::string& questId, const std::string& status)> onQuestUpdate;
    std::function<void(const std::string& mood)> onMood;
};

// ============================================================================
// LLMActionParser — parses action tags from LLM text responses
//
// Tags are bracket-delimited: [TYPE:param] or [TYPE:param1:param2]
// Tags can appear inline or on separate lines. They are stripped from
// the returned dialogue text.
// ============================================================================

class LLMActionParser {
public:
    /// Parse an LLM response, extracting action tags and clean dialogue text.
    static LLMParseResult parse(const std::string& response);

    /// Execute all actions in a parse result using the provided handler.
    static void execute(const LLMParseResult& result, const LLMActionHandler& handler);

    /// Parse and execute in one call. Returns the clean dialogue text.
    static std::string parseAndExecute(const std::string& response,
                                        const LLMActionHandler& handler);
};

} // namespace AI
} // namespace Phyxel
