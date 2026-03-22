#include "story/CharacterAgent.h"

namespace Phyxel {
namespace Story {

void to_json(nlohmann::json& j, const CharacterDecision& d) {
    j = nlohmann::json{
        {"action", d.action},
        {"parameters", d.parameters},
        {"reasoning", d.reasoning},
        {"dialogueText", d.dialogueText},
        {"emotion", d.emotion}
    };
}

void from_json(const nlohmann::json& j, CharacterDecision& d) {
    j.at("action").get_to(d.action);
    if (j.contains("parameters")) d.parameters = j["parameters"];
    if (j.contains("reasoning")) j.at("reasoning").get_to(d.reasoning);
    if (j.contains("dialogueText")) j.at("dialogueText").get_to(d.dialogueText);
    if (j.contains("emotion")) j.at("emotion").get_to(d.emotion);
}

} // namespace Story
} // namespace Phyxel
