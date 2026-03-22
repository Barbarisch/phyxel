#include "story/RuleBasedCharacterAgent.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Story {

void RuleBasedCharacterAgent::addRule(Rule rule) {
    m_rules.push_back(std::move(rule));
}

void RuleBasedCharacterAgent::clearRules() {
    m_rules.clear();
}

void RuleBasedCharacterAgent::setDialogueTemplates(
    const std::unordered_map<std::string, std::vector<std::string>>& templates) {
    m_dialogueTemplates = templates;
}

CharacterDecision RuleBasedCharacterAgent::decide(const CharacterDecisionContext& context) {
    // Evaluate custom rules first — highest scoring rule wins
    float bestScore = 0.0f;
    const Rule* bestRule = nullptr;

    for (const auto& rule : m_rules) {
        float score = rule.condition(context);
        if (score > bestScore) {
            bestScore = score;
            bestRule = &rule;
        }
    }

    if (bestRule && bestScore > 0.0f) {
        return bestRule->action(context);
    }

    // Fall back to personality-based decision
    return decideFromPersonality(context);
}

std::string RuleBasedCharacterAgent::generateDialogue(const CharacterDecisionContext& context) {
    if (!context.profile) return "";

    std::string emotion = context.profile->emotion.dominantEmotion();
    std::string line = pickDialogueTemplate(emotion);

    if (!line.empty()) return line;

    // Default ambient lines based on personality
    if (context.profile->traits.extraversion > 0.7f) {
        return "Hello there! Nice to see a friendly face.";
    }
    if (context.profile->traits.extraversion < 0.3f) {
        return "...";
    }
    return "Hmm.";
}

CharacterDecision RuleBasedCharacterAgent::decideFromPersonality(
    const CharacterDecisionContext& context) const {

    if (!context.profile) {
        return {"idle", {}, "no profile", "", ""};
    }

    const auto& profile = *context.profile;

    // Check if any action is available
    const auto& actions = context.availableActions;
    auto canDo = [&](const std::string& action) {
        return actions.empty() || std::find(actions.begin(), actions.end(), action) != actions.end();
    };

    // Score each potential action
    struct ScoredAction {
        std::string action;
        float score;
        std::string reasoning;
    };
    std::vector<ScoredAction> candidates;

    // Flee: high fear + low agreeableness + high neuroticism
    if (canDo("flee")) {
        float s = scoreFlee(context);
        if (s > 0.0f) candidates.push_back({"flee", s, "fear response"});
    }

    // Attack: high anger + low agreeableness
    if (canDo("attack")) {
        float s = scoreAttack(context);
        if (s > 0.0f) candidates.push_back({"attack", s, "hostile response"});
    }

    // Trade: if conversation partner exists and trust is positive
    if (canDo("trade") && context.conversationPartner) {
        float s = scoreTrade(context);
        if (s > 0.0f) candidates.push_back({"trade", s, "commerce interest"});
    }

    // Speak: if in conversation
    if (canDo("speak") && context.conversationPartner) {
        float s = profile.traits.extraversion * 0.5f + 0.3f;
        candidates.push_back({"speak", s, "engaging in conversation"});
    }

    // Move to: pursue active goals
    if (canDo("move_to")) {
        for (const auto& goal : profile.goals) {
            if (goal.isActive && goal.priority > 0.5f) {
                candidates.push_back({"move_to", goal.priority * 0.6f, "pursuing goal: " + goal.id});
                break;
            }
        }
    }

    // Wait: conscientiousness makes patience attractive
    if (canDo("wait")) {
        float s = profile.traits.conscientiousness * 0.3f;
        candidates.push_back({"wait", s, "being patient"});
    }

    // Idle: always an option
    float idleScore = scoreIdle(context);
    candidates.push_back({"idle", idleScore, "nothing pressing"});

    // Pick the highest scoring action
    std::sort(candidates.begin(), candidates.end(),
              [](const ScoredAction& a, const ScoredAction& b) { return a.score > b.score; });

    const auto& winner = candidates.front();
    CharacterDecision decision;
    decision.action = winner.action;
    decision.reasoning = winner.reasoning;
    decision.emotion = profile.emotion.dominantEmotion();

    // If speaking, generate dialogue
    if (decision.action == "speak") {
        decision.dialogueText = pickDialogueTemplate(decision.emotion);
        if (decision.dialogueText.empty()) {
            decision.dialogueText = "Greetings.";
        }
    }

    return decision;
}

std::string RuleBasedCharacterAgent::pickDialogueTemplate(const std::string& emotion) const {
    auto it = m_dialogueTemplates.find(emotion);
    if (it == m_dialogueTemplates.end() || it->second.empty()) return "";

    // Deterministic pick based on emotion string hash
    size_t idx = std::hash<std::string>{}(emotion) % it->second.size();
    return it->second[idx];
}

float RuleBasedCharacterAgent::scoreFlee(const CharacterDecisionContext& ctx) {
    if (!ctx.profile) return 0.0f;
    const auto& p = *ctx.profile;
    float fearScore = p.emotion.fear * 0.5f + p.traits.neuroticism * 0.3f;
    // Bravery custom trait reduces flee score
    float bravery = p.traits.getTrait("bravery");
    if (bravery > 0.0f) fearScore -= bravery * 0.3f;
    return std::max(0.0f, fearScore);
}

float RuleBasedCharacterAgent::scoreAttack(const CharacterDecisionContext& ctx) {
    if (!ctx.profile) return 0.0f;
    const auto& p = *ctx.profile;
    float aggression = p.emotion.anger * 0.5f + (1.0f - p.traits.agreeableness) * 0.3f;
    // Low openness + high conscientiousness = more likely to follow duty (attack enemies)
    return std::max(0.0f, aggression);
}

float RuleBasedCharacterAgent::scoreTrade(const CharacterDecisionContext& ctx) {
    if (!ctx.profile || !ctx.conversationPartner) return 0.0f;

    const auto* rel = ctx.profile->getRelationship(ctx.conversationPartner->id);
    float trust = rel ? rel->trust : 0.0f;
    // Need positive trust and agreeableness to trade
    return std::max(0.0f, (trust + 1.0f) * 0.25f + ctx.profile->traits.agreeableness * 0.2f);
}

float RuleBasedCharacterAgent::scoreIdle(const CharacterDecisionContext& ctx) {
    if (!ctx.profile) return 0.1f;
    // Low energy / no strong emotions → idle
    float emotionIntensity = std::abs(ctx.profile->emotion.joy) +
                              ctx.profile->emotion.anger +
                              ctx.profile->emotion.fear;
    return std::max(0.1f, 0.4f - emotionIntensity * 0.2f);
}

} // namespace Story
} // namespace Phyxel
