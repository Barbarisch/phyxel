#include "ai/LLMActionParser.h"
#include <regex>
#include <sstream>

namespace Phyxel {
namespace AI {

std::string llmActionTypeToString(LLMActionType type) {
    switch (type) {
        case LLMActionType::Emote:       return "EMOTE";
        case LLMActionType::GiveItem:    return "GIVE_ITEM";
        case LLMActionType::QuestUpdate: return "QUEST_UPDATE";
        case LLMActionType::Mood:        return "MOOD";
        default:                         return "UNKNOWN";
    }
}

LLMParseResult LLMActionParser::parse(const std::string& response) {
    LLMParseResult result;

    // Pattern: [TYPE:param1] or [TYPE:param1:param2]
    // Matches: [EMOTE:wave], [GIVE_ITEM:sword], [QUEST_UPDATE:main:completed], [MOOD:happy]
    static const std::regex tagPattern(R"(\[(EMOTE|GIVE_ITEM|QUEST_UPDATE|MOOD):([^\]\:]+)(?::([^\]]+))?\])");

    // Extract all action tags
    auto begin = std::sregex_iterator(response.begin(), response.end(), tagPattern);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        const auto& match = *it;
        std::string typeStr = match[1].str();
        std::string p1 = match[2].str();
        std::string p2 = match[3].matched ? match[3].str() : "";

        LLMAction action;
        if (typeStr == "EMOTE")            action.type = LLMActionType::Emote;
        else if (typeStr == "GIVE_ITEM")   action.type = LLMActionType::GiveItem;
        else if (typeStr == "QUEST_UPDATE") action.type = LLMActionType::QuestUpdate;
        else if (typeStr == "MOOD")        action.type = LLMActionType::Mood;

        action.param1 = p1;
        action.param2 = p2;
        result.actions.push_back(action);
    }

    // Strip tags from text to get clean dialogue
    std::string cleaned = std::regex_replace(response, tagPattern, "");

    // Clean up whitespace: collapse multiple blank lines, trim
    std::istringstream stream(cleaned);
    std::ostringstream out;
    std::string line;
    bool lastWasEmpty = false;
    bool first = true;

    while (std::getline(stream, line)) {
        // Trim trailing whitespace
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            line.pop_back();

        if (line.empty()) {
            lastWasEmpty = true;
            continue;
        }

        if (!first && lastWasEmpty)
            out << "\n";
        if (!first)
            out << "\n";

        out << line;
        first = false;
        lastWasEmpty = false;
    }

    result.dialogueText = out.str();

    // Trim leading/trailing whitespace
    while (!result.dialogueText.empty() && result.dialogueText.front() == ' ')
        result.dialogueText.erase(result.dialogueText.begin());
    while (!result.dialogueText.empty() && result.dialogueText.back() == ' ')
        result.dialogueText.pop_back();

    return result;
}

void LLMActionParser::execute(const LLMParseResult& result,
                               const LLMActionHandler& handler) {
    for (const auto& action : result.actions) {
        switch (action.type) {
            case LLMActionType::Emote:
                if (handler.onEmote) handler.onEmote(action.param1);
                break;
            case LLMActionType::GiveItem:
                if (handler.onGiveItem) handler.onGiveItem(action.param1);
                break;
            case LLMActionType::QuestUpdate:
                if (handler.onQuestUpdate) handler.onQuestUpdate(action.param1, action.param2);
                break;
            case LLMActionType::Mood:
                if (handler.onMood) handler.onMood(action.param1);
                break;
        }
    }
}

std::string LLMActionParser::parseAndExecute(const std::string& response,
                                              const LLMActionHandler& handler) {
    auto result = parse(response);
    execute(result, handler);
    return result.dialogueText;
}

} // namespace AI
} // namespace Phyxel
